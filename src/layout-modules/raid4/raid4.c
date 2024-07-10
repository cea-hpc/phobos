/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2024 CEA/DAM.
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
 * \brief  Phobos Raid4 Layout plugin
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "raid4.h"

#include "pho_module_loader.h"

#include <errno.h>
#include <glib.h>
#include <string.h>
#include <unistd.h>

#define PLUGIN_NAME     "raid4"
#define PLUGIN_MAJOR    0
#define PLUGIN_MINOR    1

static struct module_desc RAID4_MODULE_DESC = {
    .mod_name  = PLUGIN_NAME,
    .mod_major = PLUGIN_MAJOR,
    .mod_minor = PLUGIN_MINOR,
};

static struct raid_ops RAID4_OPS = {
    .write_split    = raid4_write_split,
    .read_split     = raid4_read_split,
    .get_block_size = raid4_get_block_size,
};

static const struct pho_enc_ops RAID4_ENCODER_OPS = {
    .step       = raid_encoder_step,
    .destroy    = raid_encoder_destroy,
};

/**
 * List of configuration parameters for this module
 */
enum pho_cfg_params_raid4 {
    /* Actual parameters */
    PHO_CFG_LYT_RAID4_extent_xxh128,
    PHO_CFG_LYT_RAID4_extent_md5,

    /* Delimiters, update when modifying options */
    PHO_CFG_LYT_RAID4_FIRST = PHO_CFG_LYT_RAID4_extent_xxh128,
    PHO_CFG_LYT_RAID4_LAST  = PHO_CFG_LYT_RAID4_extent_md5,
};

#if HAVE_XXH128
#define DEFAULT_XXH128 "true"
#define DEFAULT_MD5    "false"
#else
#define DEFAULT_XXH128 "false"
#define DEFAULT_MD5    "true"
#endif

const struct pho_config_item raid4_cfg_items[] = {
    [PHO_CFG_LYT_RAID4_extent_xxh128] = {
        .section = "layout_raid4",
        .name    = "extent_xxh128",
        .value   = DEFAULT_XXH128,
    },
    [PHO_CFG_LYT_RAID4_extent_md5] = {
        .section = "layout_raid4",
        .name    = "extent_md5",
        .value   = DEFAULT_MD5,
    },
};

static int layout_raid4_encode(struct pho_encoder *enc)
{
    struct raid_io_context *io_context;
    int rc;
    int i;

    ENTRY;

    io_context = xcalloc(1, sizeof(*io_context));
    enc->priv_enc = io_context;
    io_context->name = PLUGIN_NAME;
    io_context->n_data_extents = 2;
    io_context->n_parity_extents = 1;
    io_context->write.to_write = enc->xfer->xd_params.put.size;
    io_context->nb_hashes = 3;
    io_context->hashes = xcalloc(io_context->nb_hashes,
                                 sizeof(*io_context->hashes));

    for (i = 0; i < io_context->nb_hashes; i++) {
        rc = extent_hash_init(&io_context->hashes[i],
                              PHO_CFG_GET_BOOL(raid4_cfg_items,
                                               PHO_CFG_LYT_RAID4,
                                               extent_md5,
                                               false),
                              PHO_CFG_GET_BOOL(raid4_cfg_items,
                                               PHO_CFG_LYT_RAID4,
                                               extent_xxh128,
                                               false));
    }

    rc = raid_encoder_init(enc, &RAID4_MODULE_DESC, &RAID4_ENCODER_OPS,
                           &RAID4_OPS);
    if (rc)
        return rc;

    /* Empty PUT does not need any IO */
    if (io_context->write.to_write == 0)
        enc->done = true;

    return 0;
}

static int layout_raid4_decode(struct pho_encoder *dec)
{
    struct raid_io_context *io_context;
    int rc;
    int i;

    ENTRY;

    io_context = xcalloc(1, sizeof(*io_context));
    dec->priv_enc = io_context;
    io_context->name = PLUGIN_NAME;
    io_context->n_data_extents = 2;
    io_context->n_parity_extents = 1;
    rc = raid_decoder_init(dec, &RAID4_MODULE_DESC, &RAID4_ENCODER_OPS,
                           &RAID4_OPS);
    if (rc)
        return rc;

    /* Size is the sum of the extent sizes, dec->layout->wr_size is not
     * positioned properly by the dss
     */
    if (dec->layout->ext_count % 3 != 0)
        LOG_RETURN(-EINVAL,
                   "raid4 Xor layout extents count (%d) is not a multiple of 3",
                   dec->layout->ext_count);

    for (i = 0; i < dec->layout->ext_count; i = i + 3)
        io_context->read.to_read += dec->layout->extents[i].size;

    /* Empty GET does not need any IO */
    if (io_context->read.to_read == 0)
        dec->done = true;

    return 0;
}

static const struct pho_layout_module_ops LAYOUT_RAID4_OPS = {
    .encode = layout_raid4_encode,
    .decode = layout_raid4_decode,
    .locate = NULL,
    .get_specific_attrs = NULL,
    .reconstruct = NULL,
};

/** Layout module registration entry point */
int pho_module_register(void *module, void *context)
{
    struct layout_module *self = (struct layout_module *) module;

    phobos_module_context_set(context);

    self->desc = RAID4_MODULE_DESC;
    self->ops = &LAYOUT_RAID4_OPS;

    return 0;
}

int raid4_get_block_size(struct pho_encoder *enc, size_t *block_size)
{
    struct extent *extent;
    const char *attr;
    int64_t value;

    extent = &enc->layout->extents[0];
    attr = pho_attr_get(&extent->info, "raid4.chunk_size");
    if (!attr)
        LOG_RETURN(-EINVAL,
                   "'raid4.chunk_size' attribute not found on extent '%s'",
                   extent->uuid);

    pho_debug("raid4: found block size '%s' for extent '%s'", attr,
              extent->uuid);
    value = str2int64(attr);
    if (value <= 0)
        LOG_RETURN(-EINVAL,
                   "Invalid block size '%s' found in 'raid4.chunk_size' on "
                   "extent '%s'. Expected a positive integer",
                   attr, extent->uuid);

    *block_size = value;

    return 0;
}
