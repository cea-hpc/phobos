/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2022 CEA/DAM.
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
#include "pho_io.h"
#include "pho_module_loader.h"

#include <dlfcn.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uuid/uuid.h>

char *generate_uuid(void)
{
    uuid_t binary_uuid;
    char *uuid;

    uuid = xmalloc(UUID_LEN);
    uuid_generate_random(binary_uuid);
    uuid_unparse(binary_uuid, uuid);

    return uuid;
}

static int build_layout_name(const char *layout_name, char *path, size_t len)
{
    int rc;

    rc = snprintf(path, len, "layout_%s", layout_name);
    if (rc < 0 || rc >= len) {
        path[0] = '\0';
        return -EINVAL;
    }

    return 0;
}

int layout_encode(struct pho_encoder *enc, struct pho_xfer_desc *xfer)
{
    char layout_name[NAME_MAX];
    struct layout_module *mod;
    int rc;

    rc = build_layout_name(xfer->xd_params.put.layout_name, layout_name,
                           sizeof(layout_name));
    if (rc)
        return rc;

    /* Load new module if necessary */
    rc = load_module(layout_name, sizeof(*mod), phobos_context(),
                     (void **) &mod);
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
    enc->layout = xcalloc(1, sizeof(*enc->layout));
    enc->layout->oid = xfer->xd_objid;
    enc->layout->wr_size = xfer->xd_params.put.size;

    /* get io_block_size from conf */
    rc = get_io_block_size(&enc->io_block_size);
    if (rc) {
        layout_destroy(enc);
        return rc;
    }

    rc = mod->ops->encode(enc);
    if (rc) {
        layout_destroy(enc);
        LOG_RETURN(rc, "Unable to create encoder");
    }

    return rc;
}

int layout_decode(struct pho_encoder *enc, struct pho_xfer_desc *xfer,
                  struct layout_info *layout)
{
    char layout_name[NAME_MAX];
    struct layout_module *mod;
    int rc;

    rc = build_layout_name(layout->layout_desc.mod_name, layout_name,
                           sizeof(layout_name));
    if (rc)
        return rc;

    /* Load new module if necessary */
    rc = load_module(layout_name, sizeof(*mod), phobos_context(),
                     (void **) &mod);
    if (rc)
        return rc;

    /* See notes in layout_encode */
    enc->is_decoder = true;
    enc->done = false;
    enc->xfer = xfer;
    enc->layout = layout;

    /* get io_block_size from conf */
    rc = get_io_block_size(&enc->io_block_size);
    if (rc) {
        layout_destroy(enc);
        LOG_RETURN(rc, "Unable to get io_block_size");
    }

    rc = mod->ops->decode(enc);
    if (rc) {
        layout_destroy(enc);
        LOG_RETURN(rc, "Unable to create decoder");
    }

    return rc;
}

int layout_locate(struct dss_handle *dss, struct layout_info *layout,
                  const char *focus_host, char **hostname, int *nb_new_lock)
{
    char layout_name[NAME_MAX];
    struct layout_module *mod;
    int rc;

    *hostname = NULL;

    rc = build_layout_name(layout->layout_desc.mod_name, layout_name,
                           sizeof(layout_name));
    if (rc)
        return rc;

    /* Load new module if necessary */
    rc = load_module(layout_name, sizeof(*mod), phobos_context(),
                     (void **) &mod);
    if (rc)
        return rc;

    return mod->ops->locate(dss, layout, focus_host, hostname, nb_new_lock);
}

int layout_get_info_from_xattrs(int fd, struct layout_info *lyt,
                                struct object_info *obj, struct extent *ext)
{
    char layout_name[NAME_MAX];
    struct layout_module *mod;
    int rc = 0;

    rc = build_layout_name(lyt->layout_desc.mod_name, layout_name,
                           sizeof(layout_name));
    if (rc)
        return rc;

    rc = load_module(layout_name, sizeof(*mod), phobos_context(),
                     (void **) &mod);
    if (rc)
        return rc;

    return mod->ops->get_info_from_xattrs(fd, lyt, obj, ext);
}

int layout_reconstruct(struct layout_info lyt, struct object_info *obj)
{
    char layout_name[NAME_MAX];
    struct layout_module *mod;
    int rc;

    rc = build_layout_name(lyt.layout_desc.mod_name, layout_name,
                           sizeof(layout_name));
    if (rc)
        return rc;

    /* Load new module if necessary */
    rc = load_module(layout_name, sizeof(*mod), phobos_context(),
                     (void **) &mod);
    if (rc)
        return rc;

    return mod->ops->reconstruct(lyt, obj);
}

void layout_destroy(struct pho_encoder *enc)
{
    /* Only encoders own their layout */
    if (!enc->is_decoder && enc->layout != NULL) {
        pho_attrs_free(&enc->layout->layout_desc.mod_attrs);
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
