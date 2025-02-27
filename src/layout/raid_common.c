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

#ifdef HAVE_CONFIG_H

#include "config.h"
#endif

#include <assert.h>
#include <errno.h>
#include <glib.h>
#include <limits.h>
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
#include "pho_io.h"
#include "pho_layout.h"
#include "pho_module_loader.h"
#include "pho_srl_common.h"
#include "pho_type_utils.h"
#include "pho_types.h"
#include "raid_common.h"

#define EXTENT_TAG_SIZE 128
#define PHO_ATTR_BACKUP_JSON_FLAGS (JSON_COMPACT | JSON_SORT_KEYS)
#define PHO_EA_UMD_NAME     "user_md"

size_t n_total_extents(struct raid_io_context *io_context)
{
    return io_context->n_data_extents + io_context->n_parity_extents;
}

static void free_extent_address_buff(void *void_extent)
{
    struct extent *extent = void_extent;

    free(extent->address.buff);
}

static int init_posix_iod(struct pho_data_processor *proc, int target_idx)
{
    struct raid_io_context *io_context =
            &((struct raid_io_context *) proc->private_processor)[target_idx];
    int rc;
    int fd;

    rc = get_io_adapter(PHO_FS_POSIX, &io_context->posix.iod_ioa);
    if (rc)
        return rc;

    /* We duplicate the file descriptor so that ioa_close doesn't close the file
     * descriptor of the Xfer. This file descriptor is managed by Python in the
     * CLI for example.
     */
    fd = dup(proc->xfer->xd_targets[target_idx].xt_fd);
    if (fd == -1)
        return -errno;

    return iod_from_fd(io_context->posix.iod_ioa, &io_context->posix, fd);
}

static void close_posix_iod(struct pho_data_processor *proc, int target_idx)
{
    struct raid_io_context *io_context =
            &((struct raid_io_context *) proc->private_processor)[target_idx];

    if (io_context->posix.iod_ioa)
        ioa_close(io_context->posix.iod_ioa, &io_context->posix);
}

int raid_encoder_init(struct pho_data_processor *encoder,
                      const struct module_desc *module,
                      const struct pho_proc_ops *enc_ops,
                      const struct raid_ops *raid_ops)
{
    struct raid_io_context *io_context = encoder->private_processor;
    size_t n_extents = n_total_extents(io_context);
    int rc;
    int i;

    assert(is_encoder(encoder));

    /* The ops field is set early to allow the caller to call the destroy
     * function on error.
     */
    encoder->ops = enc_ops;

    for (i = 0; i < encoder->xfer->xd_ntargets; i++) {
        io_context =
            &((struct raid_io_context *) encoder->private_processor)[i];
        n_extents = n_total_extents(io_context);

        if (encoder->xfer->xd_targets[i].xt_fd < 0)
            LOG_RETURN(-EBADF,
                       "raid: invalid xfer file descriptor in '%s' encoder",
                       encoder->xfer->xd_targets[i].xt_objid);

        /* Do not copy mod_attrs as it may have been modified by the caller
         * before this function is called.
         */
        encoder->layout[i].layout_desc.mod_name = module->mod_name;
        encoder->layout[i].layout_desc.mod_minor = module->mod_minor;
        encoder->layout[i].layout_desc.mod_major = module->mod_major;
        io_context->ops = raid_ops;

        /* Build the extent attributes from the object ID and the user provided
         * attributes. This information will be attached to backend objects for
         * "self-description"/"rebuild" purpose.
         */
        io_context->write.user_md = g_string_new(NULL);
        rc = pho_attrs_to_json(&encoder->xfer->xd_targets[i].xt_attrs,
                               io_context->write.user_md,
                               PHO_ATTR_BACKUP_JSON_FLAGS);
        if (rc) {
            g_string_free(io_context->write.user_md, TRUE);
            LOG_RETURN(rc, "Failed to convert attributes to JSON");
        }

        rc = init_posix_iod(encoder, i);
        if (rc)
            /* io_context is free'd by layout_destroy */
            return rc;

        io_context->write.written_extents = g_array_new(FALSE, TRUE,
                                                        sizeof(struct extent));
        g_array_set_clear_func(io_context->write.written_extents,
                               free_extent_address_buff);

        io_context->write.to_release_media =
            g_hash_table_new_full(g_pho_id_hash, g_pho_id_equal, free, free);

        io_context->iods = xcalloc(n_extents, sizeof(*io_context->iods));
        io_context->write.extents = xcalloc(n_extents,
                                            sizeof(*io_context->write.extents));
        io_context->buffers = xmalloc(n_extents * sizeof(*io_context->buffers));
    }

    return 0;
}

int raid_decoder_init(struct pho_data_processor *decoder,
                      const struct module_desc *module,
                      const struct pho_proc_ops *enc_ops,
                      const struct raid_ops *raid_ops)
{
    struct raid_io_context *io_context = decoder->private_processor;
    size_t n_extents = n_total_extents(io_context);
    int rc;

    if (decoder->xfer->xd_targets->xt_fd < 0)
        LOG_RETURN(rc = -EBADF, "Invalid decoder xfer file descriptor");

    assert(is_decoder(decoder));

    decoder->ops = enc_ops;
    io_context->ops = raid_ops;

    io_context->iods = xcalloc(io_context->n_data_extents,
                               sizeof(*io_context->iods));
    io_context->read.extents = xcalloc(io_context->n_data_extents,
                                       sizeof(*io_context->read.extents));
    io_context->buffers = xmalloc(n_extents * sizeof(*io_context->buffers));

    rc = init_posix_iod(decoder, 0);
    if (rc) {
        free(io_context);
        return rc;
    }

    return 0;
}

int raid_eraser_init(struct pho_data_processor *eraser,
                     const struct module_desc *module,
                     const struct pho_proc_ops *enc_ops,
                     const struct raid_ops *raid_ops)
{
    struct raid_io_context *io_context = eraser->private_processor;
    size_t n_extents = n_total_extents(io_context);

    assert(is_eraser(eraser));
    eraser->ops = enc_ops;
    io_context->ops = raid_ops;

    io_context->iods = xcalloc(n_extents, sizeof(*io_context->iods));
    io_context->delete.extents = xcalloc(n_extents,
                                         sizeof(*io_context->delete.extents));

    return 0;
}

void raid_processor_destroy(struct pho_data_processor *proc)
{
    struct raid_io_context *io_context;
    int i, j;

    for (i = 0; i < proc->xfer->xd_ntargets; i++) {
        io_context = &((struct raid_io_context *) proc->private_processor)[i];

        if (!io_context)
            return;

        close_posix_iod(proc, i);

        if (is_encoder(proc)) {
            if (io_context->write.written_extents)
                g_array_free(io_context->write.written_extents, TRUE);

            if (io_context->write.to_release_media)
                g_hash_table_destroy(io_context->write.to_release_media);

            io_context->write.written_extents = NULL;
            io_context->write.to_release_media = NULL;
            free(io_context->write.extents);
            free(io_context->iods);
            g_string_free(io_context->write.user_md, TRUE);

        } else if (is_decoder(proc)) {
            free(io_context->read.extents);
            free(io_context->iods);
        } else {
            free(io_context->delete.extents);
            free(io_context->iods);
        }

        for (j = 0; j < io_context->nb_hashes; j++)
            extent_hash_fini(&io_context->hashes[j]);

        free(io_context->hashes);
        free(io_context->buffers);
    }

    free(proc->private_processor);
    proc->private_processor = NULL;
}

static size_t remaining_io_size(struct pho_data_processor *proc, int idx)
{
    const struct raid_io_context *io_context =
            &((struct raid_io_context *) proc->private_processor)[idx];

    if (is_decoder(proc))
        return io_context->read.to_read;
    else if (is_encoder(proc))
        return io_context->write.to_write;
    else
        return io_context->delete.to_delete;
}

static bool no_more_alloc(struct pho_data_processor *proc, int idx)
{
    const struct raid_io_context *io_context =
        &((struct raid_io_context *) proc->private_processor)[idx];

    if (proc->done)
        return true;

    /* still some I/O to do */
    if (remaining_io_size(proc, idx) > 0)
        return false;

    /* decoder with no more to read */
    if (is_decoder(proc) || is_eraser(proc))
        return true;

    /* encoder with nothing more to write and at least one written extent */
    if (io_context->write.written_extents->len > 0)
        return true;

    /* encoder with no more to write but needing to write at least one extent */
    return false;
}

static char *raid_proc_root_path(struct pho_data_processor *proc,
                                 size_t i, int target_idx)
{
    struct raid_io_context *io_context =
        &((struct raid_io_context *) proc->private_processor)[target_idx];

    if (is_encoder(proc))
        return io_context->write.resp->media[i]->root_path;
    else if (is_decoder(proc))
        return io_context->read.resp->media[i]->root_path;
    else
        return io_context->delete.resp->media[i]->root_path;
}

static enum address_type raid_proc_addr_type(struct pho_data_processor *proc,
                                             size_t i, int target_idx)
{
    struct raid_io_context *io_context =
        &((struct raid_io_context *) proc->private_processor)[target_idx];

    if (is_encoder(proc))
        return io_context->write.resp->media[i]->addr_type;
    else if (is_decoder(proc))
        return io_context->read.resp->media[i]->addr_type;
    else
        return io_context->delete.resp->media[i]->addr_type;
}

static struct extent *raid_proc_extent(struct pho_data_processor *proc,
                                       size_t i, int target_idx)
{
    struct raid_io_context *io_context =
        &((struct raid_io_context *) proc->private_processor)[target_idx];

    if (is_encoder(proc))
        return &io_context->write.extents[i];
    else if (is_decoder(proc))
        return io_context->read.extents[i];
    else
        return io_context->delete.extents[i];
}

static struct pho_io_descr *raid_proc_iod(struct pho_data_processor *proc,
                                          size_t i, int target_idx)
{
    struct raid_io_context *io_context =
        &((struct raid_io_context *) proc->private_processor)[target_idx];

    return &io_context->iods[i];
}

struct pho_ext_loc make_ext_location(struct pho_data_processor *proc, size_t i,
                                     int target_idx)
{
    return (struct pho_ext_loc) {
        .root_path = raid_proc_root_path(proc, i, target_idx),
        .addr_type = raid_proc_addr_type(proc, i, target_idx),
        .extent = raid_proc_extent(proc, i, target_idx),
    };
}

static int raid_io_context_open(struct raid_io_context *io_context,
                                struct pho_data_processor *proc,
                                size_t count, int target_idx)
{
    size_t i;
    int rc;

    for (i = 0; i < count; ++i) {
        struct pho_ext_loc ext_location = make_ext_location(proc, i,
                                                            target_idx);
        struct pho_io_descr *iod = raid_proc_iod(proc, i, target_idx);

        iod->iod_size = 0;
        iod->iod_loc = &ext_location;
        if (!is_eraser(proc)) {
            rc = ioa_open(iod->iod_ioa,
                          proc->xfer->xd_targets[target_idx].xt_objid, iod,
                          is_encoder(proc));
            if (rc)
                LOG_GOTO(out_close, rc,
                         "raid: unable to open extent for '%s' on '%s':'%s'",
                         proc->xfer->xd_targets[target_idx].xt_objid,
                         ext_location.extent->media.library,
                         ext_location.extent->media.name);
        }

        pho_debug("I/O size for replicate %lu: %zu", i, proc->io_block_size);
    }

    return 0;

out_close:
    while (i > 0) {
        struct pho_io_descr *iod;

        i--;
        iod = raid_proc_iod(proc, i, target_idx);

        ioa_close(iod->iod_ioa, iod);
    }

    return rc;
}

static int raid_build_write_allocation_req(struct pho_data_processor *proc,
                                           pho_req_t *req)
{
    struct raid_io_context *io_contexts = proc->private_processor;
    /* Use the first io_context as they have all the same number of extents.
     * I/O contexts are defined with the put params.
     */
    size_t n_extents = n_total_extents(&io_contexts[0]);
    int n_data_extents = io_contexts[0].n_data_extents;
    size_t size_per_extent = 0;
    size_t fs_block_size = 0;
    size_t nb_block = 0;
    size_t size = 0;
    size_t *n_tags;
    int rc = 0;
    int i, j;

    ENTRY;

    n_tags = xcalloc(n_extents, sizeof(*n_tags));

    for (i = 0; i < n_extents; ++i)
        n_tags[i] = proc->xfer->xd_params.put.tags.count;

    pho_srl_request_write_alloc(req, n_extents, n_tags);
    free(n_tags);

    rc = get_cfg_fs_block_size(proc->xfer->xd_params.put.family,
                               &fs_block_size);
    if (rc)
        return rc;

    for (i = 0; i < proc->xfer->xd_ntargets; i++) {
        /* Add an overhead to the total size to write to anticipate the size
         * taken by the xattrs and directory.
         *
         * The phys_spc_used is:
         * ceil(size / fs_block_size) * fs_block_size + 3 * fs_block_size
         */
        if (fs_block_size > 0) {
            size_per_extent =
                (io_contexts[i].write.to_write + n_data_extents - 1) /
                 n_data_extents;
            nb_block = (size_per_extent + (fs_block_size - 1)) / fs_block_size;
            size += nb_block * fs_block_size + 3 * fs_block_size;

        /* If the block size of the file system is not defined in the conf,
         * the size to alloc is equal to the size to write.
         */
        } else {
            size += (io_contexts[i].write.to_write + n_data_extents - 1) /
                     n_data_extents;
        }
    }

    for (i = 0; i < n_extents; ++i) {
        req->walloc->media[i]->size = size;

        for (j = 0; j < proc->xfer->xd_params.put.tags.count; ++j)
            req->walloc->media[i]->tags[j] =
                xstrdup(proc->xfer->xd_params.put.tags.strings[j]);
    }

    req->walloc->no_split = proc->xfer->xd_params.put.no_split;
    return 0;
}

static size_t split_first_extent_index(struct pho_data_processor *proc)
{
    struct raid_io_context *io_context = proc->private_processor;

    return io_context->current_split * n_total_extents(io_context);
}

/** Generate the next read allocation request for this decoder */
static void raid_build_read_allocation_req(struct pho_data_processor *decoder,
                                           pho_req_t *req)
{
    struct raid_io_context *io_context = decoder->private_processor;
    size_t n_extents = n_total_extents(io_context);
    int i;

    ENTRY;
    pho_srl_request_read_alloc(req, n_extents);

    req->ralloc->n_required = io_context->n_data_extents;
    req->ralloc->operation = PHO_READ_TARGET_ALLOC_OP_READ;

    for (i = 0; i < n_extents; ++i) {
        unsigned int ext_idx;

        ext_idx = split_first_extent_index(decoder) + i;

        req->ralloc->med_ids[i]->family =
            decoder->layout->extents[ext_idx].media.family;
        req->ralloc->med_ids[i]->name =
            xstrdup(decoder->layout->extents[ext_idx].media.name);
        req->ralloc->med_ids[i]->library =
            xstrdup(decoder->layout->extents[ext_idx].media.library);
    }
}

static void raid_build_delete_allocation_req(struct pho_data_processor *eraser,
                                             pho_req_t *req)
{
    raid_build_read_allocation_req(eraser, req);
    req->ralloc->n_required = req->ralloc->n_med_ids;
    req->ralloc->operation = PHO_READ_TARGET_ALLOC_OP_DELETE;
}

static void raid_io_context_set_extent_info(struct raid_io_context *io_context,
                                            pho_resp_write_elt_t **medium,
                                            int extent_idx, size_t extent_size,
                                            size_t offset)
{
    struct extent *extents = io_context->write.extents;
    int i;

    for (i = 0; i < n_total_extents(io_context); i++) {
        extents[i].uuid = generate_uuid();
        extents[i].layout_idx = extent_idx + i;
        extents[i].size = extent_size;
        extents[i].offset = offset;
        extents[i].media.family = (enum rsc_family)medium[i]->med_id->family;

        pho_id_name_set(&extents[i].media, medium[i]->med_id->name,
                        medium[i]->med_id->library);
    }
}

static void get_io_size(struct pho_io_descr *iod, size_t *io_size)
{
    if (*io_size != 0)
        /* io_size already specified in the configuration */
        return;

    *io_size = ioa_preferred_io_size(iod->iod_ioa, iod);
    if (*io_size <= 0)
        /* fallback: get the system page size */
        *io_size = sysconf(_SC_PAGESIZE);
}

static void raid_io_context_setmd(struct raid_io_context *io_context,
                                  char *xd_objid, const GString *str)
{
     struct pho_io_descr *iods = io_context->iods;
     int i;

     for (i = 0; i < n_total_extents(io_context); ++i) {
        if (!gstring_empty(str))
            pho_attr_set(&iods[i].iod_attrs, PHO_EA_UMD_NAME, str->str);
    }
}

static size_t gcd(size_t a, size_t b)
{
    size_t tmp;

    while (b != 0) {
        tmp = a % b;

        a = b;
        b = tmp;
    }

    return a;
}

static size_t lcm(size_t a, size_t b)
{
    return a / gcd(a, b) * b;
}

/**
 * Make sure the I/O size is optimal for all I/O descriptors by using the LCM
 * of all the I/O descriptor optimal sizes. If the I/O size is already set in
 * the config, simply return it.
 */
static size_t best_io_size(struct pho_data_processor *proc, int target_idx)
{
    struct raid_io_context *io_context =
        &((struct raid_io_context *) proc->private_processor)[target_idx];
    size_t nb_extents;
    size_t best = 0;
    int i;

    if (proc->io_block_size > 0)
        /* This serves 2 purposes. First, if the I/O size is setup in the
         * configuration, we don't want to overwrite it. Second, we only want to
         * find the optimal I/O size on the first split and reuse the same on
         * the subsequent splits. It seems overkill to update the I/O block size
         * for each split.
         */
        return proc->io_block_size;

    if (is_decoder(proc))
        nb_extents = io_context->n_data_extents;
    else
        nb_extents = n_total_extents(io_context);

    get_io_size(&io_context->posix, &best);

    for (i = 0; i < nb_extents; i++) {
        size_t io_size = 0;

        get_io_size(raid_proc_iod(proc, i, target_idx), &io_size);

        best = lcm(best, io_size);
    }

    return best;
}

static int write_split_setup(struct pho_data_processor *encoder,
                             pho_resp_write_t *wresp,
                             size_t split_size, int target_idx)
{
    struct raid_io_context *io_context =
        &((struct raid_io_context *) encoder->private_processor)[target_idx];
    struct pho_io_descr *iods;
    size_t left_to_write;
    size_t object_size;
    size_t n_extents;
    int rc;
    int i;

    object_size = encoder->xfer->xd_targets[target_idx].xt_size;
    left_to_write = io_context->write.to_write;

    n_extents = n_total_extents(io_context);
    iods = io_context->iods;

    for (i = 0; i < n_extents; ++i) {
        rc = get_io_adapter((enum fs_type)wresp->media[i]->fs_type,
                            &iods[i].iod_ioa);
        if (rc)
            LOG_RETURN(rc, "Unable to get io_adapter in raid encoder");

        iods[i].iod_size = 0;
        iods[i].iod_flags = PHO_IO_REPLACE | PHO_IO_NO_REUSE;
    }

    raid_io_context_setmd(io_context,
                          encoder->xfer->xd_targets[target_idx].xt_objid,
                          io_context->write.user_md);
    raid_io_context_set_extent_info(io_context, wresp->media,
                                    io_context->current_split * n_extents,
                                    split_size,
                                    object_size - left_to_write);
    rc = raid_io_context_open(io_context, encoder, n_extents, target_idx);
    if (rc)
        return rc;

    encoder->io_block_size = best_io_size(encoder, target_idx);
    if (split_size < encoder->io_block_size)
        encoder->io_block_size = split_size;

    for (i = 0; i < n_extents; i++)
        pho_buff_alloc(&io_context->buffers[i], encoder->io_block_size);

    for (i = 0; i < io_context->nb_hashes; i++) {
        rc = extent_hash_reset(&io_context->hashes[i]);
        if (rc)
            return rc;
    }

    return 0;
}

static void add_new_to_release_media(struct raid_io_context *io_context,
                                     const struct pho_id *media_id)
{
    gboolean was_not_in;
    size_t *ref_count;

    ref_count = xmalloc(sizeof(*ref_count));
    *ref_count = 1;

    was_not_in = g_hash_table_insert(io_context->write.to_release_media,
                                     pho_id_dup(media_id), ref_count);
    assert(was_not_in);
}

static void raid_io_add_written_extent(struct raid_io_context *io_context,
                                       struct extent *extent)
{
    size_t *to_release_refcount;

    /* add extent to written ones */
    g_array_append_val(io_context->write.written_extents, *extent);

    /* add medium to be released */
    to_release_refcount =
        g_hash_table_lookup(io_context->write.to_release_media, &extent->media);

    if (to_release_refcount) /* existing media_id to release */
        ++(*to_release_refcount);
    else                     /* new media_id to release */
        add_new_to_release_media(io_context, &extent->media);

    /* Since we make a copy of the extent, reset this one to avoid reusing
     * internal pointers somewhere else in the code.
     */
    memset(extent, 0, sizeof(*extent));
}

static int write_split_fini(struct pho_data_processor *encoder, int io_rc,
                            pho_req_release_t *release,
                            size_t split_size, int target_idx)
{
    struct raid_io_context *io_context =
        &((struct raid_io_context *) encoder->private_processor)[target_idx];
    size_t n_extents = n_total_extents(io_context);
    struct object_metadata object_md = {
        .object_attrs = encoder->xfer->xd_targets[target_idx].xt_attrs,
        .object_size = encoder->xfer->xd_targets[target_idx].xt_size,
        .object_version = encoder->xfer->xd_targets[target_idx].xt_version,
        .layout_name = io_context->name,
        .object_uuid = encoder->xfer->xd_targets[target_idx].xt_objuuid,
        .copy_name = encoder->layout[target_idx].copy_name,
    };
    size_t total_written = 0;
    int rc = 0;
    size_t i;

    for (i = 0; i < n_extents; i++) {
        struct pho_ext_loc ext_location = make_ext_location(encoder, i,
                                                            target_idx);
        struct pho_io_descr *iod = raid_proc_iod(encoder, i, target_idx);
        int rc2;

        iod->iod_loc = &ext_location;
        rc2 = set_object_md(iod->iod_ioa, iod, &object_md);
        rc = rc ? : rc2;

        rc2 = ioa_close(iod->iod_ioa, iod);
        rc = rc ? : rc2;

        release->media[i]->rc = io_rc;
        release->media[i]->size_written += iod->iod_size;
        release->media[i]->nb_extents_written += 1;
        release->media[i]->grouping =
            xstrdup_safe(encoder->xfer->xd_params.put.grouping);
        ext_location.extent->size = iod->iod_size;
        if (i < io_context->n_data_extents)
            /* We need to remove the size written from to_write. But to_write
             * doesn't take the parity blocks into account. This assumes that
             * all data blocks are at the beginning of the list.
             */
            total_written += iod->iod_size;
    }

    if (!io_rc) {
        io_context->write.to_write -= total_written;

        for (i = 0; i < n_extents; i++)
            raid_io_add_written_extent(io_context,
                                       &io_context->write.extents[i]);

        io_context->current_split++;
    }

    for (i = 0; i < n_extents; i++) {
        pho_attrs_free(&raid_proc_iod(encoder, i, target_idx)->iod_attrs);
        pho_buff_free(&io_context->buffers[i]);
    }

    return rc;
}

static ssize_t extent_index(struct layout_info *layout,
                            const pho_rsc_id_t *medium)
{
    size_t i;

    for (i = 0; i < layout->ext_count; i++) {
        if (!strcmp(layout->extents[i].media.name, medium->name) &&
            !strcmp(layout->extents[i].media.library, medium->library))
            return i;
    }

    return -1;
}

static int layout_index_cmp(const void *_lhs, const void *_rhs)
{
    const struct extent **lhs = (void *)_lhs;
    const struct extent **rhs = (void *)_rhs;

    return (*lhs)->layout_idx - (*rhs)->layout_idx;
}

struct extent_list {
    struct extent **extents;
    size_t count;
};

static ssize_t find_extent(struct extent_list *list,
                           const pho_rsc_id_t *medium)
{
    ssize_t i;

    for (i = 0; i < list->count; i++) {
        if (!strcmp(list->extents[i]->media.name, medium->name) &&
            !strcmp(list->extents[i]->media.library, medium->library))
            return i;
    }

    return -1;
}

/* Sort the media response list so that they are aligned with the extent list
 */
static int read_media_cmp(const void *_lhs, const void *_rhs, void *arg)
{
    const pho_resp_read_elt_t **lhs = (void *)_lhs;
    const pho_resp_read_elt_t **rhs = (void *)_rhs;
    struct extent_list *list = arg;
    ssize_t lhs_index;
    ssize_t rhs_index;

    lhs_index = find_extent(list, (*lhs)->med_id);
    rhs_index = find_extent(list, (*rhs)->med_id);

    if (lhs_index == -1 || rhs_index == -1) {
        /* the extent list is built from the media list. They must contain the
         * same elements. Otherwise, this is a bug.
         */
        pho_error(0,
                  "Unexpected medium in response ('%s':'%s' at index %ld, "
                  "'%s':'%s' at index %ld), abort.",
                  (*lhs)->med_id->library, (*lhs)->med_id->name, lhs_index,
                  (*rhs)->med_id->library, (*rhs)->med_id->name, rhs_index);
        abort();
    }

    return lhs_index - rhs_index;
}

static void sort_extents_by_layout_index(pho_resp_read_t *resp,
                                         struct extent **extents,
                                         size_t n_extents)
{
    struct extent_list list = {
        .extents = extents,
        .count = n_extents,
    };

    qsort(extents, n_extents, sizeof(*extents), layout_index_cmp);
    qsort_r(resp->media, resp->n_media, sizeof(*resp->media),
            read_media_cmp, &list);
}

static int read_split_setup(struct pho_data_processor *decoder,
                            pho_resp_read_elt_t **medium,
                            size_t n_media,
                            size_t *split_size)
{
    struct raid_io_context *io_context = decoder->private_processor;
    size_t n_extents = n_total_extents(io_context);
    size_t i;
    int rc;

    ENTRY;

    if (n_media != io_context->n_data_extents)
        LOG_RETURN(-EINVAL, "Invalid number of media return by phobosd. "
                            "Expected %lu, got %lu",
                   n_extents, n_media);

    *split_size = 0;
    for (i = 0; i < io_context->n_data_extents; i++) {
        ssize_t ext_index;

        rc = get_io_adapter((enum fs_type) medium[i]->fs_type,
                            &io_context->iods[i].iod_ioa);
        if (rc)
            return rc;

        ext_index = extent_index(decoder->layout, medium[i]->med_id);
        if (ext_index == -1)
            LOG_RETURN(-ENOMEDIUM,
                       "Did not find medium '%s':'%s' in layout of '%s'",
                       medium[i]->med_id->library,
                       medium[i]->med_id->name,
                       decoder->xfer->xd_targets->xt_objid);

        io_context->read.extents[i] = &decoder->layout->extents[ext_index];
        if (io_context->read.extents[i]->size > *split_size)
            *split_size = io_context->read.extents[i]->size;
    }
    sort_extents_by_layout_index(io_context->read.resp,
                                 io_context->read.extents,
                                 io_context->n_data_extents);

    rc = raid_io_context_open(io_context, decoder, io_context->n_data_extents,
                              0);
    if (rc)
        return rc;

    rc = io_context->ops->get_block_size(decoder, &decoder->io_block_size);
    if (rc)
        return rc;

    if (decoder->io_block_size == 0) {
        /* If the layout does not have a prefered I/O size, set it to the best
         * one. For example, RAID1 can use any I/O size.
         */
        decoder->io_block_size = best_io_size(decoder, 0);
        if (*split_size < decoder->io_block_size)
            decoder->io_block_size = *split_size;

        pho_debug("%s: setting I/O size to %lu", io_context->name,
                  decoder->io_block_size);
    }

    for (i = 0; i < n_extents; i++)
        pho_buff_alloc(&io_context->buffers[i], decoder->io_block_size);

    for (i = 0; i < io_context->n_data_extents; i++) {
        ssize_t size;

        size = ioa_size(io_context->iods[i].iod_ioa, &io_context->iods[i]);
        /* If not supported, skip the check */
        if (size == -ENOTSUP)
            break;
        if (size < 0)
            return size;

        if (size != io_context->read.extents[i]->size)
            LOG_RETURN(-EINVAL,
                       "Extent size mismatch: the size of '%s' in the "
                       "DSS differ",
                       io_context->read.extents[i]->address.buff);
    }

    if (io_context->read.check_hash) {
        for (i = 0; i < io_context->nb_hashes; i++) {
            rc = extent_hash_init(&io_context->hashes[i],
                                  io_context->read.extents[i]->with_md5,
                                  io_context->read.extents[i]->with_xxh128);
            if (rc)
                return rc;

            rc = extent_hash_reset(&io_context->hashes[i]);
            if (rc)
                return rc;
        }
    }

    return 0;
}

static int delete_split_setup(struct pho_data_processor *eraser,
                              pho_resp_read_elt_t **medium, size_t n_media,
                              size_t *split_size)
{
    struct raid_io_context *io_context = eraser->private_processor;
    size_t n_extents = n_total_extents(io_context);
    size_t i;
    int rc;

    ENTRY;

    *split_size = 0;

    if (n_media != n_extents)
        LOG_RETURN(-EINVAL, "Invalid number of media return by phobosd. "
                            "Expected %lu, got %lu",
                   n_extents, n_media);

    for (i = 0; i < n_extents; i++) {
        ssize_t ext_index;

        rc = get_io_adapter((enum fs_type)medium[i]->fs_type,
                            &io_context->iods[i].iod_ioa);
        if (rc)
            return rc;

        ext_index = extent_index(eraser->layout, medium[i]->med_id);
        if (ext_index == -1)
            LOG_RETURN(-ENOMEDIUM,
                       "Did not find medium '%s':'%s' in layout of '%s'",
                       medium[i]->med_id->library,
                       medium[i]->med_id->name,
                       eraser->xfer->xd_targets->xt_objid);

        io_context->delete.extents[i] = &eraser->layout->extents[ext_index];
        if (io_context->delete.extents[i]->size > *split_size)
            *split_size = io_context->delete.extents[i]->size;
    }
    sort_extents_by_layout_index(io_context->delete.resp,
                                 io_context->delete.extents,
                                 n_extents);

    rc = raid_io_context_open(io_context, eraser, n_extents, 0);
    if (rc)
        return rc;

    return 0;
}

int raid_delete_split(struct pho_data_processor *eraser)
{
    struct raid_io_context *io_context = eraser->private_processor;
    size_t n_extents = n_total_extents(io_context);
    struct pho_io_descr *iod;
    struct pho_ext_loc loc;
    int rc = 0;
    int i;

    for (i = 0; i < n_extents; ++i) {
        iod = &io_context->iods[i];
        loc = make_ext_location(eraser, i, 0);
        iod->iod_loc = &loc;

        rc = ioa_del(iod->iod_ioa, iod);
        if (rc)
            break;

        io_context->delete.to_delete--;
    }

    return rc;
}

static void pho_id_from_rsc_id(const pho_rsc_id_t *medium, struct pho_id *dst)
{
    dst->family = medium->family;
    pho_id_name_set(dst, medium->name, medium->library);
}

static int mark_written_medium_released(struct raid_io_context *io_context,
                                        const pho_rsc_id_t *medium)
{
    size_t *to_release_refcount;
    struct pho_id copy;

    pho_id_from_rsc_id(medium, &copy);
    to_release_refcount =
        g_hash_table_lookup(io_context->write.to_release_media, &copy);

    if (to_release_refcount == NULL)
        LOG_RETURN(-EINVAL,
                   "Got a release response for medium '%s':'%s' but it is was "
                   "not in the release list",
                   medium->library, medium->name);

    /* media id with refcount of zero must be removed from the hash table */
    assert(*to_release_refcount > 0);

    /* one medium was released */
    io_context->write.n_released_media++;

    /* only one release was ongoing for this medium: remove from the table */
    if (*to_release_refcount == 1) {
        gboolean was_in_table;

        was_in_table = g_hash_table_remove(io_context->write.to_release_media,
                                           &copy);
        assert(was_in_table);
        return 0;
    }

    /* used for several extents: only decrement once */
    --(*to_release_refcount);
    return 0;
}

static int raid_write_handle_release_resp(struct pho_data_processor *encoder,
                                          pho_resp_release_t *rel_resp)
{
    struct raid_io_context *io_context;
    int rc = 0;
    int i, j;

    for (i = 0; i < rel_resp->n_med_ids; i++) {
        int rc2;

        pho_debug("Marking medium '%s':'%s' as released",
                  rel_resp->med_ids[i]->name,
                  rel_resp->med_ids[i]->library);
        for (j = 0; j < encoder->xfer->xd_ntargets; j++) {
            /* If the media_id is unexpected, -EINVAL will be returned */
            io_context =
                &((struct raid_io_context *) encoder->private_processor)[j];
            rc2 = mark_written_medium_released(io_context,
                                               rel_resp->med_ids[i]);
            if (rc2 && !rc)
                rc = rc2;
        }
    }

    /*
     * If we wrote everything and all the releases have been received, mark the
     * encoder as done.
     */
    for (i = 0; i < encoder->xfer->xd_ntargets; i++) {
        io_context =
            &((struct raid_io_context *) encoder->private_processor)[i];
        if (io_context->write.to_write == 0 && /* no more data to write */
            /* at least one extent is created, special test for null size put */
            io_context->write.written_extents->len > 0 &&
            /* we got releases of all extents */
            io_context->write.written_extents->len ==
            io_context->write.n_released_media) {

            /* Fill the layout with the extents */
            encoder->layout[i].ext_count =
                io_context->write.written_extents->len;
            encoder->layout[i].extents = (struct extent *)
            g_array_free(io_context->write.written_extents, FALSE);
            io_context->write.written_extents = NULL;

            io_context->write.n_released_media = 0;

            for (j = 0; j < encoder->layout->ext_count; ++j)
                encoder->layout[i].extents[j].state = PHO_EXT_ST_SYNC;
        } else {
            break;
        }
    }

    /* Switch to DONE state */
    if (i == encoder->xfer->xd_ntargets) {
        encoder->done = true;
        return 0;
    }

    return rc;
}

static bool need_to_sync(pho_req_release_t *release, struct timespec start,
                         pho_resp_t *resp)
{
    unsigned long resp_sync_wsize_kb = resp->walloc->threshold->sync_wsize_kb;
    unsigned int resp_sync_nb_req = resp->walloc->threshold->sync_nb_req;
    struct timespec resp_sync_time = {
        .tv_sec = resp->walloc->threshold->sync_time_sec,
        .tv_nsec = resp->walloc->threshold->sync_time_nsec,
    };
    int i;

    for (i = 0; i < release->n_media; i++) {
        if (release->media[i]->size_written >= resp_sync_wsize_kb ||
            release->media[i]->nb_extents_written >= resp_sync_nb_req ||
            is_past(add_timespec(&start, &resp_sync_time))) {
            return true;
        }
    }

    return false;
}

static pho_resp_t *copy_response_write_alloc(pho_resp_t *resp)
{
    pho_resp_write_t *wresp;
    pho_resp_t *resp_cpy;
    int i;

    resp_cpy = xmalloc(sizeof(*resp));
    pho_srl_response_write_alloc(resp_cpy, resp->walloc->n_media);

    resp_cpy->walloc->threshold = xmalloc(sizeof(*resp_cpy->walloc->threshold));
    pho_sync_threshold__init(resp_cpy->walloc->threshold);
    wresp = resp_cpy->walloc;

    wresp->threshold->sync_nb_req    = resp->walloc->threshold->sync_nb_req;
    wresp->threshold->sync_wsize_kb  = resp->walloc->threshold->sync_wsize_kb;
    wresp->threshold->sync_time_sec  = resp->walloc->threshold->sync_time_sec;
    wresp->threshold->sync_time_nsec = resp->walloc->threshold->sync_time_nsec;

    for (i = 0; i < resp->walloc->n_media; i++) {
        rsc_id_cpy(wresp->media[i]->med_id, resp->walloc->media[i]->med_id);
        wresp->media[i]->avail_size = resp->walloc->media[i]->avail_size;
        wresp->media[i]->root_path = xstrdup(resp->walloc->media[i]->root_path);
        wresp->media[i]->fs_type = resp->walloc->media[i]->fs_type;
        wresp->media[i]->addr_type = resp->walloc->media[i]->addr_type;
    }

    return resp_cpy;
}

static int raid_handle_write_resp(struct pho_data_processor *encoder,
                                  pho_resp_t *resp_new, pho_req_t **reqs,
                                  size_t *n_reqs)
{
    struct raid_io_context *io_context;
    static int nb_written;
    struct timespec start;
    size_t split_size;
    pho_resp_t *resp;
    int rc = 0;
    int i, j;

    if (resp_new == NULL)
        resp = encoder->last_resp;
    else
        resp = resp_new;
    /*
     * Build release req matching this allocation response, this release
     * request will be emitted after the IO has been performed. Any
     * allocated medium must be released.
     */
    pho_srl_request_release_alloc(*reqs + *n_reqs, resp->walloc->n_media);

    for (i = 0; i < resp->walloc->n_media; ++i) {
        rsc_id_cpy((*reqs)[*n_reqs].release->media[i]->med_id,
                   resp->walloc->media[i]->med_id);
        (*reqs)[*n_reqs].release->media[i]->to_sync = true;
        (*reqs)[*n_reqs].release->media[i]->size_written = 0;
        (*reqs)[*n_reqs].release->media[i]->nb_extents_written = 0;
    }

    if (encoder->xfer->xd_params.put.no_split) {
        rc = clock_gettime(CLOCK_REALTIME, &start);
        if (rc)
            LOG_RETURN(-errno, "clock_gettime: unable to get CLOCK_REALTIME");
    }

    for (i = nb_written; i < encoder->xfer->xd_ntargets; i++) {
        io_context =
            &((struct raid_io_context *) encoder->private_processor)[i];

        split_size =
            (io_context->write.to_write + io_context->n_data_extents - 1) /
                io_context->n_data_extents;
        for (j = 0; j < resp->walloc->n_media; j++) {
            if (resp->walloc->media[j]->avail_size < split_size)
                split_size = resp->walloc->media[j]->avail_size;
        }

        io_context->write.resp = resp->walloc;
        rc = write_split_setup(encoder, resp->walloc, split_size, i);
        if (rc)
            goto skip_io;

        /* Perform IO and populate release request with the outcome */
        rc = io_context->ops->write_split(encoder, split_size, i);

skip_io:
        rc = write_split_fini(encoder, rc, (*reqs)[*n_reqs].release, split_size,
                              i);

        io_context->requested_alloc = false;
        /* Only increment xd_nwritten with a no-split, otherwise there is
         * always one target to write.
         */
        if (encoder->xfer->xd_params.put.no_split)
            nb_written++;

        /* If the transfer has exceeded the threshold for the sync, perform a
         * partial release (only a sync).
         * Also check if it's not the last encoders, otherwise the release
         * request is enough.
         */
        if (encoder->xfer->xd_params.put.no_split &&
            need_to_sync((*reqs)[*n_reqs].release, start, resp) &&
            i < encoder->xfer->xd_ntargets - 1) {

            (*reqs)[*n_reqs].release->partial = true;
            if (encoder->last_resp == NULL)
                encoder->last_resp = copy_response_write_alloc(resp);
            break;
        }
    }

    (*n_reqs)++;

    return rc;
}

static int raid_handle_read_resp(struct pho_data_processor *decoder,
                                 pho_resp_t *resp, pho_req_t **reqs,
                                 size_t *n_reqs)
{
    struct raid_io_context *io_context = decoder->private_processor;
    size_t split_size;
    int rc;
    int i;

    ENTRY;

    pho_srl_request_release_alloc(*reqs + *n_reqs, resp->ralloc->n_media);

    for (i = 0; i < resp->ralloc->n_media; i++)
        rsc_id_cpy((*reqs)[*n_reqs].release->media[i]->med_id,
                   resp->ralloc->media[i]->med_id);

    io_context->read.resp = resp->ralloc;
    rc = read_split_setup(decoder, resp->ralloc->media, resp->ralloc->n_media,
                          &split_size);
    if (rc)
        goto skip_io;

    rc = io_context->ops->read_split(decoder);

    for (i = 0; i < io_context->n_data_extents; i++) {
        struct pho_io_descr *iod = &io_context->iods[i];

        ioa_close(iod->iod_ioa, iod);
    }

skip_io:
    for (i = 0; i < resp->ralloc->n_media; i++) {
        (*reqs)[*n_reqs].release->media[i]->rc = rc;
        (*reqs)[*n_reqs].release->media[i]->to_sync = false;
    }

    for (i = 0; i < n_total_extents(io_context); i++)
        pho_buff_free(&io_context->buffers[i]);

    if (!rc) {
        io_context->read.to_read -= split_size;
        io_context->current_split++;
    }

    /* Nothing more to read: the decoder is done */
    if (io_context->read.to_read == 0) {
        pho_debug("Decoder for '%s' is now finished",
                  decoder->xfer->xd_targets->xt_objid);
        decoder->done = true;
    }

    (*n_reqs)++;

    return rc;
}

static int raid_handle_delete_resp(struct pho_data_processor *eraser,
                                   pho_resp_t *resp, pho_req_t **reqs,
                                   size_t *n_reqs)
{
    struct raid_io_context *io_context = eraser->private_processor;
    size_t split_size;
    int rc;
    int i;

    ENTRY;

    pho_srl_request_release_alloc(*reqs + *n_reqs, resp->ralloc->n_media);

    for (i = 0; i < resp->ralloc->n_media; ++i)
        rsc_id_cpy((*reqs)[*n_reqs].release->media[i]->med_id,
                   resp->ralloc->media[i]->med_id);

    io_context->delete.resp = resp->ralloc;
    rc = delete_split_setup(eraser, resp->ralloc->media, resp->ralloc->n_media,
                            &split_size);

    if (rc)
        goto skip_io;

    rc = io_context->ops->delete_split(eraser);

    for (i = 0; i < io_context->n_data_extents; ++i) {
        struct pho_io_descr *iod = &io_context->iods[i];

        ioa_close(iod->iod_ioa, iod);
    }

skip_io:
    for (i = 0; i < resp->ralloc->n_media; ++i) {
        (*reqs)[*n_reqs].release->media[i]->rc = rc;
        (*reqs)[*n_reqs].release->media[i]->to_sync = true;
        (*reqs)[*n_reqs].release->media[i]->size_written = -split_size;
        (*reqs)[*n_reqs].release->media[i]->nb_extents_written = -1;
    }

    if (!rc)
        io_context->current_split++;

    /* Nothing more to delete: the decoder is done */
    if (io_context->delete.to_delete == 0)
        pho_debug("Decoder for '%s' is now finished",
                  eraser->xfer->xd_targets->xt_objid);

    (*n_reqs)++;

    return rc;
}

static int raid_proc_handle_resp(struct pho_data_processor *proc,
                                 pho_resp_t *resp, pho_req_t **reqs,
                                 size_t *n_reqs)
{
    struct raid_io_context *io_context;
    int rc = 0;
    int i;

    if (pho_response_is_error(resp)) {
        proc->xfer->xd_rc = resp->error->rc;
        proc->done = true;
        LOG_RETURN(proc->xfer->xd_rc,
                   "%s %d received error %s to last request",
                   processor_type2str(proc), resp->req_id,
                   pho_srl_error_kind_str(resp->error));

    } else if (pho_response_is_write(resp)) {
        if (!is_encoder(proc))
            LOG_RETURN(-EINVAL,
                       "Non-encoder data processor received a write response");

        for (i = 0; i < proc->xfer->xd_ntargets; i++) {
            io_context =
                &((struct raid_io_context *) proc->private_processor)[i];

            if (resp->walloc->n_media != n_total_extents(io_context))
                LOG_RETURN(-EINVAL,
                            "Unexpected number of media. "
                            "Expected %lu, got %lu",
                            n_total_extents(io_context),
                            resp->walloc->n_media);
        }

        return raid_handle_write_resp(proc, resp, reqs, n_reqs);

    } else if (pho_response_is_read(resp)) {
        io_context = proc->private_processor;
        io_context->requested_alloc = false;
        if (is_encoder(proc))
            LOG_RETURN(-EINVAL, "Encoder received a read response");

        if (is_eraser(proc)) {
            if (resp->ralloc->n_media != n_total_extents(io_context))
                LOG_RETURN(-EINVAL,
                           "Unexpected number of media. "
                           "Expected %lu, got %lu",
                           n_total_extents(io_context),
                           resp->ralloc->n_media);
            return raid_handle_delete_resp(proc, resp, reqs, n_reqs);
        }

        if (resp->ralloc->n_media != io_context->n_data_extents)
            LOG_RETURN(-EINVAL,
                       "Unexpected number of media. "
                       "Expected %lu, got %lu",
                       io_context->n_data_extents,
                       resp->ralloc->n_media);

        return raid_handle_read_resp(proc, resp, reqs, n_reqs);

    } else if (pho_response_is_partial_release(resp)) {

        return raid_handle_write_resp(proc, NULL, reqs, n_reqs);

    } else if (pho_response_is_release(resp)) {

        if (is_encoder(proc))
            return raid_write_handle_release_resp(proc, resp->release);

        if (is_eraser(proc))
            proc->done = true;

        return 0;
    } else {
        LOG_RETURN(rc = -EPROTO, "Invalid response type '%s'",
                   pho_srl_response_kind_str(resp));
    }
}

int raid_processor_step(struct pho_data_processor *proc, pho_resp_t *resp,
                        pho_req_t **reqs, size_t *n_reqs)
{
    struct raid_io_context *io_context;
    int rc = 0;
    int i;

    ENTRY;

    for (i = 0; i < proc->xfer->xd_ntargets; i++) {
        if (proc->xfer->xd_targets[i].xt_fd < 0 && !is_eraser(proc))
            LOG_RETURN(-EBADF, "No file descriptor in %s",
                       processor_type2str(proc));
    }

    /* At most 2 requests will be emitted, allocate optimistically */
    *reqs = xcalloc(2, sizeof(**reqs));
    *n_reqs = 0;

    if (resp)
        rc = raid_proc_handle_resp(proc, resp, reqs, n_reqs);

    if (rc)
        goto out;

    for (i = 0; i < proc->xfer->xd_ntargets; i++) {
        io_context = &((struct raid_io_context *) proc->private_processor)[i];

        /* go to alloc if the I/O context is not finished */
        if (!io_context->requested_alloc && !no_more_alloc(proc, i))
            goto alloc;
    }

    goto out;

alloc:
    /* Build next request */
    if (is_decoder(proc)) {
        raid_build_read_allocation_req(proc, *reqs + *n_reqs);
    } else if (is_eraser(proc)) {
        raid_build_delete_allocation_req(proc, *reqs + *n_reqs);
    } else {
        rc = raid_build_write_allocation_req(proc, *reqs + *n_reqs);
        if (rc)
            goto out;
    }

    (*n_reqs)++;
    for (i = 0; i < proc->xfer->xd_ntargets; i++) {
        io_context = &((struct raid_io_context *) proc->private_processor)[i];
        io_context->requested_alloc = true;
    }

out:
    if (*n_reqs == 0) {
        free(*reqs);
        *reqs = NULL;
    }

    /* For now, orphaned extents are not cleaned up on failure */
    return rc;
}

int extent_hash_init(struct extent_hash *hash, bool use_md5, bool use_xxhash)
{
    if (use_md5) {
        hash->md5context = EVP_MD_CTX_create();
        if (!hash->md5context)
            LOG_RETURN(-ENOMEM, "Failed to create MD5 context");
    }

#if HAVE_XXH128
    if (use_xxhash) {
        hash->xxh128context = XXH3_createState();
        if (!hash->xxh128context)
            LOG_RETURN(-ENOMEM, "Failed to create XXHASH128 context");
    }
#else
    (void) use_xxhash;
#endif

    return 0;
}

int extent_hash_reset(struct extent_hash *hash)
{
    if (hash->md5context) {
        if (EVP_DigestInit_ex(hash->md5context, EVP_md5(), NULL) == 0)
            LOG_RETURN(-ENOMEM, " ");
    }

#if HAVE_XXH128
    if (hash->xxh128context) {
        if (XXH3_128bits_reset(hash->xxh128context) == XXH_ERROR)
            LOG_RETURN(-ENOMEM, "Failed to initialize XXHASH128 context");
    }
#endif

    return 0;
}

void extent_hash_fini(struct extent_hash *hash)
{
    if (hash->md5context)
        EVP_MD_CTX_destroy(hash->md5context);
#if HAVE_XXH128
    if (hash->xxh128context)
        XXH3_freeState(hash->xxh128context);
#endif
}

int extent_hash_update(struct extent_hash *hash, char *buffer, size_t size)
{
    if (hash->md5context &&
        EVP_DigestUpdate(hash->md5context, buffer, size) == 0) {
        LOG_RETURN(-ENOMEM, "Unable to update MD5");
    }
#if HAVE_XXH128
    if (hash->xxh128context &&
        XXH3_128bits_update(hash->xxh128context, buffer, size) == XXH_ERROR) {
        LOG_RETURN(-ENOMEM, "Unable to update XXHASH128");
    }
#endif

    return 0;
}

int extent_hash_digest(struct extent_hash *hash)
{
    if (hash->md5context) {
        if (EVP_DigestFinal_ex(hash->md5context, hash->md5, NULL) == 0)
            LOG_RETURN(-ENOMEM, "Unable to produce MD5 hash");
    }
#if HAVE_XXH128
    if (hash->xxh128context)
        hash->xxh128 = XXH3_128bits_digest(hash->xxh128context);
#endif
    return 0;
}

int extent_hash_copy(struct extent_hash *hash, struct extent *extent)
{
    if (hash->md5context) {
        memcpy(extent->md5, hash->md5, MD5_BYTE_LENGTH);
        extent->with_md5 = true;
    }

#if HAVE_XXH128
    if (hash->xxh128context) {
        XXH128_canonical_t canonical;

        XXH128_canonicalFromHash(&canonical, hash->xxh128);
        memcpy(extent->xxh128, canonical.digest, sizeof(extent->xxh128));
        extent->with_xxh128 = true;
    }
#endif

    return 0;
}

int extent_hash_compare(struct extent_hash *hash, struct extent *extent)
{
    int rc;

    if (hash->md5context && extent->with_md5) {
        rc = memcmp(hash->md5, extent->md5, MD5_BYTE_LENGTH);
        if (rc)
            goto log_err;
    }

#if HAVE_XXH128
    if (hash->xxh128context && extent->with_xxh128) {
        XXH128_canonical_t canonical;

        XXH128_canonicalFromHash(&canonical, hash->xxh128);
        rc = memcmp(canonical.digest, extent->xxh128, XXH128_BYTE_LENGTH);
        if (rc)
            goto log_err;
    }
#endif
    return 0;

log_err:
    LOG_RETURN(-EINVAL,
               "Hash mismatch: the data in the extent %s/%s has been corrupted",
               extent->media.name, extent->address.buff);
}
