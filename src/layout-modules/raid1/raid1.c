/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2025 CEA/DAM.
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
 * \brief  Phobos Raid1 Layout plugin
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <errno.h>
#include <glib.h>
#include <openssl/evp.h>
#include <string.h>
#include <unistd.h>
#ifdef HAVE_XXH128
#include <xxhash.h>
#endif

#include "pho_attrs.h"
#include "pho_cfg.h"
#include "pho_common.h"
#include "pho_dss.h"
#include "pho_dss_wrapper.h"
#include "pho_io.h"
#include "pho_layout.h"
#include "pho_module_loader.h"
#include "pho_srl_common.h"
#include "pho_type_utils.h"
#include "raid1.h"
#include "raid_common.h"

#define PLUGIN_NAME     "raid1"
#define PLUGIN_MAJOR    0
#define PLUGIN_MINOR    2

static struct module_desc RAID1_MODULE_DESC = {
    .mod_name  = PLUGIN_NAME,
    .mod_major = PLUGIN_MAJOR,
    .mod_minor = PLUGIN_MINOR,
};

/**
 * List of configuration parameters for this module
 */
enum pho_cfg_params_raid1 {
    /* Actual parameters */
    PHO_CFG_LYT_RAID1_repl_count,
    PHO_CFG_LYT_RAID1_extent_xxh128,
    PHO_CFG_LYT_RAID1_extent_md5,
    PHO_CFG_LYT_RAID1_check_hash,

    /* Delimiters, update when modifying options */
    PHO_CFG_LYT_RAID1_FIRST = PHO_CFG_LYT_RAID1_repl_count,
    PHO_CFG_LYT_RAID1_LAST  = PHO_CFG_LYT_RAID1_check_hash,
};

const struct pho_config_item cfg_lyt_raid1[] = {
    [PHO_CFG_LYT_RAID1_repl_count] = {
        .section = "layout_raid1",
        .name    = REPL_COUNT_ATTR_KEY,
        .value   = "2"  /* Total # of copies (default) */
    },
    [PHO_CFG_LYT_RAID1_extent_xxh128] = {
        .section = "layout_raid1",
        .name    = EXTENT_XXH128_ATTR_KEY,
        .value   = DEFAULT_XXH128,
    },
    [PHO_CFG_LYT_RAID1_extent_md5] = {
        .section = "layout_raid1",
        .name    = EXTENT_MD5_ATTR_KEY,
        .value   = DEFAULT_MD5
    },
    [PHO_CFG_LYT_RAID1_check_hash] = {
        .section = "layout_raid1",
        .name    = "check_hash",
        .value   = DEFAULT_CHECK_HASH,
    },
};

int raid1_repl_count(struct layout_info *layout, unsigned int *repl_count)
{
    const char *string_repl_count = pho_attr_get(&layout->layout_desc.mod_attrs,
                                                 PHO_EA_RAID1_REPL_COUNT_NAME);

    if (string_repl_count == NULL)
        /* Ensure we can read objects from the old schema, which have the
         * replica count under the name 'repl_count' and not 'raid1.repl_count'
         */
        string_repl_count = pho_attr_get(&layout->layout_desc.mod_attrs,
                                         "repl_count");

    if (string_repl_count == NULL)
        LOG_RETURN(-ENOENT, "Unable to get replica count from layout attrs");

    errno = 0;
    *repl_count = strtoul(string_repl_count, NULL, 10);
    if (errno != 0 || *repl_count == 0)
        LOG_RETURN(-EINVAL, "Invalid replica count '%s'", string_repl_count);

    return 0;
}

static int set_layout_specific_md(int layout_index, int replica_count,
                                  struct pho_io_descr *iod)
{
    char str_buffer[16];
    int rc = 0;

    rc = sprintf(str_buffer, "%d", layout_index);
    if (rc < 0)
        LOG_GOTO(attrs_free, rc = -errno,
                 "Unable to construct extent index buffer");

    pho_attr_set(&iod->iod_attrs, PHO_EA_RAID1_EXTENT_INDEX_NAME, str_buffer);

    rc = sprintf(str_buffer, "%d", replica_count);
    if (rc < 0)
        LOG_GOTO(attrs_free, rc = -errno,
                 "Unable to construct replica count buffer");

    pho_attr_set(&iod->iod_attrs, PHO_EA_RAID1_REPL_COUNT_NAME, str_buffer);

    return 0;

attrs_free:
    pho_attrs_free(&iod->iod_attrs);

    return rc;
}

static int raid1_read_into_buff(struct pho_data_processor *proc)
{
    size_t buffer_data_size = proc->reader_offset - proc->buffer_offset;
    struct raid_io_context *io_context =
        (struct raid_io_context *)proc->private_reader;
    size_t inside_split_offset = proc->reader_offset -
                                 io_context->current_split_offset;
    char *buff_start = proc->buff.buff + buffer_data_size;
    size_t to_read;
    int rc;

    ENTRY;

    /* limit read : object -> split -> buffer */
    to_read = min(proc->object_size - proc->reader_offset,
                  io_context->read.extents[0]->size - inside_split_offset);
    to_read = min(to_read, proc->buff.size - buffer_data_size);

    rc = data_processor_read_into_buff(proc, &io_context->iods[0], to_read);
    if (rc)
        return rc;

    if (io_context->read.check_hash)
        return extent_hash_update(&io_context->hashes[0], buff_start, to_read);
    else
        return 0;
}

static int raid1_write_from_buff(struct pho_data_processor *proc)
{
    struct raid_io_context *io_context =
        &((struct raid_io_context *)proc->private_writer)[proc->current_target];
    size_t inside_split_offset = proc->writer_offset -
                                 io_context->current_split_offset;
    size_t repl_count = io_context->n_data_extents +
                        io_context->n_parity_extents;
    struct pho_io_descr *iods = io_context->iods;
    char *buff_start = proc->buff.buff +
                       (proc->writer_offset - proc->buffer_offset);
    size_t to_write;
    int rc;
    int i;

    ENTRY;

    /* limit write : split -> buffer */
    to_write = min(io_context->write.extents[0].size - inside_split_offset,
                   proc->reader_offset - proc->writer_offset);

    for (i = 0; i < repl_count; ++i) {
        rc = ioa_write(iods[i].iod_ioa, &iods[i], buff_start, to_write);
        if (rc)
            LOG_RETURN(rc,
                       "RAID1 write: unable to write %zu bytes in replica %d "
                       "at offset %zu", to_write, i, proc->writer_offset);

        iods[i].iod_size += to_write;

        rc = extent_hash_update(&io_context->hashes[i], buff_start, to_write);
        if (rc)
            return rc;
    }

    proc->writer_offset += to_write;
    if (proc->writer_offset == proc->reader_offset)
        proc->buffer_offset = proc->writer_offset;

    if (proc->writer_offset >= proc->object_size)
        io_context->write.all_is_written = true;

    return 0;
}

static int raid1_get_reader_chunk_size(struct pho_data_processor *enc,
                                       size_t *block_size)
{
    (void) enc;
    (void) block_size;

    return 0;
}

static int raid1_extra_attrs(struct pho_data_processor *proc)
{
    struct raid_io_context *io_context =
        &((struct raid_io_context *)proc->private_writer)[proc->current_target];
    size_t repl_count = io_context->n_data_extents +
        io_context->n_parity_extents;
    struct pho_io_descr *iods = io_context->iods;
    size_t i;

    for (i = 0; i < repl_count; ++i) {
        struct extent *extent = &io_context->write.extents[i];
        int rc;

        rc = set_layout_specific_md(extent->layout_idx, repl_count, &iods[i]);
        if (rc)
            LOG_RETURN(rc,
                       "Failed to set layout specific attributes on extent "
                       "'%s'", extent->uuid);
    }

    return 0;
}

static const struct pho_proc_ops RAID1_WRITER_PROCESSOR_OPS = {
    .step       = raid_writer_processor_step,
    .destroy    = raid_writer_processor_destroy,
};

static const struct pho_proc_ops RAID1_READER_PROCESSOR_OPS = {
    .step       = raid_reader_processor_step,
    .destroy    = raid_reader_processor_destroy,
};

static const struct pho_proc_ops RAID1_ERASER_PROCESSOR_OPS = {
    .step       = raid_eraser_processor_step,
    .destroy    = raid_eraser_processor_destroy,
};

static const struct raid_ops RAID1_OPS = {
    .get_reader_chunk_size = raid1_get_reader_chunk_size,
    .read_into_buff = raid1_read_into_buff,
    .write_from_buff = raid1_write_from_buff,
    .set_extra_attrs = raid1_extra_attrs,
};

static int raid1_encoder_get_repl_count(struct pho_data_processor *enc,
                                        unsigned int *repl_count)
{
    struct pho_xfer_put_params *put_params;
    const char *string_repl_count;
    int rc;
    int i;

    *repl_count = 0;

    if (enc->xfer->xd_op == PHO_XFER_OP_COPY)
        put_params = &enc->xfer->xd_params.copy.put;
    else
        put_params = &enc->xfer->xd_params.put;

    if (pho_attrs_is_empty(&put_params->lyt_params))
        string_repl_count = PHO_CFG_GET(cfg_lyt_raid1, PHO_CFG_LYT_RAID1,
                                        repl_count);
    else
        string_repl_count = pho_attr_get(&put_params->lyt_params,
                                         REPL_COUNT_ATTR_KEY);

    if (string_repl_count == NULL)
        LOG_RETURN(-EINVAL, "Unable to get replica count from conf to "
                            "build a raid1 encoder");

    for (i = 0; i < enc->xfer->xd_ntargets; i++) {
        /* set repl_count as char * in layout */
        pho_attr_set(&enc->dest_layout[i].layout_desc.mod_attrs,
                     PHO_EA_RAID1_REPL_COUNT_NAME, string_repl_count);

        /* set repl_count in encoder */
        rc = raid1_repl_count(&enc->dest_layout[i], repl_count);
        if (rc)
            LOG_RETURN(rc, "Invalid replica count from layout to build raid1 "
                           "encoder");

        /* set write size */
        if (*repl_count <= 0)
            LOG_RETURN(-EINVAL, "Invalid # of replica (%d)", *repl_count);
    }

    return 0;
}

/**
 * Create an encoder.
 *
 * This function initializes the internal raid1_encoder based on encoder->xfer
 * and encoder->dest_layout.
 *
 * Implements the layout_encode layout module methods.
 */
static int layout_raid1_encode(struct pho_data_processor *encoder)
{
    struct raid_io_context *io_contexts;
    struct raid_io_context *io_context;
    unsigned int repl_count;
    size_t i, j;
    int rc;

    rc = raid1_encoder_get_repl_count(encoder, &repl_count);
    if (rc)
        return rc;

    io_contexts = xcalloc(encoder->xfer->xd_ntargets, sizeof(*io_contexts));
    encoder->private_writer = io_contexts;
    for (i = 0; i < encoder->xfer->xd_ntargets; i++) {
        io_context = &io_contexts[i];
        io_context->name = PLUGIN_NAME;
        io_context->n_data_extents = 1;
        io_context->n_parity_extents = repl_count - 1;
        io_context->write.to_write = encoder->xfer->xd_targets[i].xt_size;
        if (encoder->xfer->xd_targets[i].xt_size == 0)
            io_context->write.all_is_written = true;

        io_context->nb_hashes = repl_count;
        io_context->hashes = xcalloc(io_context->nb_hashes,
                                     sizeof(*io_context->hashes));

        for (j = 0; j < io_context->nb_hashes; j++) {
            rc = extent_hash_init(&io_context->hashes[j],
                                  PHO_CFG_GET_BOOL(cfg_lyt_raid1,
                                                   PHO_CFG_LYT_RAID1,
                                                   extent_md5, false),
                                  PHO_CFG_GET_BOOL(cfg_lyt_raid1,
                                                   PHO_CFG_LYT_RAID1,
                                                   extent_xxh128, false));
            if (rc)
                goto out_hash;
        }
    }

    return raid_encoder_init(encoder, &RAID1_MODULE_DESC,
                             &RAID1_WRITER_PROCESSOR_OPS,
                             &RAID1_OPS);

out_hash:
    for (i -= 1; i >= 0; i--) {
        io_context = &io_contexts[i];
        for (j = 0; j < io_context->nb_hashes; j++)
            extent_hash_fini(&io_context->hashes[j]);
    }

    /* The rest will be free'd by layout destroy */
    return rc;
}

/** Implements layout_decode layout module methods. */
static int layout_raid1_decode(struct pho_data_processor *decoder)
{
    struct raid_io_context *io_context;
    unsigned int repl_count;
    int rc;
    int i;

    ENTRY;

    rc = raid1_repl_count(decoder->src_layout, &repl_count);
    if (rc)
        LOG_RETURN(rc,
                   "Invalid replica count from layout to build raid1 decoder");

    if (decoder->src_layout->ext_count % repl_count != 0)
        LOG_RETURN(-EINVAL, "layout extents count (%d) is not a multiple "
                   "of replica count (%u)",
                   decoder->src_layout->ext_count, repl_count);

    io_context = xcalloc(1, sizeof(*io_context));
    decoder->private_reader = io_context;
    io_context->name = PLUGIN_NAME;
    io_context->n_data_extents = 1;
    io_context->n_parity_extents = repl_count - 1;
    io_context->read.check_hash = PHO_CFG_GET_BOOL(cfg_lyt_raid1,
                                                   PHO_CFG_LYT_RAID1,
                                                   check_hash, true);
    if (io_context->read.check_hash) {
        io_context->nb_hashes = io_context->n_data_extents;
        io_context->hashes = xcalloc(io_context->nb_hashes,
                                     sizeof(*io_context->hashes));
    }

    rc = raid_decoder_init(decoder, &RAID1_MODULE_DESC,
                           &RAID1_READER_PROCESSOR_OPS, &RAID1_OPS);
    if (rc) {
        decoder->private_reader = NULL;
        free(io_context);
        return rc;
    }

    io_context->read.to_read = 0;
    decoder->object_size = 0;
    for (i = 0; i < decoder->src_layout->ext_count / repl_count; i++) {
        struct extent *extent = &decoder->src_layout->extents[i * repl_count];

        io_context->read.to_read += extent->size;
        decoder->object_size += extent->size;
    }

    /* Empty GET does not need any IO */
    if (decoder->object_size == 0)
        decoder->done = true;

    return 0;
}

static int layout_raid1_erase(struct pho_data_processor *eraser)
{
    struct raid_io_context *io_context;
    unsigned int repl_count;
    int rc;

    rc = raid1_repl_count(eraser->src_layout, &repl_count);
    if (rc)
        LOG_RETURN(rc, "Invalid replica count from layout to build raid1 "
                       "eraser");

    io_context = xcalloc(1, sizeof(*io_context));
    eraser->private_eraser = io_context;
    io_context->name = PLUGIN_NAME;
    io_context->n_data_extents = 1;
    io_context->n_parity_extents = repl_count - 1;

    rc = raid_eraser_init(eraser, &RAID1_MODULE_DESC,
                          &RAID1_ERASER_PROCESSOR_OPS, &RAID1_OPS);
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

int layout_raid1_locate(struct dss_handle *dss, struct layout_info *layout,
                        const char *focus_host, char **hostname,
                        int *nb_new_locks)
{
    unsigned int repl_count;
    int rc;

    rc = raid1_repl_count(layout, &repl_count);
    if (rc)
        LOG_RETURN(rc, "Invalid replica count from layout to locate");

    return raid_locate(dss, layout, 1, repl_count - 1, focus_host, hostname,
                       nb_new_locks);
}

static int layout_raid1_reconstruct(struct layout_info lyt,
                                    struct copy_info *copy)
{
    ssize_t extent_sizes = 0;
    ssize_t replica_size = 0;
    struct extent *extents;
    unsigned int repl_cnt;
    const char *buffer;
    int obj_size;
    int ext_cnt;
    int rc;
    int i;

    // Recover repl_count and obj_size
    rc = raid1_repl_count(&lyt, &repl_cnt);
    if (rc)
        LOG_RETURN(rc,
                   "Failed to get replica count for reconstruction of object '%s'",
                   lyt.oid);

    ext_cnt = lyt.ext_count;
    extents = lyt.extents;

    buffer = pho_attr_get(&lyt.layout_desc.mod_attrs, PHO_EA_OBJECT_SIZE_NAME);
    if (buffer == NULL)
        LOG_RETURN(-EINVAL,
                   "Failed to get object size for reconstruction of object '%s'",
                   lyt.oid);

    obj_size = str2int64(buffer);
    if (obj_size < 0)
        LOG_RETURN(-EINVAL,
                   "Invalid object size for reconstruction of object '%s': '%d'",
                   lyt.oid, obj_size);

    for (i = 0; i < ext_cnt; i++) {
        if (replica_size == extents[i].offset)
            replica_size += extents[i].size;

        extent_sizes += extents[i].size;
    }

    if (extent_sizes == repl_cnt * obj_size)
        copy->copy_status = PHO_COPY_STATUS_COMPLETE;
    else if (replica_size == obj_size)
        copy->copy_status = PHO_COPY_STATUS_READABLE;
    else
        copy->copy_status = PHO_COPY_STATUS_INCOMPLETE;

    return 0;
}

static int layout_raid1_get_specific_attrs(struct pho_io_descr *iod,
                                           struct io_adapter_module *ioa,
                                           struct extent *extent,
                                           struct pho_attrs *layout_md)
{
    const char *tmp_extent_index;
    const char *tmp_repl_count;
    struct pho_attrs md;
    int rc;

    md.attr_set = NULL;
    pho_attr_set(&md, PHO_EA_RAID1_EXTENT_INDEX_NAME, NULL);
    pho_attr_set(&md, PHO_EA_RAID1_REPL_COUNT_NAME, NULL);

    iod->iod_attrs = md;
    iod->iod_flags = PHO_IO_MD_ONLY;

    rc = ioa_open(ioa, NULL, iod, false);
    if (rc)
        goto end;

    tmp_repl_count = pho_attr_get(&md, PHO_EA_RAID1_REPL_COUNT_NAME);
    if (tmp_repl_count == NULL)
        LOG_GOTO(end, rc = -EINVAL,
                 "Failed to retrieve replica count of file '%s'",
                 iod->iod_loc->extent->address.buff);

    tmp_extent_index = pho_attr_get(&md, PHO_EA_RAID1_EXTENT_INDEX_NAME);
    if (tmp_extent_index == NULL)
        LOG_GOTO(end, rc = -EINVAL,
                 "Failed to retrieve extent index of file '%s'",
                 iod->iod_loc->extent->address.buff);

    extent->layout_idx = str2int64(tmp_extent_index);
    if (extent->layout_idx < 0)
        LOG_GOTO(end, rc = -EINVAL,
                 "Invalid extent index found on '%s': '%d'",
                 iod->iod_loc->extent->address.buff, extent->layout_idx);

    pho_attr_set(layout_md, PHO_EA_RAID1_REPL_COUNT_NAME, tmp_repl_count);

end:
    pho_attrs_free(&md);

    return rc;
}

static const struct pho_layout_module_ops LAYOUT_RAID1_OPS = {
    .encode = layout_raid1_encode,
    .decode = layout_raid1_decode,
    .erase = layout_raid1_erase,
    .locate = layout_raid1_locate,
    .get_specific_attrs = layout_raid1_get_specific_attrs,
    .reconstruct = layout_raid1_reconstruct,
};

/** Layout module registration entry point */
int pho_module_register(void *module, void *context)
{
    struct layout_module *self = (struct layout_module *) module;

    phobos_module_context_set(context);

    self->desc = RAID1_MODULE_DESC;
    self->ops = &LAYOUT_RAID1_OPS;

    return 0;
}
