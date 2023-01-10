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
#include "pho_cfg.h"
#include "pho_io.h"
#include "pho_module_loader.h"

#include <dlfcn.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * Block size parameter read from configuration, only used when writting data
 * to multiple media in raid layout.
 */
#define IO_BLOCK_SIZE_ATTR_KEY "io_block_size"

/**
 * List of configuration parameters for this module
 */
enum pho_cfg_params_lyt {
    /* Actual parameters */
    PHO_CFG_LYT_io_block_size,

    /* Delimiters, update when modifying options */
    PHO_CFG_LYT_FIRST = PHO_CFG_LYT_io_block_size,
    PHO_CFG_LYT_LAST  = PHO_CFG_LYT_io_block_size,
};

const struct pho_config_item cfg_lyt[] = {
    [PHO_CFG_LYT_io_block_size] = {
        .section = "io",
        .name    = IO_BLOCK_SIZE_ATTR_KEY,
        .value   = "0" /** default value = not set. */
    }
};

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

/**
 * Set size_t decoder/encoder value from config file
 *
 * 0 is not a valid write block size, -EINVAL will be returned.
 *
 * @param[out] enc      decoder/encoder with io_block_size to modify
 *
 * @return 0 if success, -error_code if failure
 */
static int get_io_block_size(struct pho_encoder *enc)
{
    const char *string_io_block_size;
    int64_t sz;

    string_io_block_size = PHO_CFG_GET(cfg_lyt, PHO_CFG_LYT, io_block_size);
    if (!string_io_block_size) {
        /* If not forced by configuration, the io adapter will retrieve it
         * from the backend storage system.
         */
        enc->io_block_size = 0;
        return 0;
    }

    sz = str2int64(string_io_block_size);
    if (sz < 0) {
        enc->io_block_size = 0;
        LOG_RETURN(-EINVAL, "Invalid value '%s' for parameter '%s'",
                   string_io_block_size, IO_BLOCK_SIZE_ATTR_KEY);
    }
    enc->io_block_size = sz;

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
    rc = load_module(layout_name, sizeof(*mod), (void **) &mod);
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

    /* get io_block_size from conf */
    rc = get_io_block_size(enc);
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
    rc = load_module(layout_name, sizeof(*mod), (void **) &mod);
    if (rc)
        return rc;

    /* See notes in layout_encode */
    enc->is_decoder = true;
    enc->done = false;
    enc->xfer = xfer;
    enc->layout = layout;

    /* get io_block_size from conf */
    rc = get_io_block_size(enc);
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
    rc = load_module(layout_name, sizeof(*mod), (void **) &mod);
    if (rc)
        return rc;

    return mod->ops->locate(dss, layout, focus_host, hostname, nb_new_lock);
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
