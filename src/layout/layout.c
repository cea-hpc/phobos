/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2017 CEA/DAM.
 *
 *  This file is part of Phobos.
 *
 *  Phobos is free software: you can redistribute it and/or modify it under
 *  the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation, either version 2.1 of the License, or
 *  (at your option) any later version.
 *
 *  Phobos is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with Phobos. If not, see <http://www.gnu.org/licenses/>.
 */
/**
 * \brief  Phobos Layout Composition Layer
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "pho_lrs.h"
#include "pho_common.h"
#include "pho_dss.h"
#include "pho_type_utils.h"
#include "pho_cfg.h"
#include "pho_layout.h"
#include "pho_io.h"

#include <dlfcn.h>
#include <limits.h>

/* Expected ctor/dtor types for external layout modules */
typedef int (*mod_init_func_t)(struct layout_module *, enum layout_action);

typedef void (*mod_fini_func_t)(struct layout_module *);

/* As for now we do not have to load more than a single module at a time */
static struct layout_module ActiveLayoutModule;

/**
 * Convert layout name into the corresponding module library name.
 * Actual path resolution will be handled by regular LD mechanism.
 *
 * eg. "simple" -> "libpho_layout_simple.so"
 *
 * \return 0 on success, negated errno on error.
 */
static int build_module_instance_path(const char *name, char *path, size_t len)
{
    int rc;

    rc = snprintf(path, len, "libpho_layout_%s.so", name);
    if (rc < 0 || rc >= len) {
        path[0] = '\0';
        return -EINVAL;
    }

    return 0;
}

/**
 * Load and register a layout module for a given mode (\a action).
 * The module must provide an initialization function which will be called.
 *
 * This function is in turn responsible for filling in module description and
 * operation vector.
 */
static int layout_register(const char *name, struct layout_module *mod,
                           enum layout_action action)
{
    mod_init_func_t  op_init;
    char             libpath[NAME_MAX];
    void            *hdl;
    int              rc;
    ENTRY;

    rc = build_module_instance_path(name, libpath, sizeof(libpath));
    if (rc)
        LOG_RETURN(rc, "Invalid layout module name '%s'", name);

    pho_debug("Loading layout module '%s'", name);

    hdl = dlopen(libpath, RTLD_NOW);
    if (!hdl)
        LOG_RETURN(-ENOSYS, "Cannot load module '%s': %s", name, dlerror());

    op_init = dlsym(hdl, PLM_OP_INIT);
    if (!op_init)
        LOG_RETURN(-ENOSYS, "Operation '%s' is missing", PLM_OP_INIT);

    rc = op_init(mod, action);
    if (rc)
        LOG_GOTO(out_err, rc, "Cannot initialize module '%s'", name);

    pho_debug("Plugin %s-%d.%d successfully loaded",
              mod->lm_desc.mod_name,
              mod->lm_desc.mod_major,
              mod->lm_desc.mod_minor);

    mod->lm_dl_handle = hdl;

out_err:
    if (rc)
        dlclose(hdl);

    return rc;
}

static int layout_deregister(struct layout_module *mod)
{

    mod_fini_func_t  op_fini;
    int              rc;
    ENTRY;

    /* Optional teardown function */
    op_fini = dlsym(mod->lm_dl_handle, PLM_OP_FINI);
    if (op_fini)
        op_fini(mod);

    rc = dlclose(mod->lm_dl_handle);
    if (rc)
        LOG_RETURN(rc = -EINVAL, "Cannot release module handle: %s", dlerror());

    /* In case attributes were set... */

    memset(mod, 0, sizeof(*mod));
    return 0;
}

int layout_init(struct dss_handle *dss, struct layout_composer *comp,
                enum layout_action action)
{
    ENTRY;

    comp->lc_dss     = dss;
    comp->lc_action  = action;
    comp->lc_layouts = g_hash_table_new(g_str_hash, g_str_equal);
    comp->lc_private = NULL;
    comp->lc_tags    = NO_TAGS;
    return 0;
}

static bool declaration_is_valid(const struct layout_composer *comp,
                                 const struct layout_info *layout)
{
    if (comp->lc_action == LA_ENCODE && layout->ext_count != 0)
        return false;

    if (comp->lc_action == LA_DECODE && layout->ext_count == 0)
        return false;

    return true;
}

int layout_declare(struct layout_composer *comp, struct layout_info *layout)
{
    ENTRY;

    if (!declaration_is_valid(comp, layout))
        LOG_RETURN(-EINVAL, "Invalid layout composition request");

    g_hash_table_insert(comp->lc_layouts, layout->oid, layout);
    return 0;
}

static gboolean find_any_cb(void *key, void *val, void *udata)
{
    return true;
}

static const char *comp_layout_name_get(const struct layout_composer *comp)
{
    const struct layout_info    *val;

    val = g_hash_table_find(comp->lc_layouts, find_any_cb, NULL);
    if (!val)
        return NULL;

    return val->layout_desc.mod_name;
}

static void set_mod_desc_cb(void *key, void *value, void *udata)
{
    struct layout_info  *layout = value;

    layout->layout_desc = ActiveLayoutModule.lm_desc;
}

int layout_acquire(struct layout_composer *comp)
{
    const char  *mod_name = comp_layout_name_get(comp);
    int          rc;
    ENTRY;

    /* 1 - Load appropriate plugin. This has to be done after layout_declare()
     * since the layout type (and therefore: the layout module) to use is not
     * known before.
     */
    pho_verb("Registering module '%s'", mod_name);

    rc = layout_register(mod_name, &ActiveLayoutModule, comp->lc_action);
    if (rc)
        LOG_RETURN(rc, "Layout module '%s' registration failed", mod_name);

    /* 2 - Let the plugin compose a LRS intent list according to its needs */
    rc = layout_module_compose(&ActiveLayoutModule, comp);
    if (rc)
        LOG_GOTO(err_dereg, rc, "Module failed to generate an intent list");

    /* 3 - Store the module version with which data will be encoded */
    if (comp->lc_action == LA_ENCODE)
        g_hash_table_foreach(comp->lc_layouts, set_mod_desc_cb, NULL);

err_dereg:
    if (rc)
        layout_deregister(&ActiveLayoutModule);

    return rc;
}

int layout_io(struct layout_composer *comp, const char *objid,
              struct pho_io_descr *iod)
{
    ENTRY;
    return layout_module_io_submit(&ActiveLayoutModule, comp, objid, iod);
}

int layout_commit(struct layout_composer *comp, int errcode)
{
    const char  *mod_name = ActiveLayoutModule.lm_desc.mod_name;
    int          rc;
    ENTRY;

    rc = layout_module_io_commit(&ActiveLayoutModule, comp, errcode);
    if (rc)
        LOG_RETURN(rc, "Cannot commit pending transactions for '%s'", mod_name);

    return 0;
}

static inline bool layout_module_is_registered(const struct layout_module *mod)
{
    return mod && mod->lm_ops;
}

int layout_fini(struct layout_composer *comp)
{
    const char  *mod_name = ActiveLayoutModule.lm_desc.mod_name;
    ENTRY;

    /* This function can be called with unregistered modules for instance
     * if comp init is OK but layout_acquire fails to register the module
     */
    if (!layout_module_is_registered(&ActiveLayoutModule))
        return 0;

    pho_verb("Deregistering module '%s'", mod_name);

    if (comp->lc_private_dtor)
        comp->lc_private_dtor(comp);

    tags_free(&comp->lc_tags);
    g_hash_table_destroy(comp->lc_layouts);
    return layout_deregister(&ActiveLayoutModule);
}
