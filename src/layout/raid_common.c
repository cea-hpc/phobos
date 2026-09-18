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
#include <stdbool.h>
#include <string.h>
#include <sys/time.h>
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

size_t get_n_extents(struct raid_io_context *io_context,
                     enum processor_type type)
{
    if (type == PHO_PROC_REBUILDER)
        return io_context->rebuild.current_split_missing_count;
    else
        return n_total_extents(io_context);
}

static struct pho_xfer_put_params *get_put_params(
                                            struct pho_data_processor *proc)
{
    if (proc->type == PHO_PROC_REBUILDER)
        return &proc->xfer->xd_params.rebuild.put;
    else if (proc->type == PHO_PROC_COPIER)
        return &proc->xfer->xd_params.copy.put;
    else
        return &proc->xfer->xd_params.put;
}

static size_t raid_get_nb_split(struct pho_data_processor *rebuilder)
{
    struct raid_io_context *io_context = rebuilder->private_reader;
    struct layout_info *layout = rebuilder->src_layout;
    size_t nb_extent_per_split;
    size_t last_lyt_index;

    nb_extent_per_split = n_total_extents(io_context);
    last_lyt_index = layout->extents[layout->ext_count - 1].layout_idx;

    return (last_lyt_index / nb_extent_per_split) + 1;
}

int raid_get_nb_extent_to_rebuild(struct pho_data_processor *rebuilder)
{
    struct raid_io_context *io_context = rebuilder->private_reader;
    struct layout_info *layout = rebuilder->src_layout;
    int nb_extent_per_split;
    int nb_split;

    if (rebuilder->xfer->xd_params.rebuild.n_extents > 0)
        return rebuilder->xfer->xd_params.rebuild.n_extents;

    nb_split = raid_get_nb_split(rebuilder);
    nb_extent_per_split = n_total_extents(io_context);

    return (nb_split * nb_extent_per_split) - layout->ext_count;
}

GPtrArray *raid_get_extents_to_rebuild_from(struct layout_info *lyt,
                                            int n_extents_per_split,
                                            struct extent *extent_to_rebuild)
{
    GPtrArray *avail_extents = g_ptr_array_new();
    int first_excluded_split_idx;
    int first_split_idx;
    int split;

    split = extent_to_rebuild->layout_idx / n_extents_per_split;
    first_split_idx = split * n_extents_per_split;
    first_excluded_split_idx = (split + 1) * n_extents_per_split;

    for (int i = 0; i < lyt->ext_count; i++) {
        struct extent *_extent = &lyt->extents[i];

        if (_extent->layout_idx < first_split_idx ||
            _extent->layout_idx >= first_excluded_split_idx)
            continue;

        if (pho_id_equal(&extent_to_rebuild->media, &_extent->media))
            continue;

        g_ptr_array_add(avail_extents, _extent);
    }

    return avail_extents;
}

/** Return the extent with the corresponding layout_idx or NULL */
struct extent *extent_from_layout_idx(struct extent *extents,
                                      int ext_count, int layout_idx)
{
    int i;

    for (i = 0; i < ext_count; i++)
        if (extents[i].layout_idx == layout_idx)
            return &extents[i];

    return NULL;
}

/** Number of extent in src layout corresponding to the current split */
static size_t split_src_layout_n_extent(struct layout_info *layout,
                                        size_t current_split,
                                        size_t n_extents_per_split)
{
    size_t n_extent = 0;
    int idx;

    for (idx = current_split * n_extents_per_split;
         idx < (current_split + 1) * n_extents_per_split;
         idx++)
        if (extent_from_layout_idx(layout->extents, layout->ext_count, idx))
            n_extent++;

    return n_extent;
}

/* Check if an index is in the extent list to rebuild */
static bool layout_idx_in_extent_list(struct pho_data_processor *proc,
                                      size_t layout_idx)
{
    const int *extents_idx = proc->xfer->xd_params.rebuild.extents_idx;
    size_t n_extents = proc->xfer->xd_params.rebuild.n_extents;

    for (int i = 0; i < n_extents; i++) {
        if (extents_idx[i] == layout_idx)
            return true;
    }

    return false;
}

bool rebuilder_validate_extent_list(struct pho_data_processor *proc,
                                    size_t n_data_extents,
                                    size_t n_parity_extents)
{
    size_t n_extents_per_split = n_data_extents + n_parity_extents;
    struct layout_info *layout = proc->src_layout;
    size_t current_split = 0;
    const int *extents_idx;
    size_t max_layout_idx;
    size_t n_extents_idx;

    extents_idx = proc->xfer->xd_params.rebuild.extents_idx;
    n_extents_idx = proc->xfer->xd_params.rebuild.n_extents;

    max_layout_idx = layout->extents[layout->ext_count - 1].layout_idx;

    while (current_split * n_extents_per_split <= max_layout_idx) {
        size_t first_layout_idx = current_split * n_extents_per_split;
        size_t first_excluded_idx = (current_split + 1) * n_extents_per_split;
        size_t listed_present = 0;
        size_t found = 0;
        size_t present;

        present = split_src_layout_n_extent(layout, current_split,
                                            n_data_extents + n_parity_extents);

        for (int i = 0; i < n_extents_idx; i++) {
            if (extents_idx[i] < 0 || extents_idx[i] > max_layout_idx)
                return false;

            if (extents_idx[i] >= first_layout_idx &&
                extents_idx[i] < first_excluded_idx)
                found++;
        }

        /* Present extents of this split that are requested for rebuild */
        for (size_t idx = first_layout_idx; idx < first_excluded_idx; idx++) {
            if (extent_from_layout_idx(layout->extents, layout->ext_count,
                                       idx) &&
                layout_idx_in_extent_list(proc, idx))
                listed_present++;
        }

        /*
         * A split cannot rebuild more extents than its available parity
         * extents.
         */
        if (found > n_parity_extents)
            return false;

        /*
         * The extents requested for rebuild are excluded from the
         * reconstruction sources: at least n_data_extents present extents
         * must remain to read from in each split.
         */
        if (present - listed_present < n_data_extents)
            return false;

        current_split++;
    }

    return true;
}

static void free_extent_address_buff(void *void_extent)
{
    struct extent *extent = void_extent;

    free(extent->address.buff);
}

static int common_rebuilder_writer_init(struct raid_io_context *io_context,
                                        enum processor_type type,
                                        struct pho_attrs *attrs)
{
    struct output_io_context *output = raid_output_io_context(io_context, type);
    size_t n_extents = n_total_extents(io_context);
    int rc = 0;

    /* Build the extent attributes from the object ID and the user provided
     * attributes. This information will be attached to backend objects for
     * "self-description"/"rebuild" purpose.
     */
    output->user_md = g_string_new(NULL);
    rc = pho_attrs_to_json(attrs, output->user_md, PHO_ATTR_BACKUP_JSON_FLAGS);
    if (rc) {
        g_string_free(output->user_md, TRUE);
        LOG_RETURN(rc, "Failed to convert attributes to JSON");
    }

    output->written_extents = g_array_new(FALSE, TRUE, sizeof(struct extent));
    g_array_set_clear_func(output->written_extents, free_extent_address_buff);

    output->to_release_media = g_hash_table_new_full(g_pho_id_hash,
                                                     g_pho_id_equal,
                                                     free,
                                                     free);
    io_context->iods = xcalloc(n_extents, sizeof(*io_context->iods));
    output->extents = xcalloc(n_extents, sizeof(*output->extents));

    return rc;
}

int raid_encoder_init(struct pho_data_processor *encoder,
                      const struct module_desc *module,
                      const struct pho_proc_ops *enc_ops,
                      const struct raid_ops *raid_ops)
{
    struct raid_io_context *io_context;
    int rc;
    int i;

    /* The ops field is set early to allow the caller to call the destroy
     * function on error.
     */
    encoder->writer_ops = enc_ops;

    for (i = 0; i < encoder->xfer->xd_ntargets; i++) {
        io_context =
            &((struct raid_io_context *) encoder->private_writer)[i];

        if (encoder->xfer->xd_targets[i].xt_fd < 0)
            LOG_RETURN(-EBADF,
                       "raid: invalid xfer file descriptor in '%s' encoder",
                       encoder->xfer->xd_targets[i].xt_objid);

        /* Do not copy mod_attrs as it may have been modified by the caller
         * before this function is called.
         */
        encoder->dest_layout[i].layout_desc.mod_name = module->mod_name;
        encoder->dest_layout[i].layout_desc.mod_minor = module->mod_minor;
        encoder->dest_layout[i].layout_desc.mod_major = module->mod_major;
        io_context->ops = raid_ops;

        rc = common_rebuilder_writer_init(io_context, encoder->type,
                                        &encoder->xfer->xd_targets[i].xt_attrs);
        if (rc)
            return rc;
    }

    return 0;
}

int raid_decoder_init(struct pho_data_processor *decoder,
                      const struct pho_proc_ops *enc_ops,
                      const struct raid_ops *raid_ops)
{
    struct raid_io_context *io_context = decoder->private_reader;
    int rc;

    if (decoder->xfer->xd_targets->xt_fd < 0)
        LOG_RETURN(rc = -EBADF, "Invalid decoder xfer file descriptor");

    assert(is_decoder(decoder) || is_copier(decoder) || is_rebuilder(decoder));

    decoder->reader_ops = enc_ops;
    io_context->ops = raid_ops;

    io_context->iods = xcalloc(io_context->n_data_extents,
                               sizeof(*io_context->iods));
    io_context->read.extents = xcalloc(io_context->n_data_extents,
                                       sizeof(*io_context->read.extents));

    return 0;
}

int raid_eraser_init(struct pho_data_processor *eraser,
                     const struct pho_proc_ops *eraser_ops,
                     const struct raid_ops *raid_ops)
{
    struct raid_io_context *io_context = eraser->private_eraser;
    size_t n_extents = n_total_extents(io_context);

    assert(is_eraser(eraser));
    eraser->eraser_ops = eraser_ops;
    io_context->ops = raid_ops;

    io_context->iods = xcalloc(n_extents, sizeof(*io_context->iods));
    return 0;
}

int raid_rebuilder_init(struct pho_data_processor *rebuilder,
                        const struct module_desc *module,
                        const struct pho_proc_ops *enc_ops,
                        const struct raid_ops *raid_ops)
{
    struct raid_io_context *io_context = rebuilder->private_writer;
    int rc;

    rebuilder->writer_ops = enc_ops;
    io_context->ops = raid_ops;

    rebuilder->dest_layout->layout_desc.mod_name = module->mod_name;
    rebuilder->dest_layout->layout_desc.mod_minor = module->mod_minor;
    rebuilder->dest_layout->layout_desc.mod_major = module->mod_major;

    rc = pho_attrs_copy(&rebuilder->src_layout->layout_desc.mod_attrs,
                        &rebuilder->dest_layout->layout_desc.mod_attrs);
    if (rc)
        return rc;

    return common_rebuilder_writer_init(io_context, rebuilder->type,
                                        &rebuilder->xfer->xd_targets->xt_attrs);
}

static void read_resp_destroy(struct read_io_context *read_context)
{
    if (read_context->resp) {
        pho_srl_response_free(read_context->resp, false);
        free(read_context->resp);
        read_context->resp = NULL;
    }
}

static void write_resp_destroy(struct pho_data_processor *proc)
{
    if (proc->write_resp) {
        pho_srl_response_free(proc->write_resp, false);
        free(proc->write_resp);
        proc->write_resp = NULL;
    }
}

static void writer_release_alloc_destroy(struct pho_data_processor *proc)
{
    if (proc->writer_release_alloc) {
        pho_srl_request_free(proc->writer_release_alloc, false);
        free(proc->writer_release_alloc);
        proc->writer_release_alloc = NULL;
    }
}

void raid_reader_processor_destroy(struct pho_data_processor *proc)
{
    struct raid_io_context *io_context;
    int i;

    for (i = 0; i < proc->xfer->xd_ntargets; i++) {
        io_context = &((struct raid_io_context *) proc->private_reader)[i];

        if (!io_context)
            return;

        read_resp_destroy(&io_context->read);
        free(io_context->read.extents);
        free(io_context->iods);
        free(io_context->hashes);
    }

    free(proc->private_reader);
    proc->private_reader = NULL;
}

void raid_eraser_processor_destroy(struct pho_data_processor *proc)
{
    struct raid_io_context *io_context;
    int i;

    for (i = 0; i < proc->xfer->xd_ntargets; i++) {
        io_context = &((struct raid_io_context *) proc->private_eraser)[i];

        if (!io_context)
            return;

        free(io_context->iods);
    }

    free(proc->private_eraser);
    proc->private_eraser = NULL;
}

void raid_writer_rebuilder_processor_destroy(struct pho_data_processor *proc)
{
    struct raid_io_context *io_context;
    struct output_io_context *output;
    int i, j;

    for (i = 0; i < proc->xfer->xd_ntargets; i++) {
        io_context = &((struct raid_io_context *) proc->private_writer)[i];

        if (!io_context)
            return;

        output = raid_output_io_context(io_context, proc->type);

        if (output->written_extents)
            g_array_free(output->written_extents, TRUE);

        if (output->to_release_media)
            g_hash_table_destroy(output->to_release_media);

        output->written_extents = NULL;
        output->to_release_media = NULL;
        for (j = 0; j < n_total_extents(io_context); ++j) {
            free(output->extents[j].uuid);
            free(output->extents[j].address.buff);
        }
        free(output->extents);
        free(io_context->iods);
        g_string_free(output->user_md, TRUE);

        for (j = 0; j < io_context->nb_hashes; j++)
            extent_hash_fini(&io_context->hashes[j]);

        free(io_context->hashes);
    }

    write_resp_destroy(proc);
    writer_release_alloc_destroy(proc);

    free(proc->private_writer);
    proc->private_writer = NULL;
}

static struct raid_io_context *io_context_from_proc(
    struct pho_data_processor *proc, int target_idx, enum processor_type type)
{
    if (type == PHO_PROC_ENCODER)
        return &((struct raid_io_context *)proc->private_writer)[target_idx];

    if (type == PHO_PROC_DECODER)
        return &((struct raid_io_context *)proc->private_reader)[target_idx];

    /* PHO_PROC_ERASER */
    return &((struct raid_io_context *)proc->private_eraser)[target_idx];
}

struct pho_ext_loc make_ext_location(struct pho_data_processor *proc,
                                     size_t alloc_medium_index,
                                     size_t context_or_layout_ext_index,
                                     int target_idx, enum processor_type type)
{
    struct raid_io_context *io_context = io_context_from_proc(proc, target_idx,
                                                              type);
    struct pho_ext_loc loc;

    if (type == PHO_PROC_ENCODER) {
        struct output_io_context *output = raid_output_io_context(io_context,
                                                                  proc->type);
        loc.root_path =
            proc->write_resp->walloc->media[alloc_medium_index]->root_path;
        loc.addr_type =
            proc->write_resp->walloc->media[alloc_medium_index]->addr_type;
        loc.extent = &output->extents[context_or_layout_ext_index];
    } else if (type == PHO_PROC_DECODER) {
        loc.root_path =
            io_context->read.resp->ralloc->media[alloc_medium_index]->root_path;
        loc.addr_type =
            io_context->read.resp->ralloc->media[alloc_medium_index]->addr_type;
        loc.extent = io_context->read.extents[context_or_layout_ext_index];
    } else {
        /* PHO_PROC_ERASER */
        loc.root_path =
            io_context->delete.resp->media[alloc_medium_index]->root_path;
        loc.addr_type =
            io_context->delete.resp->media[alloc_medium_index]->addr_type;
        loc.extent = &proc->src_layout->extents[context_or_layout_ext_index];
    }

    return loc;
}

static int raid_io_context_open(struct raid_io_context *io_context,
                                struct pho_data_processor *proc,
                                size_t count, int target_idx,
                                enum processor_type type)
{
    size_t i;
    int rc;

    for (i = 0; i < count; ++i) {
        struct pho_ext_loc ext_location = make_ext_location(proc, i, i,
                                                            target_idx, type);
        struct pho_io_descr *iod = &io_context->iods[i];

        iod->iod_size = 0;
        iod->iod_loc = &ext_location;
        if (!is_eraser(proc)) {
            rc = ioa_open(iod->iod_ioa,
                          proc->xfer->xd_targets[target_idx].xt_objid, iod,
                          type == PHO_PROC_ENCODER);
            if (rc)
                LOG_GOTO(out_close, rc,
                         "raid: unable to open extent for '%s' on '%s':'%s'",
                         proc->xfer->xd_targets[target_idx].xt_objid,
                         ext_location.extent->media.library,
                         ext_location.extent->media.name);
        }
    }

    return 0;

out_close:
    while (i > 0) {
        struct pho_io_descr *iod;

        i--;
        iod = &io_context->iods[i];

        ioa_close(iod->iod_ioa, iod);
    }

    return rc;
}

static int xfer_remain_to_write_per_medium(
    struct pho_data_processor *proc, size_t *size)
{
    struct raid_io_context *io_context =
        &((struct raid_io_context *)
          proc->private_writer)[proc->current_target];
    int n_data_extents = io_context->n_data_extents;
    size_t fs_block_size = 0;
    int rc;
    int i;

    *size = 0;

    rc = get_cfg_fs_block_size(get_put_params(proc)->family,
                               &fs_block_size);
    if (rc)
        return rc;

    for (i = proc->current_target; i < proc->xfer->xd_ntargets; i++) {
        size_t size_per_extent = 0;
        size_t target_remain_size;
        size_t nb_block = 0;

        if (i > proc->current_target)
            target_remain_size = proc->xfer->xd_targets[i].xt_size;
        else
            target_remain_size = proc->object_size - proc->writer_offset;

        /* Add an overhead to the total size to write to anticipate the size
         * taken by the xattrs and directory.
         *
         * The phys_spc_used is:
         * ceil(size / fs_block_size) * fs_block_size + 3 * fs_block_size
         */
        if (fs_block_size > 0) {
            size_per_extent = (target_remain_size + n_data_extents - 1) /
                              n_data_extents;
            nb_block = (size_per_extent + (fs_block_size - 1)) / fs_block_size;
            *size += nb_block * fs_block_size + 3 * fs_block_size;

        /* If the block size of the file system is not defined in the conf,
         * the size to alloc is equal to the size to write.
         */
        } else {
            *size += (target_remain_size + n_data_extents - 1) / n_data_extents;
        }
    }

    return 0;
}

static bool extent_is_in_current_split(struct raid_io_context *io_context,
                                       const struct extent *extent)
{
    size_t n_extents_per_split = n_total_extents(io_context);
    size_t first_idx = io_context->current_split * n_extents_per_split;
    size_t first_excluded_idx =
        (io_context->current_split + 1) * n_extents_per_split;
    size_t layout_idx = extent->layout_idx;

    return layout_idx >= first_idx && layout_idx < first_excluded_idx;
}

static void raid_writer_rebuilder_build_allocation_req(
                                             struct pho_data_processor *proc,
                                             pho_req_t *req, size_t size)
{
    struct raid_io_context *io_context =
        &((struct raid_io_context *)
          proc->private_writer)[proc->current_target];
    struct pho_xfer_put_params *put_params;
    size_t n_avoid_med = 0;
    size_t n_extents;
    size_t *n_tags;
    int i, j;

    ENTRY;

    n_extents = get_n_extents(io_context, proc->type);
    n_tags = xcalloc(n_extents, sizeof(*n_tags));

    put_params = get_put_params(proc);

    for (i = 0; i < n_extents; ++i)
            n_tags[i] = put_params->tags.count;

    if (is_copier(proc)) {
        n_avoid_med = proc->src_layout->ext_count;
    } else if (is_rebuilder(proc)) {
        for (i = 0; i < proc->src_layout->ext_count; i++) {
            struct extent *extent = &proc->src_layout->extents[i];

            if (extent_is_in_current_split(io_context, extent))
                n_avoid_med++;
        }
    }

    pho_srl_request_write_alloc(req, n_extents, n_tags, n_avoid_med);
    free(n_tags);

    for (i = 0; i < n_extents; ++i) {
        req->walloc->media[i]->size = size;

        for (j = 0; j < put_params->tags.count; ++j)
            req->walloc->media[i]->tags[j] =
                xstrdup(put_params->tags.strings[j]);
    }

    req->walloc->no_split = put_params->no_split;

    if (is_copier(proc) || is_rebuilder(proc)) {
        size_t avoid_idx = 0;

        for (i = 0; i < proc->src_layout->ext_count; i++) {
            struct extent *extent = &proc->src_layout->extents[i];

            if (is_rebuilder(proc) &&
                !extent_is_in_current_split(io_context, extent))
                continue;

            req->walloc->avoid_med[avoid_idx]->family = extent->media.family;
            req->walloc->avoid_med[avoid_idx]->name =
                xstrdup(extent->media.name);
            req->walloc->avoid_med[avoid_idx]->library =
                xstrdup(extent->media.library);
            avoid_idx++;
        }
    }
}

/* The older a ctime is, the higher its priority. */
static inline int64_t priority_from_ctime(struct timeval copy_ctime)
{
   return -((int64_t)copy_ctime.tv_sec * (int64_t)1000000 +
            (int64_t)copy_ctime.tv_usec);
}

static size_t current_split_src_layout_n_extent(struct pho_data_processor *proc,
                                                enum processor_type type)
{
    struct raid_io_context *io_context =
        io_context_from_proc(proc, proc->current_target, type);
    size_t n_extents_per_split = n_total_extents(io_context);

    return split_src_layout_n_extent(proc->src_layout,
                                     io_context->current_split,
                                     n_extents_per_split);
}

static size_t current_split_rebuild_n_extent(struct pho_data_processor *proc,
                                             enum processor_type type)
{
    struct raid_io_context *io_context =
        io_context_from_proc(proc, proc->current_target, type);
    size_t n_extents_per_split = n_total_extents(io_context);
    size_t first_idx = io_context->current_split * n_extents_per_split;
    size_t first_excluded_idx =
        (io_context->current_split + 1) * n_extents_per_split;
    size_t n_extents = 0;

    if (proc->xfer->xd_params.rebuild.n_extents > 0) {
        const struct pho_xfer_rebuild_params *params =
            &proc->xfer->xd_params.rebuild;

        for (size_t i = 0; i < params->n_extents; i++)
            if (params->extents_idx[i] >= first_idx &&
                params->extents_idx[i] < first_excluded_idx)
                n_extents++;

        return n_extents;
    }

    n_extents = current_split_src_layout_n_extent(proc, type);
    if (n_extents == n_extents_per_split)
        return 0;

    return n_extents_per_split - n_extents;
}

static bool rebuilder_can_read_extent(struct pho_data_processor *proc,
                                      size_t n_extents_per_split,
                                      struct extent *extent)
{
    struct pho_xfer_rebuild_params *params = &proc->xfer->xd_params.rebuild;

    if (!is_rebuilder(proc))
        return true;

    if (params->n_extents > 0 &&
        layout_idx_in_extent_list(proc, extent->layout_idx))
        return false;

    if (params->media_read && !pho_id_equal(params->media_read, &extent->media))
        return false;

    return true;
}

/* Retrieve the number of extents available to read from.
 * If proc is a REBUILDER and a list of extents to rebuild is specified,
 * also check that these extents are not requested for rebuild.
 */
static size_t current_split_read_alloc_n_extent(struct pho_data_processor *proc,
                                                enum processor_type type)
{
    struct raid_io_context *io_context =
        io_context_from_proc(proc, proc->current_target, type);
    size_t n_extents_per_split = n_total_extents(io_context);
    size_t n_extents = 0;
    int idx;

    for (idx = io_context->current_split * n_extents_per_split;
         idx < (io_context->current_split + 1) * n_extents_per_split;
         idx++) {
        struct extent *extent =
            extent_from_layout_idx(proc->src_layout->extents,
                                   proc->src_layout->ext_count, idx);

        if (!extent)
            continue;

        if (!rebuilder_can_read_extent(proc, n_extents_per_split, extent))
            continue;

        n_extents++;
    }

    return n_extents;
}

/** Generate the next read or delete allocation request for this eraser */
static void raid_reader_eraser_build_allocation_req(
    struct pho_data_processor *proc, pho_req_t *req, enum processor_type type)
{
    struct raid_io_context *io_context =
        io_context_from_proc(proc, proc->current_target, type);
    size_t n_extents_per_split = n_total_extents(io_context);
    size_t current_split_n_extents = 0;
    int read_alloc_med_idx = 0;
    int layout_idx;

    ENTRY;

    current_split_n_extents = current_split_read_alloc_n_extent(proc, type);

    if (is_decoder(proc) || is_copier(proc) || is_rebuilder(proc))
        assert(current_split_n_extents >= io_context->n_data_extents);

    pho_srl_request_read_alloc(req, current_split_n_extents);
    req->has_qos = true;
    req->qos = 0;
    req->has_priority = true;
    req->priority = priority_from_ctime(proc->src_copy_ctime);
    req->ralloc->n_required =
        is_eraser(proc) ? current_split_n_extents : io_context->n_data_extents;
    req->ralloc->operation =
        is_eraser(proc) ? PHO_READ_TARGET_ALLOC_OP_DELETE :
                          PHO_READ_TARGET_ALLOC_OP_READ;

    for (layout_idx = io_context->current_split * n_extents_per_split;
         layout_idx < (io_context->current_split + 1) * n_extents_per_split;
         layout_idx++) {
        struct extent *extent =
            extent_from_layout_idx(proc->src_layout->extents,
                                   proc->src_layout->ext_count, layout_idx);

        if (!extent)
            continue;

        if (!rebuilder_can_read_extent(proc, n_extents_per_split, extent))
            continue;

        req->ralloc->med_ids[read_alloc_med_idx]->family = extent->media.family;
        req->ralloc->med_ids[read_alloc_med_idx]->name =
            xstrdup(extent->media.name);
        req->ralloc->med_ids[read_alloc_med_idx]->library =
            xstrdup(extent->media.library);
        read_alloc_med_idx++;
    }
}

static void raid_set_extent_info(struct extent *extent,
                                 pho_resp_write_elt_t *medium,
                                 int idx, size_t offset)
{
    extent->uuid = generate_uuid();
    extent->layout_idx = idx;
    extent->offset = offset;
    extent->media.family = (enum rsc_family)medium->med_id->family;

    pho_id_name_set(&extent->media, medium->med_id->name,
                    medium->med_id->library);
}

static void raid_writer_set_extent_info(struct raid_io_context *io_context,
                                        pho_resp_write_elt_t **medium,
                                        int extent_idx, size_t offset)
{
    struct extent *extents = io_context->write.output.extents;
    int i;

    for (i = 0; i < n_total_extents(io_context); i++)
        raid_set_extent_info(&extents[i], medium[i], extent_idx + i, offset);
}

static bool extent_from_rebuild_list(const struct pho_data_processor *proc,
                                     int idx)
{
    const struct pho_xfer_rebuild_params *params =
        &proc->xfer->xd_params.rebuild;

    for (size_t i = 0; i < params->n_extents; i++)
        if (params->extents_idx[i] == idx)
            return true;

    return false;
}

static void raid_rebuilder_set_extent_info(struct pho_data_processor *proc,
                                           struct raid_io_context *io_context,
                                           pho_resp_write_elt_t **medium,
                                           struct layout_info *layout)
{
    size_t n_extents_per_split = n_total_extents(io_context);
    struct extent *extents = io_context->rebuild.output.extents;
    int j = 0;
    int i;

    for (i = io_context->current_split * n_extents_per_split;
         i < (io_context->current_split + 1) * n_extents_per_split;
         i++) {
        bool rebuild_extent;

        if (proc->xfer->xd_params.rebuild.n_extents > 0)
            rebuild_extent = extent_from_rebuild_list(proc, i);
        else
            rebuild_extent = !extent_from_layout_idx(layout->extents,
                                                     layout->ext_count, i);

        if (rebuild_extent) {
            raid_set_extent_info(&extents[j], medium[j], i,
                                 io_context->current_split_offset);
            j++;
        }
    }
}

static void raid_io_context_set_extent_size(struct raid_io_context *io_context,
                                            enum processor_type type,
                                            size_t extent_size,
                                            size_t extent_size_remainder)
{
    struct output_io_context *output;
    struct extent *extents;
    size_t n_extents;
    int i;

    n_extents = get_n_extents(io_context, type);
    output = raid_output_io_context(io_context, type);
    extents = output->extents;

    for (i = 0; i < n_extents; i++) {
        size_t extent_idx;

        if (type == PHO_PROC_REBUILDER)
            extent_idx = extents[i].layout_idx % n_total_extents(io_context);
        else
            extent_idx = i;

        extents[i].size = extent_size;

        if (extent_size_remainder > 0 &&
            (extent_idx < extent_size_remainder ||
             extent_idx >= io_context->n_data_extents))
            extents[i].size++;
    }
}

static void raid_io_context_setmd(struct raid_io_context *io_context,
                                  enum processor_type type,
                                  const GString *str)
{
    size_t n_extents = get_n_extents(io_context, type);
    struct pho_io_descr *iods = io_context->iods;
    int i;

    for (i = 0; i < n_extents; ++i) {
        if (!gstring_empty(str))
            pho_attr_set(&iods[i].iod_attrs, PHO_EA_UMD_NAME, str->str);
    }
}

static void add_new_to_release_media(struct raid_io_context *io_context,
                                     enum processor_type type,
                                     const struct pho_id *media_id)
{
    struct output_io_context *output;
    gboolean was_not_in;
    size_t *ref_count;

    ref_count = xmalloc(sizeof(*ref_count));
    *ref_count = 1;

    output = raid_output_io_context(io_context, type);
    was_not_in = g_hash_table_insert(output->to_release_media,
                                     pho_id_dup(media_id), ref_count);
    assert(was_not_in);
}

static void raid_io_add_written_extent(struct raid_io_context *io_context,
                                       enum processor_type type,
                                       struct extent *extent)
{
    struct output_io_context *output;
    size_t *to_release_refcount;

    output = raid_output_io_context(io_context, type);

    /* add extent to written ones */
    g_array_append_val(output->written_extents, *extent);

    /* add medium to be released */
    to_release_refcount = g_hash_table_lookup(output->to_release_media,
                                              &extent->media);

    if (to_release_refcount) /* existing media_id to release */
        ++(*to_release_refcount);
    else                     /* new media_id to release */
        add_new_to_release_media(io_context, type, &extent->media);

    /* Since we make a copy of the extent, reset this one to avoid reusing
     * internal pointers somewhere else in the code.
     */
    memset(extent, 0, sizeof(*extent));
}

/* return the index of the extent corresponding to medium or -1 */
static ssize_t extent_index(struct layout_info *layout,
                            const pho_rsc_id_t *medium,
                            int first_layout_index,
                            int first_excluded_layout_index)
{
    int i;

    for (i = 0; i < layout->ext_count; i++) {
        if (layout->extents[i].layout_idx >= first_layout_index &&
            layout->extents[i].layout_idx < first_excluded_layout_index &&
            !strcmp(layout->extents[i].media.name, medium->name) &&
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

static void pho_id_from_rsc_id(const pho_rsc_id_t *medium, struct pho_id *dst)
{
    dst->family = medium->family;
    pho_id_name_set(dst, medium->name, medium->library);
}

static int mark_written_medium_released(struct raid_io_context *io_context,
                                        enum processor_type type,
                                        const pho_rsc_id_t *medium, bool *found)
{
    struct output_io_context *output;
    size_t *to_release_refcount;
    struct pho_id copy;

    output = raid_output_io_context(io_context, type);

    pho_id_from_rsc_id(medium, &copy);
    to_release_refcount = g_hash_table_lookup(output->to_release_media, &copy);

    if (to_release_refcount == NULL)
        return 0;

    *found = true;

    /* media id with refcount of zero must be removed from the hash table */
    assert(*to_release_refcount > 0);

    /* one medium was released */
    output->n_released_media++;

    /* only one release was ongoing for this medium: remove from the table */
    if (*to_release_refcount == 1) {
        gboolean was_in_table;

        was_in_table = g_hash_table_remove(output->to_release_media, &copy);
        assert(was_in_table);
        return 0;
    }

    /* used for several extents: only decrement once */
    --(*to_release_refcount);
    return 0;
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

static pho_resp_t *copy_response_read_alloc(pho_resp_t *resp)
{
    pho_resp_read_t *rresp;
    pho_resp_t *resp_cpy;
    int i;

    resp_cpy = xcalloc(1, sizeof(*resp));
    pho_srl_response_read_alloc(resp_cpy, resp->ralloc->n_media);
    rresp = resp_cpy->ralloc;

    for (i = 0; i < resp->ralloc->n_media; i++) {
        rsc_id_cpy(rresp->media[i]->med_id, resp->ralloc->media[i]->med_id);
        rresp->media[i]->root_path = xstrdup(resp->ralloc->media[i]->root_path);
        rresp->media[i]->fs_type = resp->ralloc->media[i]->fs_type;
        rresp->media[i]->addr_type = resp->ralloc->media[i]->addr_type;
    }

    return resp_cpy;
}

static pho_resp_t *copy_response_write_alloc(pho_resp_t *resp)
{
    pho_resp_write_t *wresp;
    pho_resp_t *resp_cpy;
    int i;

    resp_cpy = xcalloc(1, sizeof(*resp));
    pho_srl_response_write_alloc(resp_cpy, resp->walloc->n_media);
    wresp = resp_cpy->walloc;

    if (resp->walloc->threshold) {
        resp_cpy->walloc->threshold =
            xcalloc(1, sizeof(*resp_cpy->walloc->threshold));
        pho_sync_threshold__init(resp_cpy->walloc->threshold);
        wresp->threshold->sync_nb_req =
            resp->walloc->threshold->sync_nb_req;
        wresp->threshold->sync_wsize_kb =
            resp->walloc->threshold->sync_wsize_kb;
        wresp->threshold->sync_time_sec =
            resp->walloc->threshold->sync_time_sec;
        wresp->threshold->sync_time_nsec =
            resp->walloc->threshold->sync_time_nsec;
    }

    for (i = 0; i < resp->walloc->n_media; i++) {
        rsc_id_cpy(wresp->media[i]->med_id, resp->walloc->media[i]->med_id);
        wresp->media[i]->avail_size = resp->walloc->media[i]->avail_size;
        wresp->media[i]->root_path = xstrdup(resp->walloc->media[i]->root_path);
        wresp->media[i]->fs_type = resp->walloc->media[i]->fs_type;
        wresp->media[i]->addr_type = resp->walloc->media[i]->addr_type;
    }

    return resp_cpy;
}

static void set_current_split_chunk_size(struct raid_io_context *io_context,
                                         size_t n_iod)
{
    int i;

    io_context->current_split_chunk_size = 0;
    for (i = 0; i < n_iod; i++) {
        ssize_t size;

        size = ioa_preferred_io_size(io_context->iods[i].iod_ioa,
                                     &io_context->iods[i]);
        if (size <= 0)
            /* fallback: get the system page size */
            size = sysconf(_SC_PAGESIZE);

        if (!io_context->current_split_chunk_size)
            io_context->current_split_chunk_size = size;
        else
            lcm(io_context->current_split_chunk_size, size);
    }
}

static int raid_reader_split_setup(struct pho_data_processor *proc,
                                   pho_resp_t *resp)
{
    struct raid_io_context *io_context = proc->private_reader;
    pho_resp_read_elt_t **medium = resp->ralloc->media;
    size_t n_media = resp->ralloc->n_media;
    size_t i;
    int rc;

    ENTRY;

    if (n_media != io_context->n_data_extents)
        LOG_RETURN(-EINVAL, "Invalid number of media return by phobosd. "
                            "Expected %lu, got %lu",
                   io_context->n_data_extents, n_media);

    read_resp_destroy(&io_context->read);
    io_context->read.resp = copy_response_read_alloc(resp);

    /* identify extents corresponding to received media */
    for (i = 0; i < n_media; i++) {
        ssize_t ext_index;

        rc = get_io_adapter((enum fs_type) medium[i]->fs_type,
                            &io_context->iods[i].iod_ioa);
        if (rc)
            return rc;

        ext_index = extent_index(
            proc->src_layout,
            medium[i]->med_id,
            io_context->current_split * n_total_extents(io_context),
            (io_context->current_split + 1) * n_total_extents(io_context));

        if (ext_index == -1)
            LOG_RETURN(-ENOMEDIUM,
                       "Did not find medium '%s':'%s' in reader layout of '%s'",
                       medium[i]->med_id->library,
                       medium[i]->med_id->name,
                       proc->xfer->xd_targets->xt_objid);

        io_context->read.extents[i] = &proc->src_layout->extents[ext_index];
    }

    sort_extents_by_layout_index(io_context->read.resp->ralloc,
                                 io_context->read.extents, n_media);

    rc = raid_io_context_open(io_context, proc, n_media, 0, PHO_PROC_DECODER);
    if (rc)
        return rc;

    /* check extent size */
    for (i = 0; i < n_media; i++) {
        ssize_t size;

        size = ioa_size(io_context->iods[i].iod_ioa, &io_context->iods[i]);
        /* If not supported, skip the check */
        if (size == -ENOTSUP)
            break;
        if (size < 0) {
            rc = size;
            goto close_iod;
        }

        if (size != io_context->read.extents[i]->size)
            LOG_GOTO(close_iod, rc = -EINVAL,
                     "Extent size mismatch: %zd whereas we expect %zd",
                     size, io_context->read.extents[i]->size);
    }

    rc = io_context->ops->get_reader_chunk_size(
             proc, &io_context->current_split_chunk_size);
    if (rc)
        goto close_iod;

    if (!io_context->current_split_chunk_size) {
        if (proc->io_block_size)
            io_context->current_split_chunk_size = proc->io_block_size;
        else
            set_current_split_chunk_size(io_context, n_media);
    }

    if (!proc->reader_stripe_size)
        proc->reader_stripe_size = io_context->current_split_chunk_size *
                                   io_context->n_data_extents;

    if (io_context->read.check_hash) {
        for (i = 0; i < io_context->nb_hashes; i++) {
            rc = extent_hash_init(&io_context->hashes[i],
                                  io_context->read.extents[i]->with_md5,
                                  io_context->read.extents[i]->with_xxh128);

            if (rc)
                goto close_iod;

            rc = extent_hash_reset(&io_context->hashes[i]);
            if (rc)
                goto close_iod;
        }
    }

    io_context->current_split_size = 0;
    for (i = 0; i < io_context->n_data_extents; i++)
        io_context->current_split_size += io_context->read.extents[i]->size;

    /*
     * We limit the current split size to size that we really need to read.
     * There is only one case where we are not supposed to read all bytes
     * of all extents, that is when we read from parity extents that are longer
     * than the data extents they are currently replacing.
     */
    io_context->current_split_size = min(io_context->current_split_size,
                                         proc->object_size -
                                             proc->reader_offset);

    return 0;

close_iod:
    for (i = 0; i < n_media; i++)
        ioa_close(io_context->iods[i].iod_ioa, &io_context->iods[i]);

    return rc;
}

static int raid_reader_split_fini(struct pho_data_processor *proc)
{
    struct raid_io_context *io_context = proc->private_reader;
    int rc;
    int i;

    if (io_context->read.check_hash) {
        for (i = 0; i < io_context->n_data_extents; i++) {
            rc = extent_hash_digest(&io_context->hashes[i]);
            if (rc)
                return rc;

            rc = extent_hash_compare(&io_context->hashes[i],
                                     io_context->read.extents[i]);
            if (rc)
                return rc;

            extent_hash_fini(&io_context->hashes[i]);
        }
    }

    for (i = 0; i < io_context->n_data_extents; i++) {
        rc = ioa_close(io_context->iods[i].iod_ioa, &io_context->iods[i]);
        if (rc)
            return rc;
    }

    /* next split */
    io_context->current_split++;
    io_context->current_split_offset = proc->reader_offset;
    io_context->current_split_chunk_size = 0;
    return 0;
}

static size_t raid_seek_next_missing_split(struct pho_data_processor *proc,
                                           enum processor_type type,
                                           bool update_offset)
{
    struct raid_io_context *io_context =
        io_context_from_proc(proc, proc->current_target, type);
    size_t n_extents_per_split = n_total_extents(io_context);
    struct layout_info *layout = proc->src_layout;
    size_t max_layout_idx;
    int i;

    ENTRY;

    max_layout_idx = layout->extents[layout->ext_count - 1].layout_idx;

    while (io_context->current_split * n_extents_per_split <= max_layout_idx) {
        size_t first_layout_idx =
            io_context->current_split * n_extents_per_split;
        size_t n_extents = current_split_rebuild_n_extent(proc, type);

        if (n_extents == 0) {
            io_context->current_split++;
            continue;
        }

        if (type == PHO_PROC_ENCODER)
            io_context->rebuild.current_split_missing_count = n_extents;

        /* Get offset of the first extent of this split */
        for (i = 0; i < layout->ext_count; i++) {
            struct extent *extent = &layout->extents[i];

            if (extent->layout_idx < first_layout_idx)
                continue;

            io_context->current_split_offset = extent->offset;

            if (update_offset) {
                if (type == PHO_PROC_ENCODER) {
                    proc->writer_offset = extent->offset;
                } else if (type == PHO_PROC_DECODER) {
                    proc->reader_offset = extent->offset;
                    proc->buffer_offset = extent->offset;
                }
            }

            return true;
        }
    }

    return false;
}

int raid_reader_processor_step(struct pho_data_processor *proc,
                               pho_resp_t *resp, pho_req_t **reqs,
                               size_t *n_reqs)
{
    struct raid_io_context *io_context = proc->private_reader;
    bool has_next_split_to_read = true;
    bool stop_io = false;
    bool need_new_alloc;
    bool need_release;
    bool split_ended;
    int rc;
    int i;

    /* manage error */
    if (resp && pho_response_is_error(resp)) {
        proc->xfer->xd_rc = resp->error->rc;
        proc->done = true;
        LOG_RETURN(proc->xfer->xd_rc,
                   "%s %d received error %s to last request",
                   processor_type2str(proc), resp->req_id,
                   pho_srl_error_kind_str(resp->error));

    }

    /* first init step from the data processor: return first allocation */
    if (!resp && !proc->buff.size) {
        /* If the reader is used in a rebuilder proc, we need to get the
         * first split with missing extents before allocation media.
         */
        if (is_rebuilder(proc) &&
            !raid_seek_next_missing_split(proc, PHO_PROC_DECODER, true))
            return 0;

        *reqs = xcalloc(1, sizeof(**reqs));
        *n_reqs = 1;
        raid_reader_eraser_build_allocation_req(proc, *reqs, PHO_PROC_DECODER);
        proc->need_alloc_response_to_read = true;
        return 0;
    }

    /* manage received allocation */
    if (resp) {
        if (is_rebuilder(proc) &&
            proc->reader_offset < io_context->current_split_offset) {
            proc->reader_offset = io_context->current_split_offset;
            proc->buffer_offset = io_context->current_split_offset;
        }

        proc->need_alloc_response_to_read = false;
        rc = raid_reader_split_setup(proc, resp);
        if (rc)
            goto release;
    }

    if (proc->xfer->xd_targets[proc->current_target].xt_rc != 0) {
        stop_io = true;
        rc = proc->xfer->xd_targets[proc->current_target].xt_rc;
        goto release;
    }

    /* read */
    if (!proc->buff.size)
        return 0;

    rc = io_context->ops->read_into_buff(proc);
    if (rc)
        goto release;

    split_ended = (proc->reader_offset - io_context->current_split_offset) >=
                  io_context->current_split_size;
    if (split_ended)
        rc = raid_reader_split_fini(proc);

    if (rc && !proc->xfer->xd_targets[proc->current_target].xt_rc)
        proc->xfer->xd_targets[proc->current_target].xt_rc = rc;

release:
    if (!rc && split_ended && is_rebuilder(proc))
        has_next_split_to_read = raid_seek_next_missing_split(proc,
                                                              PHO_PROC_DECODER,
                                                              false);
    need_release = rc || split_ended;
    need_new_alloc = !rc && split_ended && has_next_split_to_read &&
                     proc->reader_offset < proc->object_size;
    if (need_release) {
        if (need_new_alloc)
            *reqs = xcalloc(2, sizeof(**reqs));
        else
            *reqs = xcalloc(1, sizeof(**reqs));
    }

    if (need_release) {
        pho_srl_request_release_alloc(*reqs + *n_reqs,
                                      io_context->read.resp->ralloc->n_media,
                                      true);
        for (i = 0; i < io_context->read.resp->ralloc->n_media; i++) {
            rsc_id_cpy((*reqs)[*n_reqs].release->media[i]->med_id,
                       io_context->read.resp->ralloc->media[i]->med_id);
            (*reqs)[*n_reqs].release->media[i]->rc = stop_io ? 0 : rc;
            (*reqs)[*n_reqs].release->media[i]->to_sync = false;
        }

        (*n_reqs)++;
   }


   if (need_new_alloc) {
        raid_reader_eraser_build_allocation_req(proc, *reqs + *n_reqs,
                                                PHO_PROC_DECODER);
        (*n_reqs)++;
        proc->need_alloc_response_to_read = true;
   }

   return rc;
}

static int prepare_writer_rebuilder_release_request(
                                          struct pho_data_processor *proc,
                                          pho_resp_t *new_resp)
{
    int rc;
    int i;

    rc = clock_gettime(CLOCK_REALTIME, &proc->writer_start_req);
    if (rc)
        LOG_RETURN(-errno, "clock_gettime: unable to get CLOCK_REALTIME");

    if (!pho_response_is_partial_release(new_resp)) {
        write_resp_destroy(proc);
        proc->write_resp = copy_response_write_alloc(new_resp);
    }

    /* prepare release and potential next alloc */
    proc->writer_release_alloc =
        xcalloc(2, sizeof(*proc->writer_release_alloc));
    pho_srl_request_release_alloc(proc->writer_release_alloc,
                                  proc->write_resp->walloc->n_media, false);
    for (i = 0; i < proc->write_resp->walloc->n_media; i++) {
        rsc_id_cpy(proc->writer_release_alloc->release->media[i]->med_id,
                   proc->write_resp->walloc->media[i]->med_id);
    }

    return 0;
}

static int common_split_setup(struct pho_data_processor *proc)
{
    struct raid_io_context *io_context =
        &((struct raid_io_context *)
          proc->private_writer)[proc->current_target];
    struct output_io_context *output;
    struct pho_io_descr *iods;
    pho_resp_write_t *wresp;
    size_t n_extents;
    int rc = 0;
    int i;

    output = raid_output_io_context(io_context, proc->type);
    n_extents = get_n_extents(io_context, proc->type);
    wresp = proc->write_resp->walloc;
    iods = io_context->iods;

    if (wresp->n_media != n_extents)
        LOG_RETURN(-EINVAL, "Invalid number of media return by phobosd. "
                   "Expected %lu, got %lu",
                   n_extents, wresp->n_media);

    for (i = 0; i < n_extents; ++i) {
        rc = get_io_adapter((enum fs_type)wresp->media[i]->fs_type,
                            &iods[i].iod_ioa);
        if (rc)
            LOG_RETURN(rc, "Unable to get io_adapter in raid %s",
                       processor_type2str(proc));

        iods[i].iod_size = 0;
        iods[i].iod_flags = PHO_IO_REPLACE | PHO_IO_NO_REUSE;
    }

    raid_io_context_setmd(io_context, proc->type, output->user_md);

    if (proc->type == PHO_PROC_REBUILDER)
        raid_rebuilder_set_extent_info(proc, io_context, wresp->media,
                                       proc->src_layout);
    else
        raid_writer_set_extent_info(io_context, wresp->media,
                                    io_context->current_split * n_extents,
                                    proc->writer_offset);

    rc = raid_io_context_open(io_context, proc, n_extents,
                              proc->current_target, PHO_PROC_ENCODER);
    if (rc)
        return rc;

    if (!io_context->current_split_chunk_size) {
        if (proc->io_block_size)
            io_context->current_split_chunk_size = proc->io_block_size;
        else
            set_current_split_chunk_size(io_context, n_extents);
    }

    return rc;
}

static int raid_writer_split_setup(struct pho_data_processor *proc)
{
    struct raid_io_context *io_context =
        &((struct raid_io_context *)
          proc->private_writer)[proc->current_target];
    size_t extent_size_remainder = 0;
    pho_resp_write_t *wresp;
    size_t extent_size = 0;
    size_t n_extents;
    int rc;
    int i;

    ENTRY;

    rc = common_split_setup(proc);
    if (rc)
        return rc;

    wresp = proc->write_resp->walloc;
    n_extents = n_total_extents(io_context);

    extent_size = (proc->object_size - proc->writer_offset) /
                  io_context->n_data_extents;
    extent_size_remainder = (proc->object_size - proc->writer_offset) %
                            io_context->n_data_extents;
    for (i = 0; i < n_extents; i++) {
        if (wresp->media[i]->avail_size < extent_size) {
            extent_size = wresp->media[i]->avail_size;
            extent_size_remainder = 0;
        }
    }

    proc->writer_stripe_size = io_context->current_split_chunk_size *
                               io_context->n_data_extents;

    /* We check whether extent_size is not smaller than stripe_size. If it
     * isn't, we don't need extent_size to be a multiple of stripe_size.
     * Also, we check if this is the last split to write. If it is, extent_size
     * doesn't need to be a multiple of stripe_size.
     */
    if (extent_size > io_context->current_split_chunk_size &&
        proc->object_size != io_context->current_split_offset +
                                (extent_size * io_context->n_data_extents) +
                                 extent_size_remainder)
        extent_size -= extent_size % io_context->current_split_chunk_size;

    raid_io_context_set_extent_size(io_context, proc->type, extent_size,
                                    extent_size_remainder);

    /* keep a buff.size compliant with new stripe size */
    if (proc->buff.size && proc->buff.size % proc->writer_stripe_size != 0)
        pho_buff_realloc(&proc->buff, lcm(proc->buff.size,
                                          proc->writer_stripe_size));

    for (i = 0; i < io_context->nb_hashes; i++) {
        rc = extent_hash_reset(&io_context->hashes[i]);
        if (rc)
            return rc;
    }

    io_context->current_split_size = 0;
    for (i = 0; i < io_context->n_data_extents; i++)
        io_context->current_split_size +=
            io_context->write.output.extents[i].size;

    return io_context->ops->set_extra_attrs(proc);
}

static void common_writer_rebuilder_split_close(struct pho_data_processor *proc,
                                                struct object_metadata *md,
                                                int *rc)
{
    pho_req_release_t *release_req = proc->writer_release_alloc->release;
    struct raid_io_context *io_context;
    int target = proc->current_target;
    struct output_io_context *output;
    size_t n_extents;
    int i = 0;
    int rc2;

    ENTRY;

    io_context = &((struct raid_io_context *) proc->private_writer)[target];
    output = raid_output_io_context(io_context, proc->type);
    n_extents = get_n_extents(io_context, proc->type);

    /* set extent md */
    if (!*rc) {
        for (i = 0; i < n_extents; i++) {
            struct pho_io_descr *iod = &io_context->iods[i];
            struct pho_ext_loc ext_location;

            /* decrease avail size */
            proc->write_resp->walloc->media[i]->avail_size -= iod->iod_size;

            /* set extent hashes */
            rc2 = extent_hash_digest(&io_context->hashes[i]);
            if (rc2) {
                *rc = rc2;
                break;
            }

            extent_hash_copy(&io_context->hashes[i], &output->extents[i]);

            /* set extent location */
            ext_location.root_path =
                proc->write_resp->walloc->media[i]->root_path;
            ext_location.addr_type =
                proc->write_resp->walloc->media[i]->addr_type;
            ext_location.extent = &output->extents[i];
            iod->iod_loc = &ext_location;
            rc2 = set_object_md(iod->iod_ioa, iod, md);
            pho_attrs_free(&iod->iod_attrs);
            if (rc2) {
                *rc = rc2;
                break;
            }

            rc2 = ioa_close(iod->iod_ioa, iod);
            if (rc2) {
                i++;
                *rc = rc2;
                break;
            }

            rc2 = gettimeofday(&output->extents[i].creation_time, NULL);
            if (rc2) {
                i++;
                *rc = rc2;
                pho_error(*rc,
                          "raid: unable to get ctime of extent %d for '%s'", i,
                          proc->xfer->xd_targets[target].xt_objid);
                break;
            }

            raid_io_add_written_extent(io_context, proc->type,
                                       &output->extents[i]);

            /* update release */
            release_req->media[i]->nb_extents_written += 1;
            release_req->media[i]->size_written += iod->iod_size;
        }
    }

    for (; i < n_extents; i++)
        ioa_close(io_context->iods[i].iod_ioa, &io_context->iods[i]);
}

static void raid_writer_split_close(struct pho_data_processor *proc, int *rc)
{
    int target = proc->current_target;
    struct raid_io_context *io_context =
        &((struct raid_io_context *) proc->private_writer)[target];
    struct object_metadata object_md = {
        .object_attrs = proc->xfer->xd_targets[target].xt_attrs,
        .object_size = proc->xfer->xd_targets[target].xt_size,
        .object_version = proc->xfer->xd_targets[target].xt_version,
        .layout_name = io_context->name,
        .object_uuid = proc->xfer->xd_targets[target].xt_objuuid,
        .copy_name = proc->dest_layout[target].copy_name,
    };

    ENTRY;

    common_writer_rebuilder_split_close(proc, &object_md, rc);

    /* next split */
    if (!*rc) {
        io_context->current_split++;
        io_context->current_split_offset = proc->writer_offset;
    }
}

static void common_fill_layout(struct output_io_context *output,
                               struct layout_info *layout)
{
    layout->ext_count = output->written_extents->len;
    layout->extents = (struct extent *)
        g_array_free(output->written_extents, FALSE);
    output->written_extents = NULL;

    output->n_released_media = 0;

    for (int i = 0; i < layout->ext_count; ++i)
        layout->extents[i].state = PHO_EXT_ST_SYNC;
}

static int raid_writer_handle_partial_release_resp(
    struct pho_data_processor *encoder
)
{
    struct raid_io_context *io_context;
    struct output_io_context *output;
    int rc = 0;
    int i;

    for (i = 0; i < encoder->current_target; i++) {
        io_context =
            &((struct raid_io_context *) encoder->private_writer)[i];

        if (io_context->write.released)
            continue;

        output = raid_output_io_context(io_context, encoder->type);
        common_fill_layout(output, &encoder->dest_layout[i]);

        io_context->write.released = true;
    }

    return rc;
}

static int common_release_media(struct pho_data_processor *proc,
                                pho_resp_release_t *rel_resp)
{
    struct raid_io_context *io_context;
    int rc = 0;
    int i, j;

    for (i = 0; i < rel_resp->n_med_ids; i++) {
        bool found = false;

        pho_debug("Marking medium '%s':'%s' as released",
                  rel_resp->med_ids[i]->name,
                  rel_resp->med_ids[i]->library);
        for (j = 0; j < proc->xfer->xd_ntargets; j++) {
            /* If the media_id is unexpected, -EINVAL will be returned */
            io_context = &((struct raid_io_context *) proc->private_writer)[j];
            mark_written_medium_released(io_context, proc->type,
                                         rel_resp->med_ids[i], &found);
        }

        if (!found)
            pho_error(rc = -EINVAL,
                      "Got a release response for medium '%s':'%s' but it is "
                      "was not in any release list",
                      rel_resp->med_ids[i]->library,
                      rel_resp->med_ids[i]->name);
    }

    return rc;
}

static int raid_writer_handle_release_resp(struct pho_data_processor *encoder,
                                           pho_resp_release_t *rel_resp)
{
    struct raid_io_context *io_context;
    int target_released = 0;
    int rc = 0;
    int i;

    rc = common_release_media(encoder, rel_resp);

    /*
     * If we wrote everything and all the releases have been received, mark the
     * encoder as done.
     */
    for (i = 0; i < encoder->xfer->xd_ntargets; i++) {
        struct output_io_context *output;

        io_context =
            &((struct raid_io_context *) encoder->private_writer)[i];

        if (io_context->write.released) {
            target_released++;
            continue;
        }

        output = raid_output_io_context(io_context, encoder->type);

        if (io_context->write.all_is_written && /* no more data to write */
            /* at least one extent is created, special test for null size put */
            output->written_extents->len > 0 &&
            /* we got releases of all extents */
            output->written_extents->len == output->n_released_media) {

            /* Fill the layout with the extents */
            common_fill_layout(output, &encoder->dest_layout[i]);

            io_context->write.released = true;
            target_released++;
        }
    }

    /* Switch to DONE state */
    if (target_released == encoder->xfer->xd_ntargets) {
        encoder->done = true;
        return 0;
    }

    return rc;
}

static void complete_and_transfer_release(struct pho_data_processor *proc,
                                          int rc, pho_req_t **reqs,
                                          size_t *n_reqs,
                                          bool stop_io)
{
    pho_req_release_t *release_req = proc->writer_release_alloc->release;
    struct pho_xfer_put_params *params = get_put_params(proc);
    int i;

    ENTRY;

    for (i = 0; i < proc->write_resp->walloc->n_media; i++) {
        release_req->media[i]->rc = stop_io ? 0 : rc;
        if (rc || stop_io) {
            release_req->media[i]->to_sync = false;
        } else {
            release_req->media[i]->to_sync = true;
            release_req->media[i]->grouping =
                (char *) xstrdup_safe(params->grouping);
        }
    }

    *reqs = proc->writer_release_alloc;
    proc->writer_release_alloc = NULL;
    (*n_reqs) = 1;
}

int raid_writer_processor_step(struct pho_data_processor *proc,
                               pho_resp_t *resp, pho_req_t **reqs,
                               size_t *n_reqs)
{
    struct raid_io_context *io_context =
        &((struct raid_io_context *)
          proc->private_writer)[proc->current_target];
    size_t all_target_remain_to_write_per_medium = 0;
    bool need_alloc_for_next_target = false;
    bool need_partial_release = false;
    bool need_full_release = false;
    bool last_target_ended = false;
    bool need_new_alloc = false;
    bool target_ended = false;
    bool split_ended = false;
    bool stop_io = false;
    int rc = 0;
    int i;

    ENTRY;

    /* first init step from the data processor: return first allocation */
    if (!resp && !proc->buff.size) {
        rc = xfer_remain_to_write_per_medium(
                 proc, &all_target_remain_to_write_per_medium);
        if (rc)
            goto set_target_rc;

        *reqs = xcalloc(1, sizeof(**reqs));
        *n_reqs = 1;
        raid_writer_rebuilder_build_allocation_req(proc, *reqs,
                                         all_target_remain_to_write_per_medium);
        proc->need_alloc_response_to_write = true;
        goto set_target_rc;
    }

    /* manage error */
    if (resp && pho_response_is_error(resp)) {
        proc->xfer->xd_rc = resp->error->rc;
        proc->done = true;
        LOG_GOTO(set_target_rc, rc = proc->xfer->xd_rc,
                 "%s %d received error %s to last request",
                 processor_type2str(proc), resp->req_id,
                 pho_srl_error_kind_str(resp->error));
    }

    /* manage release */
    if (resp && pho_response_is_release(resp)) {
        if (pho_response_is_partial_release(resp)) {
            rc = raid_writer_handle_partial_release_resp(proc);
            if (rc)
                goto set_target_rc;
        } else {
            rc = raid_writer_handle_release_resp(proc, resp->release);
            goto set_target_rc;
        }
    }

    /* manage received allocation and partial release */
    if (resp) {
        rc = prepare_writer_rebuilder_release_request(proc, resp);
        if (rc)
            goto set_target_rc;

        if (pho_response_is_partial_release(resp)) {
            struct phobos_global_context *context = phobos_context();

            if (context->mocks.mock_failure_after_second_partial_release !=
                    NULL)
                rc = context->mocks.mock_failure_after_second_partial_release();
        } else {
            proc->need_alloc_response_to_write = false;
            rc = raid_writer_split_setup(proc);
        }

        if (rc)
            goto check_for_release;
    }

    if (proc->xfer->xd_targets[proc->current_target].xt_rc != 0) {
        stop_io = true;
        rc = proc->xfer->xd_targets[proc->current_target].xt_rc;
        goto check_for_release;
    }

    /* write */
    if (!proc->buff.size) {
        rc = 0;
        goto set_target_rc;
    }

    rc = io_context->ops->write_from_buff(proc);

    split_ended = (proc->writer_offset - io_context->current_split_offset) >=
                   io_context->current_split_size;
    target_ended = proc->writer_offset == proc->object_size;
    last_target_ended = target_ended &&
                        (proc->current_target + 1) == proc->xfer->xd_ntargets;

    if (!rc && split_ended && !last_target_ended)
        rc = xfer_remain_to_write_per_medium(
                 proc, &all_target_remain_to_write_per_medium);

    if (!rc && target_ended && !last_target_ended) {
        for (i = 0; i < io_context->n_data_extents; i++) {
            if (proc->write_resp->walloc->media[i]->avail_size <
                all_target_remain_to_write_per_medium) {
                need_alloc_for_next_target = true;
                break;
            }
        }
    }

check_for_release:
    need_full_release = rc || ((split_ended && !target_ended) ||
                          need_alloc_for_next_target || last_target_ended);

    need_new_alloc = !rc && ((split_ended && !target_ended) ||
                             need_alloc_for_next_target);

    if (split_ended || rc)
        raid_writer_split_close(proc, &rc);

    /* check if partial release is needed */
    if (!rc && split_ended && !need_full_release)
        need_partial_release = need_to_sync(proc->writer_release_alloc->release,
                                            proc->writer_start_req,
                                            proc->write_resp);

    if (need_full_release || need_partial_release)
        complete_and_transfer_release(proc, rc, reqs, n_reqs, stop_io);

    if (need_partial_release)
        (*reqs)[*n_reqs - 1].release->partial = true;

    if (target_ended) {
        proc->current_target++;
        proc->reader_offset = 0;
        proc->writer_offset = 0;
        proc->buffer_offset = 0;

        /* prepare new-target */
        if (proc->current_target < proc->xfer->xd_ntargets) {
            proc->object_size =
                proc->xfer->xd_targets[proc->current_target].xt_size;

            if (!need_new_alloc)
                rc = raid_writer_split_setup(proc);
        }
    }

    if (need_new_alloc) {
        raid_writer_rebuilder_build_allocation_req(proc, *reqs + *n_reqs,
                                         all_target_remain_to_write_per_medium);
        (*n_reqs)++;
        proc->need_alloc_response_to_write = true;
    }

set_target_rc:
    if (rc) {
        if (proc->xfer->xd_rc == 0)
            proc->xfer->xd_rc = rc;

        for (i = proc->current_target; i < proc->xfer->xd_ntargets; i++)
            proc->xfer->xd_targets[i].xt_rc = rc;
    }

    return rc;
}

static int raid_eraser_handle_release_resp(struct pho_data_processor *proc,
                                           pho_resp_t *resp, pho_req_t **reqs,
                                           size_t *n_reqs)
{
    struct raid_io_context *io_context = proc->private_eraser;
    size_t n_extents = n_total_extents(io_context);

    if (resp->release->n_med_ids !=
        current_split_src_layout_n_extent(proc, PHO_PROC_ERASER))
        LOG_RETURN(-EINVAL,
                   "Eraser release unexpected number of media. Expected %lu, "
                   "got %lu",
                   current_split_src_layout_n_extent(proc, PHO_PROC_ERASER),
                   resp->release->n_med_ids);

    for (int i = 0; i < resp->release->n_med_ids; ++i) {
        ssize_t ext_index;

        ext_index = extent_index(proc->src_layout,
                                 resp->release->med_ids[i],
                                 n_extents * io_context->current_split,
                                 n_extents * (io_context->current_split + 1));
        if (ext_index == -1)
            LOG_RETURN(-ENOMEDIUM,
                       "Did not find in hard delete release resp medium "
                       "'%s':'%s' in eraser layout of '%s'",
                       resp->release->med_ids[i]->library,
                       resp->release->med_ids[i]->name,
                       proc->xfer->xd_targets[proc->current_target].xt_objid);

        io_context->delete.to_delete--;
    }

    if (io_context->delete.to_delete == 0) {
        proc->current_target++;
        io_context->current_split = 0;
    } else {
        io_context->current_split++;
        while (current_split_src_layout_n_extent(proc, PHO_PROC_ERASER) == 0)
            io_context->current_split++;
    }

    if (proc->current_target == proc->xfer->xd_ntargets) {
            proc->done = true;
    } else {
        *reqs = xcalloc(1, sizeof(**reqs));
        raid_reader_eraser_build_allocation_req(proc, *reqs + *n_reqs,
                                                PHO_PROC_ERASER);
        (*n_reqs)++;
    }

    return 0;
}

int raid_eraser_processor_step(struct pho_data_processor *proc,
                               pho_resp_t *resp, pho_req_t **reqs,
                               size_t *n_reqs)
{
    struct raid_io_context *io_context = proc->private_eraser;
    size_t n_extents = n_total_extents(io_context);
    int rc;
    int i;

    /* first init step from the data processor: return first allocation */
    if (!resp) {
        /* move to next split if no extent to delete in current one */
        while (current_split_src_layout_n_extent(proc, PHO_PROC_ERASER) == 0)
            io_context->current_split++;

        *reqs = xcalloc(1, sizeof(**reqs));
        *n_reqs = 1;
        raid_reader_eraser_build_allocation_req(proc, *reqs, PHO_PROC_ERASER);
        return 0;
    }

    /* manage error */
    if (pho_response_is_error(resp)) {
        proc->xfer->xd_rc = resp->error->rc;
        proc->done = true;
        LOG_RETURN(proc->xfer->xd_rc,
                   "%s %d received error %s to last request",
                   processor_type2str(proc), resp->req_id,
                   pho_srl_error_kind_str(resp->error));
    }

    /* manage release */
    if (pho_response_is_release(resp))
        return raid_eraser_handle_release_resp(proc, resp, reqs, n_reqs);

    /* manage read alloc */
    if (!pho_response_is_read(resp)) {
        proc->xfer->xd_rc = -EPROTO;
        proc->done = true;
        LOG_RETURN(proc->xfer->xd_rc,
                   "%s %d received a resp which is not a read alloc",
                   processor_type2str(proc), resp->req_id);
    }

    if (resp->ralloc->n_media !=
        current_split_src_layout_n_extent(proc, PHO_PROC_ERASER))
        LOG_RETURN(-EINVAL, "Eraser unexpected number of media. Expected %lu, "
                   "got %lu",
                   current_split_src_layout_n_extent(proc, PHO_PROC_ERASER),
                   resp->ralloc->n_media);

    /* prepare release req with potential next alloc */
    *reqs = xcalloc(1, sizeof(**reqs));
    pho_srl_request_release_alloc(*reqs + *n_reqs, resp->ralloc->n_media, true);
    for (i = 0; i < resp->ralloc->n_media; ++i)
        rsc_id_cpy((*reqs)[*n_reqs].release->media[i]->med_id,
                   resp->ralloc->media[i]->med_id);

    /* delete extent */
    io_context->delete.resp = resp->ralloc;
    for (i = 0; i < resp->ralloc->n_media; ++i) {
        struct pho_io_descr iod = {0};
        struct pho_ext_loc loc;
        ssize_t ext_index;

        rc = get_io_adapter((enum fs_type)resp->ralloc->media[i]->fs_type,
                            &iod.iod_ioa);
        if (rc)
            break;

        ext_index = extent_index(proc->src_layout,
                                 resp->ralloc->media[i]->med_id,
                                 n_extents * io_context->current_split,
                                 n_extents * (io_context->current_split + 1));
        if (ext_index == -1) {
            pho_error(rc = -ENOMEDIUM,
                      "Did not find in hard delete alloc resp medium '%s':'%s' "
                      "in eraser layout of '%s'",
                      resp->ralloc->media[i]->med_id->library,
                      resp->ralloc->media[i]->med_id->name,
                      proc->xfer->xd_targets[proc->current_target].xt_objid);
            break;
        }

        loc = make_ext_location(proc, i, ext_index, proc->current_target,
                                PHO_PROC_ERASER);
        iod.iod_loc = &loc;
        rc = ioa_del(iod.iod_ioa, &iod);
        if (rc)
            break;

        ioa_close(iod.iod_ioa, &iod);
        (*reqs)[*n_reqs].release->media[i]->size_written =
            -proc->src_layout->extents[ext_index].size;
        (*reqs)[*n_reqs].release->media[i]->nb_extents_written = -1;
        (*reqs)[*n_reqs].release->media[i]->to_sync = true;
    }

    if (rc)
        for (i = 0; i < resp->ralloc->n_media; ++i) {
            (*reqs)[*n_reqs].release->media[i]->rc = rc;
            (*reqs)[*n_reqs].release->media[i]->to_sync = false;
            (*reqs)[*n_reqs].release->media[i]->size_written = 0;
            (*reqs)[*n_reqs].release->media[i]->nb_extents_written = 0;
        }

    (*n_reqs)++;

    return rc;
}

static int raid_rebuilder_split_setup(struct pho_data_processor *proc)
{
    struct raid_io_context *io_context = proc->private_writer;
    int rc;

    ENTRY;

    rc = common_split_setup(proc);
    if (rc)
        return rc;

    raid_io_context_set_extent_size(
                                io_context, proc->type,
                                io_context->rebuild.current_split_extent_size,
                                io_context->rebuild.extent_remainder);

    proc->writer_stripe_size = io_context->current_split_chunk_size;

    if (proc->buff.size && proc->buff.size % proc->writer_stripe_size != 0)
        pho_buff_realloc(&proc->buff, lcm(proc->buff.size,
                                          proc->writer_stripe_size));

    for (int i = 0; i < io_context->nb_hashes; i++) {
        rc = extent_hash_reset(&io_context->hashes[i]);
        if (rc)
            return rc;
    }

    return io_context->ops->set_extra_attrs(proc);
}

static void raid_rebuilder_split_close(struct pho_data_processor *proc, int *rc)
{
    struct raid_io_context *io_context = proc->private_writer;
    struct object_metadata object_md = {
        .object_attrs = proc->xfer->xd_targets->xt_attrs,
        .object_size = proc->object_size,
        .object_version = proc->src_layout->version,
        .layout_name = io_context->name,
        .object_uuid = proc->src_layout->uuid,
        .copy_name = proc->src_layout->copy_name,
    };

    ENTRY;

    common_writer_rebuilder_split_close(proc, &object_md, rc);

    /* next split */
    if (!*rc) {
        io_context->current_split++;
        io_context->rebuild.current_split_missing_count = 0;
    }
}

static int raid_rebuilder_handle_release_resp(struct pho_data_processor *proc,
                                              pho_resp_release_t *resp)
{
    struct raid_io_context *io_context = proc->private_writer;
    struct output_io_context *output;
    int rc = 0;

    rc = common_release_media(proc, resp);

    output = raid_output_io_context(io_context, proc->type);

    if (io_context->rebuild.missing_extents_remaining == 0 &&
        output->written_extents->len > 0 &&
        output->written_extents->len == output->n_released_media) {

        common_fill_layout(output, proc->dest_layout);
        proc->done = true;
    }

    return rc;
}

static size_t raid_get_current_split_size(struct pho_data_processor *proc)
{
    struct raid_io_context *io_context = proc->private_writer;
    size_t n_extents_per_split = n_total_extents(io_context);
    struct layout_info *layout = proc->src_layout;
    size_t next_split_first_layout_idx;
    size_t remaining;
    int i;

    next_split_first_layout_idx = (io_context->current_split + 1) *
                                    n_extents_per_split;

    remaining = proc->object_size - io_context->current_split_offset;

    for (i = 0; i < layout->ext_count; i++) {
        struct extent *extent = &layout->extents[i];

        if (extent->layout_idx < next_split_first_layout_idx)
            continue;

        return min(extent->offset - io_context->current_split_offset,
                   remaining);
    }

    return remaining;
}

static void rebuilder_get_extent_size(struct pho_data_processor *proc)
{
    struct raid_io_context *io_context = proc->private_writer;
    size_t extent_size;
    size_t remainder;

    io_context->current_split_size = raid_get_current_split_size(proc);
    extent_size = io_context->current_split_size / io_context->n_data_extents;
    remainder = io_context->current_split_size % io_context->n_data_extents;

    io_context->rebuild.current_split_extent_size = extent_size;
    io_context->rebuild.extent_remainder = remainder;
}

int raid_rebuilder_processor_step(struct pho_data_processor *proc,
                                  pho_resp_t *resp, pho_req_t **reqs,
                                  size_t *n_reqs)
{
    struct raid_io_context *io_context = proc->private_writer;
    bool need_full_release = false;
    bool need_new_alloc = false;
    bool split_ended = false;
    bool stop_io = false;
    int rc = 0;

    ENTRY;

    /* First init step from the data processor: return first allocation */
    if (!resp && !proc->buff.size) {
        if (!raid_seek_next_missing_split(proc, PHO_PROC_ENCODER, true)) {
            proc->done = true;
            return 0;
        }

        rebuilder_get_extent_size(proc);

        *reqs = xcalloc(1, sizeof(**reqs));
        *n_reqs = 1;
        raid_writer_rebuilder_build_allocation_req(proc, *reqs,
                                 io_context->rebuild.current_split_extent_size +
                                 io_context->rebuild.extent_remainder);

        goto set_target_rc;
    }

    /* manage error */
    if (resp && pho_response_is_error(resp)) {
        proc->done = true;
        LOG_GOTO(set_target_rc, rc = resp->error->rc,
                 "%s %d received error %s to last request",
                 processor_type2str(proc), resp->req_id,
                 pho_srl_error_kind_str(resp->error));
    }

    /* manage release */
    if (resp && pho_response_is_release(resp)) {
        rc = raid_rebuilder_handle_release_resp(proc, resp->release);
        goto set_target_rc;
    }

    /* manage received allocation */
    if (resp) {
        rc = prepare_writer_rebuilder_release_request(proc, resp);
        if (rc)
            goto set_target_rc;

        proc->need_alloc_response_to_write = false;
        rc = raid_rebuilder_split_setup(proc);
        if (rc)
            goto check_for_release;
    }

    if (proc->xfer->xd_targets[proc->current_target].xt_rc != 0) {
        stop_io = true;
        rc = proc->xfer->xd_targets[proc->current_target].xt_rc;
        goto check_for_release;
    }

    /* write */
    if (!proc->buff.size) {
        rc = 0;
        goto set_target_rc;
    }

    rc = io_context->ops->rebuild_from_buff(proc);

    split_ended = (proc->writer_offset - io_context->current_split_offset) >=
                    io_context->current_split_size;
    if (split_ended)
        io_context->rebuild.missing_extents_remaining -=
            io_context->rebuild.current_split_missing_count;

    if (split_ended || rc)
        raid_rebuilder_split_close(proc, &rc);

check_for_release:
    need_full_release = rc || split_ended;
    need_new_alloc = !rc && split_ended &&
                     raid_seek_next_missing_split(proc, PHO_PROC_ENCODER,
                                                  true);

    if (need_full_release)
        complete_and_transfer_release(proc, rc, reqs, n_reqs, stop_io);

    if (need_new_alloc) {
        rebuilder_get_extent_size(proc);
        raid_writer_rebuilder_build_allocation_req(proc, *reqs + *n_reqs,
                                io_context->rebuild.current_split_extent_size +
                                io_context->rebuild.extent_remainder);
        (*n_reqs)++;
        proc->need_alloc_response_to_write = true;
    }

set_target_rc:
    if (rc) {
        if (proc->xfer->xd_rc == 0)
            proc->xfer->xd_rc = rc;
        proc->xfer->xd_targets->xt_rc = rc;
    }

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

void extent_hash_copy(struct extent_hash *hash, struct extent *extent)
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

int get_object_size_from_layout(struct layout_info *layout)
{
    const char *buffer;
    int object_size;

    buffer = pho_attr_get(&layout->layout_desc.mod_attrs,
                          PHO_EA_OBJECT_SIZE_NAME);
    if (buffer == NULL)
        LOG_RETURN(-EINVAL, "Failed to get object size of object '%s'",
                   layout->oid);

    object_size = str2int64(buffer);
    if (object_size < 0)
        LOG_RETURN(-EINVAL, "Failed to convert '%s' to size for object '%s'",
                   layout->oid, buffer);

    return object_size;
}
