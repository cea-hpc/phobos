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

static int raid4_get_reader_chunk_size(struct pho_data_processor *proc,
                                size_t *chunk_size)
{
    struct extent *extent;
    const char *attr;
    int64_t value;

    extent = &proc->src_layout->extents[0];
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

    *chunk_size = value;

    return 0;
}

static struct raid_ops RAID4_OPS = {
    .get_reader_chunk_size = raid4_get_reader_chunk_size,
    .read_into_buff = raid4_read_into_buff,
    .write_from_buff = raid4_write_from_buff,
    .set_extra_attrs = raid4_extra_attrs,
};

static const struct pho_proc_ops RAID4_WRITER_PROCESSOR_OPS = {
    .step       = raid_writer_processor_step,
    .destroy    = raid_writer_processor_destroy,
};

static const struct pho_proc_ops RAID4_READER_PROCESSOR_OPS = {
    .step       = raid_reader_processor_step,
    .destroy    = raid_reader_processor_destroy,
};

static const struct pho_proc_ops RAID4_ERASER_PROCESSOR_OPS = {
    .step       = raid_eraser_processor_step,
    .destroy    = raid_eraser_processor_destroy,
};

/**
 * List of configuration parameters for this module
 */
enum pho_cfg_params_raid4 {
    /* Actual parameters */
    PHO_CFG_LYT_RAID4_extent_xxh128,
    PHO_CFG_LYT_RAID4_extent_md5,
    PHO_CFG_LYT_RAID4_check_hash,

    /* Delimiters, update when modifying options */
    PHO_CFG_LYT_RAID4_FIRST = PHO_CFG_LYT_RAID4_extent_xxh128,
    PHO_CFG_LYT_RAID4_LAST  = PHO_CFG_LYT_RAID4_check_hash,
};

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
    [PHO_CFG_LYT_RAID4_check_hash] = {
        .section = "layout_raid4",
        .name    = "check_hash",
        .value   = DEFAULT_CHECK_HASH,
    },
};

static int layout_raid4_encode(struct pho_data_processor *encoder)
{
    struct raid_io_context *io_contexts;
    struct raid_io_context *io_context;
    int i, j;
    int rc;

    ENTRY;

    io_contexts = xcalloc(encoder->xfer->xd_ntargets, sizeof(*io_contexts));
    encoder->private_writer = io_contexts;

    for (i = 0; i < encoder->xfer->xd_ntargets; i++) {
        io_context = &io_contexts[i];
        io_context->name = PLUGIN_NAME;
        io_context->n_data_extents = 2;
        io_context->n_parity_extents = 1;
        io_context->write.to_write = encoder->xfer->xd_targets[i].xt_size;
        if (encoder->xfer->xd_targets[i].xt_size == 0)
            io_context->write.all_is_written = true;

        io_context->nb_hashes = 3;
        io_context->hashes = xcalloc(io_context->nb_hashes,
                                     sizeof(*io_context->hashes));

        for (j = 0; j < io_context->nb_hashes; j++) {
            rc = extent_hash_init(&io_context->hashes[j],
                              PHO_CFG_GET_BOOL(raid4_cfg_items,
                                               PHO_CFG_LYT_RAID4,
                                               extent_md5,
                                               false),
                              PHO_CFG_GET_BOOL(raid4_cfg_items,
                                               PHO_CFG_LYT_RAID4,
                                               extent_xxh128,
                                               false));
            if (rc)
                goto out_hash;
        }
    }

    return raid_encoder_init(encoder, &RAID4_MODULE_DESC,
                             &RAID4_WRITER_PROCESSOR_OPS, &RAID4_OPS);

out_hash:
    for (i -= 1; i >= 0; i--) {
        io_context = &io_contexts[i];
        for (j = 0; j < io_context->nb_hashes; j++)
            extent_hash_fini(&io_context->hashes[i]);
        io_context->nb_hashes = 0;
    }

    /* The rest will be free'd by layout_destroy */
    return rc;
}

static int layout_raid4_decode(struct pho_data_processor *decoder)
{
    struct raid_io_context *io_context;
    int rc;

    ENTRY;

    io_context = xcalloc(1, sizeof(*io_context));
    decoder->private_reader = io_context;
    io_context->name = PLUGIN_NAME;
    io_context->n_data_extents = 2;
    io_context->n_parity_extents = 1;

    io_context->read.check_hash = PHO_CFG_GET_BOOL(raid4_cfg_items,
                                                   PHO_CFG_LYT_RAID4,
                                                   check_hash, true);

    if (io_context->read.check_hash) {
        io_context->nb_hashes = io_context->n_data_extents;
        io_context->hashes = xcalloc(io_context->nb_hashes,
                                     sizeof(*io_context->hashes));
    }

    rc = raid_decoder_init(decoder, &RAID4_MODULE_DESC,
                           &RAID4_READER_PROCESSOR_OPS, &RAID4_OPS);
    if (rc)
        return rc;

    io_context->read.to_read = decoder->object_size;

    /* Empty GET does not need any IO */
    if (decoder->object_size == 0)
        decoder->done = true;

    return 0;
}

static int layout_raid4_erase(struct pho_data_processor *eraser)
{
    struct raid_io_context *io_context;
    int rc;

    io_context = xcalloc(1, sizeof(*io_context));
    eraser->private_eraser = io_context;
    io_context->name = PLUGIN_NAME;
    io_context->n_data_extents = 2;
    io_context->n_parity_extents = 1;

    rc = raid_eraser_init(eraser, &RAID4_MODULE_DESC,
                          &RAID4_ERASER_PROCESSOR_OPS, &RAID4_OPS);

    if (rc) {
        eraser->private_eraser = NULL;
        free(io_context);
    }

    io_context->delete.to_delete = 0;
    /* No hard removal on tapes */
    if (eraser->src_layout->ext_count != 0 &&
        eraser->src_layout->extents[0].media.family != PHO_RSC_TAPE)
        io_context->delete.to_delete = eraser->src_layout->ext_count;

    if (io_context->delete.to_delete == 0)
        eraser->done = true;

    return rc;
}

static int layout_raid4_locate(struct dss_handle *dss,
                               struct layout_info *layout,
                               const char *focus_host,
                               char **hostname,
                               int *nb_new_lock)
{
    return raid_locate(dss, layout, 2, 1, focus_host, hostname, nb_new_lock);
}

static int layout_raid4_reconstruct(struct layout_info layout,
                                    struct copy_info *copy)
{
    ssize_t split_sizes[3] = {0};
    ssize_t extent_sizes = 0;
    struct extent *extents;
    int extent_count;
    int object_size;
    int odd;

    extent_count = layout.ext_count;
    extents = layout.extents;

    object_size = get_object_size_from_layout(&layout);
    if (object_size < 0)
        LOG_RETURN(-EINVAL,
                   "Invalid object size for reconstruction of object '%s': '%d'",
                   layout.oid, object_size);

    for (int i = 0; i < extent_count; i++) {
        int index = (extents[i].layout_idx % 3);

        split_sizes[index] += extents[i].size;
        extent_sizes += extents[i].size;
    }

    /**
     * If object_size is even, then:
     * size(first half) = size(second half) = size(xor) = object_size / 2
     *
     * So a copy is considered complete if the combined size of all extents is
     * equal to (3 * object_size) / 2.
     *
     * If object_size is odd, then:
     * size(first half) = size(xor) = (object_size + 1) / 2
     * size(second half) = (object_size - 1) / 2
     *
     * So a copy is considered complete if the combined size of all extents is
     * equal to:
     * (2 * (object_size + 1) + object_size - 1) / 2 = (3 * object_size + 1) / 2
     *
     * All in all, a copy is considered complete if the combined size of all
     * extents is equal to (3 * object_size + (object_size % 2)) / 2.
     *
     * Similarly, a copy is considered readable if size of the first half or the
     * second half + the size of the xor is equal to the object size, or the
     * size of the first half + the size of the xor is equal to the object size
     * + 1.
     */
    odd = (object_size % 2);

    if (extent_sizes == ((3 * object_size + odd) / 2))
        copy->copy_status = PHO_COPY_STATUS_COMPLETE;
    else if (split_sizes[0] + split_sizes[1] == object_size ||
             split_sizes[0] + split_sizes[2] == object_size + odd ||
             split_sizes[1] + split_sizes[2] == object_size)
        copy->copy_status = PHO_COPY_STATUS_READABLE;
    else
        copy->copy_status = PHO_COPY_STATUS_INCOMPLETE;

    return 0;
}

static const struct pho_layout_module_ops LAYOUT_RAID4_OPS = {
    .encode = layout_raid4_encode,
    .decode = layout_raid4_decode,
    .erase = layout_raid4_erase,
    .locate = layout_raid4_locate,
    .get_specific_attrs = NULL,
    .reconstruct = layout_raid4_reconstruct,
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
