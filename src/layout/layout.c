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

#include "pho_layout.h"

#include "phobos_store.h"
#include "pho_common.h"
#include "pho_dss.h"
#include "pho_type_utils.h"
#include "pho_cfg.h"
#include "pho_io.h"

#include <dlfcn.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static pthread_rwlock_t layout_modules_rwlock;
static GHashTable *layout_modules;

typedef int (*mod_init_func_t)(struct layout_module *);

/**
 * Initializes layout_modules_rwlock and the layout_modules hash table.
 */
__attribute__((constructor)) static void layout_globals_init(void)
{
    int rc;

    rc = pthread_rwlock_init(&layout_modules_rwlock, NULL);
    if (rc) {
        fprintf(stderr,
                "Unexpected error: cannot initialize layout rwlock: %s\n",
                strerror(rc));
        exit(EXIT_FAILURE);
    }

    /* Note: layouts are not meant to be unloaded for now */
    layout_modules = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
                                           NULL);
    if (layout_modules == NULL) {
        fprintf(stderr, "Could not create layout modules hash table\n");
        exit(EXIT_FAILURE);
    }
}

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
 * Load a layout module by \a mod_name into \a mod.
 *
 * @param[in]   mod_name    Name of the module to load.
 * @param[out]  mod         Module descriptor to fill out.
 *
 * @return 0 on success, -errno on error.
 */
static int layout_module_load(const char *mod_name, struct layout_module **mod)
{
    mod_init_func_t op_init;
    char libpath[NAME_MAX];
    void *hdl;
    int rc;

    ENTRY;

    pho_debug("Loading layout module '%s'", mod_name);

    rc = build_module_instance_path(mod_name, libpath, sizeof(libpath));
    if (rc)
        LOG_RETURN(rc, "Invalid layout module name '%s'", mod_name);

    hdl = dlopen(libpath, RTLD_NOW);
    if (hdl == NULL)
        LOG_RETURN(-EINVAL, "Cannot load module '%s': %s", mod_name, dlerror());

    *mod = calloc(1, sizeof(**mod));
    if (*mod == NULL)
        GOTO(out_err, rc = -ENOMEM);

    op_init = dlsym(hdl, PLM_OP_INIT);
    if (!op_init)
        LOG_RETURN(-ENOSYS, "Operation '%s' is missing", PLM_OP_INIT);

    rc = op_init(*mod);
    if (rc)
        LOG_GOTO(out_err, rc, "Could not initialize module '%s'", mod_name);

    pho_debug("Plugin %s-%d.%d successfully loaded",
              (*mod)->desc.mod_name,
              (*mod)->desc.mod_major,
              (*mod)->desc.mod_minor);

    (*mod)->dl_handle = hdl;

out_err:
    if (rc) {
        free(*mod);
        dlclose(hdl);
    }

    return rc;
}

/**
 * Load a layout module if it has not already been. Relies on the global
 * layout_modules hash map and its associated rwlock.
 *
 * @param[in]   mod_name    Name of the module to load.
 * @param[out]  mod         Module descriptor to fill out.
 *
 * @return 0 on success, -errno on error.
 */
static int layout_module_lazy_load(const char *mod_name,
                                   struct layout_module **module)
{
    int rc;

    rc = pthread_rwlock_rdlock(&layout_modules_rwlock);
    if (rc)
        LOG_RETURN(rc, "Cannot read lock layout module table");

    /* Check if module loaded */
    *module = g_hash_table_lookup(layout_modules, mod_name);

    /* If not loaded, unlock, write lock, check if it is loaded and load it */
    if (*module == NULL) {
        /* Release the read lock */
        rc = pthread_rwlock_unlock(&layout_modules_rwlock);
        if (rc)
            LOG_RETURN(rc, "Cannot unlock layout module table");

        /* Re-acquire a write lock */
        rc = pthread_rwlock_wrlock(&layout_modules_rwlock);
        if (rc)
            LOG_RETURN(rc, "Cannot write lock layout module table");

        /*
         * Re-check if module loaded (this could have changed between the read
         * lock release and the write lock acquisition)
         */
        *module = g_hash_table_lookup(layout_modules, mod_name);
        if (*module != NULL)
            goto out_unlock;

        rc = layout_module_load(mod_name, module);
        if (rc)
            LOG_GOTO(out_unlock, rc, "Error while loading layout module %s",
                     mod_name);

        /* Insert the module into the module table */
        g_hash_table_insert(layout_modules, strdup(mod_name), *module);

        /* Write lock is released in function final cleanup */
    }

out_unlock:
    rc = pthread_rwlock_unlock(&layout_modules_rwlock);
    if (rc)
        LOG_RETURN(rc, "Cannot unlock layout module table");

    return rc;
}

int layout_encode(struct pho_encoder *enc, struct pho_xfer_desc *xfer)
{
    struct layout_module *mod;
    int rc;

    /* Load new module if necessary */
    rc = layout_module_lazy_load(xfer->xd_params.put.layout_name, &mod);
    if (rc)
        return rc;

    /*
     * Note 1: we don't acquire layout_modules_rwlock because we consider that
     * the module we just retrieved will never be unloaded.
     */
    /*
     * Note 2: the encoder module must not be unloaded until all encoders of
     * this type have been destroyed, since they all hold a reference to the
     * module's code. In this implementation, the module is never unloaded.
     */
    enc->is_decoder = false;
    enc->done = false;
    enc->xfer = xfer;
    enc->layout = calloc(1, sizeof(*enc->layout));
    if (enc->layout == NULL)
        return -ENOMEM;
    enc->layout->oid = xfer->xd_objid;
    enc->layout->wr_size = xfer->xd_params.put.size;
    enc->layout->state = PHO_EXT_ST_PENDING;

    rc = mod->ops->encode(enc);
    if (rc)
        layout_destroy(enc);

    return rc;
}

int layout_decode(struct pho_encoder *enc, struct pho_xfer_desc *xfer,
                  struct layout_info *layout)
{
    struct layout_module *mod;
    int rc;

    /* Load new module if necessary */
    rc = layout_module_lazy_load(layout->layout_desc.mod_name, &mod);
    if (rc)
        return rc;

    /* See notes in layout_encode */
    enc->is_decoder = true;
    enc->done = false;
    enc->xfer = xfer;
    enc->layout = layout;

    rc = mod->ops->decode(enc);
    if (rc)
        layout_destroy(enc); /* free private decoder if needed */

    return rc;
}

void layout_destroy(struct pho_encoder *enc)
{
    /* Only encoders own their layout */
    if (!enc->is_decoder && enc->layout != NULL) {
        layout_info_free_extents(enc->layout);
        free(enc->layout);
        enc->layout = NULL;
    }

    /* Not fully initialized */
    if (enc->ops == NULL)
        return;

    CHECK_ENC_OP(enc, destroy);

    enc->ops->destroy(enc);
}
