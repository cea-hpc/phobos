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

#include "raid_common.h"
#include <errno.h>
#include <glib.h>
#include <string.h>
#include <unistd.h>

#include "pho_attrs.h"
#include "pho_cfg.h"
#include "pho_common.h"
#include "pho_dss.h"
#include "pho_io.h"
#include "pho_layout.h"
#include "pho_module_loader.h"
#include "pho_srl_common.h"
#include "pho_type_utils.h"

#define PLUGIN_NAME     "raid4"
#define PLUGIN_MAJOR    0
#define PLUGIN_MINOR    1

static struct module_desc RAID4_MODULE_DESC = {
    .mod_name  = PLUGIN_NAME,
    .mod_major = PLUGIN_MAJOR,
    .mod_minor = PLUGIN_MINOR,
};

static void buffer_xor(char *buff_1, char *buff_2, char *buff_xor,
                       size_t to_write)
{
    size_t i;

    for (i = 0; i < to_write; i++)
        buff_xor[i] = buff_1[i] ^ buff_2[i];
}

static int read_buffer(int input_fd, char *buffer, size_t read_size,
                       ssize_t *bytes_read, size_t *left_to_read,
                       bool *write_finished)
{
    int rc = 0;

    while (*bytes_read < read_size) {
        rc = read(input_fd, buffer + *bytes_read,
                  read_size - *bytes_read);
        if (rc < 0)
            LOG_RETURN(rc = -errno, "Error on loading buffer in raid4 write");

        *bytes_read += rc;
        *left_to_read -= rc;
        if (rc == 0 || *left_to_read == 0) {
            *write_finished = true;
            break;
        }
    }

    return rc;
}

static int write_split(int input_fd, struct raid_io_context *io_context,
                       size_t buffer_size, size_t split_size)
{
    size_t left_to_read = split_size * 2;
    size_t read_size = buffer_size;
    bool write_finished = false;
    int rc = 0;

    while (!write_finished) {
        ssize_t bytes_read1 = 0;
        ssize_t bytes_read2 = 0;

        if (left_to_read < read_size)
            /* split the size over the 2 extents otherwise, one extent will
             * exceed the size allocated by the LRS
             */
            read_size = (left_to_read + 1) / 2;

        rc = read_buffer(input_fd, io_context->parts[0], read_size,
                         &bytes_read1, &left_to_read, &write_finished);
        if (rc < 0)
            goto out;

        rc = read_buffer(input_fd, io_context->parts[1], read_size,
                         &bytes_read2, &left_to_read, &write_finished);
        if (rc < 0)
            goto out;

        rc = ioa_write(io_context->ioa[0], &io_context->iod[0],
                       io_context->parts[0], bytes_read1);
        if (rc)
            LOG_GOTO(out, rc, "Unable to write %zu bytes in raid4 write",
                     bytes_read1);

        io_context->iod[0].iod_size += bytes_read1;

        rc = ioa_write(io_context->ioa[1], &io_context->iod[1],
                       io_context->parts[1], bytes_read2);
        if (rc)
            LOG_GOTO(out, rc, "Unable to write %zu bytes in raid4 write",
                     bytes_read2);

        io_context->iod[1].iod_size += bytes_read2;

        /* Add 0 padding at the end of the second buffer to match the size of
         * the first one for the last xor.
         */
        if (write_finished)
            memset(io_context->parts[1] + bytes_read2, 0,
                   bytes_read1 - bytes_read2);

        buffer_xor(io_context->parts[0], io_context->parts[1],
                   io_context->parts[2], bytes_read1);

        rc = ioa_write(io_context->ioa[2], &io_context->iod[2],
                       io_context->parts[2], bytes_read1);
        if (rc)
            LOG_GOTO(out, rc, "Unable to write %zu bytes in raid4 write",
                     bytes_read1);

        io_context->iod[2].iod_size += bytes_read1;
    }

out:
    return rc;
}

static size_t find_maximum_split_size(pho_resp_write_t *wresp, size_t to_write)
{
    size_t extent_size = to_write;
    int i;

    for (i = 0; i < wresp->n_media; ++i) {
        if (wresp->media[i]->avail_size < extent_size)
            extent_size = wresp->media[i]->avail_size;
    }

    return extent_size;
}

static int multiple_enc_write_chunk(struct pho_encoder *enc,
                                    pho_resp_write_t *wresp,
                                    pho_req_release_t *rreq)
{
    struct raid_io_context *io_context = enc->priv_enc;
    size_t extent_size;
    size_t buffer_size;
    int rc = 0;
    int i;

    if (enc->xfer->xd_fd < 0)
        LOG_RETURN(-EBADF, "Invalid encoder xfer file descriptor in write "
                            "raid4 encoder");

    extent_size = find_maximum_split_size(wresp, io_context->to_write);

    /* set io_context (iod, ioa, extent, ext_location, buffers, extent_tag) */
    rc = raid_io_context_write_init(enc, wresp, &buffer_size, extent_size);
    if (rc)
        return rc;

    for (i = 0; i < io_context->repl_count; i++)
        io_context->iod[i].iod_flags = PHO_IO_REPLACE | PHO_IO_NO_REUSE;

    /* write all extents by chunk of buffer size*/
    rc = write_split(enc->xfer->xd_fd, io_context, buffer_size, extent_size);

    for (i = 0; i < io_context->repl_count; ++i) {
        int rc2;

        rc2 = ioa_close(io_context->ioa[i], &io_context->iod[i]);
        if (!rc && rc2)
            rc = rc2;
    }

    /* update size in write encoder */
    if (!rc) {
        io_context->to_write -= extent_size;
        io_context->cur_extent_idx++;
    }

    /* update all release requests */
    for (i = 0; i < io_context->repl_count; ++i) {
        rreq->media[i]->rc = rc;
        rreq->media[i]->size_written = io_context->iod[i].iod_size;
    }

    /* add all written extents */
    if (!rc) {
        for (i = 0; i < io_context->repl_count; ++i)
            raid_io_add_written_extent(io_context, &io_context->extent[i]);
    }

    for (i = 0; i < io_context->repl_count; ++i)
        pho_attrs_free(&io_context->iod[i].iod_attrs);

    raid_io_context_fini(io_context);
    return rc;
}

static int write_in_file(int tgt_fd, char *buffer, size_t to_write)
{
    ssize_t written = 0;
    ssize_t rc;

    while (written < to_write) {
        rc = write(tgt_fd, buffer + written, to_write - written);
        if (rc < 0)
            LOG_RETURN(-errno, "Failed to write in file");

        written += rc;
    }

   return 0;
}

static int find_medium(struct extent *extents, size_t ext_count,
                       const pho_resp_read_elt_t *medium)
{
    int i;

    for (i = 0; i < ext_count; i++) {
        if (!strcmp(extents[i].media.name, medium->med_id->name))
            return i;
    }

    return -1;
}

static int find_chosen_extents(struct raid_io_context *io_context,
                               struct pho_encoder *dec,
                               const pho_resp_read_elt_t *medium1,
                               const pho_resp_read_elt_t *medium2,
                               struct extent *extents_to_use[],
                               bool chosen_extent[])
{
    int extent_count = 0;
    int ext_index1;
    int ext_index2;
    int i;

    ext_index1 = find_medium(dec->layout->extents, dec->layout->ext_count,
                             medium1);
    if (ext_index1 == -1)
        LOG_RETURN(-ENOMEDIUM, "Could not find medium '%s' in layout",
                   medium1->med_id->name);

    ext_index2 = find_medium(dec->layout->extents, dec->layout->ext_count,
                             medium2);
    if (ext_index2 == -1)
        LOG_RETURN(-ENOMEDIUM, "Could not find medium '%s' in layout",
                   medium2->med_id->name);

    /* make sure that extents_to_use is ordered */
    if (ext_index1 > ext_index2)
        swap(ext_index1, ext_index2);

    extents_to_use[0] = &dec->layout->extents[ext_index1];
    extents_to_use[1] = &dec->layout->extents[ext_index2];
    chosen_extent[ext_index1 % 3] = true;
    chosen_extent[ext_index2 % 3] = true;

    /* check if the number of chosen extents is correct */
    for (i = 0; i < io_context->repl_count; i++) {
        if (chosen_extent[i])
            extent_count++;
    }

    if (extent_count != 2)
        LOG_RETURN(-EINVAL, "raid4 layout received an incorrect number of "
                            "medium to read not in layout extents list");
    return 0;
}

static int open_file(struct raid_io_context *io_context,
                     struct pho_encoder *dec, struct extent *extent,
                     const pho_resp_read_elt_t *medium, int file_index)
{
    int rc;

    io_context->ext_location[file_index].root_path = medium->root_path;
    io_context->ext_location[file_index].extent = extent;
    io_context->ext_location[file_index].addr_type =
                                    (enum address_type)medium->addr_type;
    io_context->iod[file_index].iod_fd = dec->xfer->xd_fd;
    if (io_context->iod[file_index].iod_fd < 0)
        LOG_RETURN(rc = -EBADF, "Invalid decoder xfer file descriptor");

    io_context->iod[file_index].iod_size =
                            io_context->ext_location[file_index].extent->size;
    io_context->iod[file_index].iod_loc = &io_context->ext_location[file_index];

    pho_debug("Reading %ld bytes from medium %s", extent->size,
              extent->media.name);

    rc = ioa_open(*io_context->ioa, dec->xfer->xd_objuuid,
                  &io_context->iod[file_index], false);
    if (rc)
        LOG_RETURN(rc, "failed to open I/O descriptor for '%s'",
                   dec->xfer->xd_objuuid);

    return rc;
}

static int write_buffer(int fd, char *buffer, size_t size)
{
    char *iter = buffer;

    while (size > 0) {
        ssize_t written;

        written = write(fd, iter, size);
        if (written < 0)
            LOG_RETURN(-errno, "Failed to read file");

        iter += written;
        size -= written;
    }

    return 0;
}

static int write_with_xor(struct pho_encoder *dec,
                          struct pho_io_descr *iod1,
                          struct extent *extent1,
                          struct pho_io_descr *iod2,
                          struct extent *extent2,
                          bool second_part_missing)
{
    struct raid_io_context *io_context = dec->priv_enc;
    size_t buf_size = dec->io_block_size;
    size_t written = 0;
    size_t split_size;
    int rc;

    ENTRY;

    // FIXME this should be part1 size + part2 size
    split_size = extent1->size + extent2->size;

    io_context->parts[0] = xmalloc(dec->io_block_size);
    io_context->parts[1] = xmalloc(dec->io_block_size);
    io_context->parts[2] = xmalloc(dec->io_block_size);

    while (true) {
        size_t part1_size;
        size_t part2_size;

        part1_size = ioa_read(*io_context->ioa, iod1, io_context->parts[0],
                              buf_size);
        if (part1_size < 0)
            LOG_RETURN(-errno, "Failed to read file");

        // XOR
        part2_size = ioa_read(*io_context->ioa, iod2, io_context->parts[1],
                              buf_size);
        if (part2_size < 0)
            LOG_RETURN(-errno, "Failed to read file");

        if (part1_size != part2_size) {
            /* Since the xor is always associated with iod2 and each buffer is
             * padded during put, the xor should always be a multiple of the
             * buffer size.
             */
            assert(part1_size < part2_size);
            memset(io_context->parts[0] + part1_size, 0,
                   part2_size - part1_size);
        }

        buffer_xor(io_context->parts[0], io_context->parts[1],
                   io_context->parts[2], buf_size);

        // FIXME rename parts, this is confusing. They are just buffers here
        rc = write_buffer(dec->xfer->xd_fd,
                          second_part_missing ?
                              io_context->parts[0] :
                              io_context->parts[2],
                          second_part_missing ?
                              part1_size :
                              part2_size);
        if (rc)
            return rc;

        written += second_part_missing ?  part1_size : part2_size;
        rc = write_buffer(dec->xfer->xd_fd,
                          second_part_missing ?
                              io_context->parts[2] :
                              io_context->parts[0],
                          second_part_missing ?
                              min(part1_size, split_size - written) :
                              part1_size);
        if (rc)
            return rc;

        written += second_part_missing ?
            min(part1_size, split_size - written) :
            part1_size;

        if (written >= split_size)
            break;
    }

    free(io_context->parts[0]);
    free(io_context->parts[1]);
    free(io_context->parts[2]);

    return 0;
}

static int write_without_xor(struct pho_encoder *dec,
                             struct pho_io_descr *iod1,
                             struct pho_io_descr *iod2)
{
    struct raid_io_context *io_context = dec->priv_enc;
    size_t written = 0;
    size_t data_read;
    size_t read_size;
    size_t to_write;
    int rc = 0;

    ENTRY;

    to_write = io_context->ext_location[0].extent->size;
    read_size = dec->io_block_size;

    io_context->parts[0] = malloc(dec->io_block_size);
    if (io_context->parts[0] == NULL)
        LOG_RETURN(-ENOMEM, "Unable to alloc buffer for raid4 encoder write");

    while (written < to_write) {

        data_read = ioa_read(*io_context->ioa, iod1, io_context->parts[0],
                             read_size);
        if (data_read < 0)
            LOG_GOTO(out_free, -errno, "Failed to read file");

        rc = write_in_file(dec->xfer->xd_fd, io_context->parts[0],
                           data_read);
        if (rc < 0)
            LOG_GOTO(out_free, -errno, "Failed to write in file");

        data_read = ioa_read(*io_context->ioa, iod2, io_context->parts[0],
                             read_size);
        if (data_read < 0)
            LOG_GOTO(out_free, -errno, "Failed to read file");

        rc = write_in_file(dec->xfer->xd_fd, io_context->parts[0], data_read);
        if (rc < 0)
            LOG_GOTO(out_free, -errno, "Failed to write in file");

        written += read_size;
    }

out_free:
    free(io_context->parts[0]);

    return rc;
}

/**
 * Read the data specified by \a extent from \a medium into the output fd of
 * dec->xfer.
 */
static int simple_dec_read_chunk(struct pho_encoder *dec,
                                 pho_resp_read_elt_t **medium)
{
    struct raid_io_context *io_context = dec->priv_enc;
    bool chosen_extent[3] = {false, false, false};
    struct extent *extents_to_use[2] = {NULL};
    int rc;

    rc = raid_io_context_read_init(dec, medium);
    if (rc)
        return rc;

    /* find two good extent among all three available extents */
    rc = find_chosen_extents(io_context, dec, medium[0], medium[1],
                             extents_to_use, chosen_extent);
    if (rc)
        return rc;

    /* open first file */
    rc = open_file(io_context, dec, extents_to_use[0], medium[0], 0);
    if (rc)
        return rc;

    /* open second file */
    rc = open_file(io_context, dec, extents_to_use[1], medium[1], 1);
    if (rc)
        return rc;

    dec->io_block_size = ioa_preferred_io_size(io_context->ioa[0],
                                               &io_context->iod[0]);

    if (io_context->ext_location[0].extent->size < dec->io_block_size)
        dec->io_block_size = io_context->ext_location[0].extent->size;

    /* main writing loop: writing process depends on the chosen extents */
    if (chosen_extent[0] && chosen_extent[1])
        rc = write_without_xor(dec, &io_context->iod[0], &io_context->iod[1]);
    else if (chosen_extent[0] && chosen_extent[2])
        rc = write_with_xor(dec,
                            &io_context->iod[0],
                            extents_to_use[0],
                            &io_context->iod[1],
                            extents_to_use[1],
                            true);
    else if (chosen_extent[1] && chosen_extent[2])
        rc = write_with_xor(dec,
                            &io_context->iod[0],
                            extents_to_use[0],
                            &io_context->iod[1],
                            extents_to_use[1],
                            false);

    free(io_context->ioa);

    if (rc == 0) {
        io_context->to_write -= extents_to_use[0]->size;
        io_context->cur_extent_idx++;
    }

    /* Nothing more to write: the decoder is done */
    if (io_context->to_write <= 0) {
        pho_debug("Decoder for '%s' is now done", dec->xfer->xd_objid);
        dec->done = true;
    }

    return rc;
}

static void free_extent_address_buff(void *void_extent)
{
    struct extent *extent = void_extent;

    free(extent->address.buff);
}

static struct raid_ops RAID4_OPS = {
    .write      = multiple_enc_write_chunk,
    .read       = simple_dec_read_chunk,
};

static const struct pho_enc_ops RAID4_ENCODER_OPS = {
    .step       = raid_encoder_step,
    .destroy    = raid_encoder_destroy,
};

static int layout_raid4_encode(struct pho_encoder *enc)
{
    struct raid_io_context *io_context = xcalloc(1, sizeof(*io_context));

    enc->ops = &RAID4_ENCODER_OPS;
    enc->priv_enc = io_context;

    io_context->ops = &RAID4_OPS;
    io_context->layout_number = 5;
    io_context->cur_extent_idx = 0;
    io_context->requested_alloc = false;

    enc->layout->layout_desc = RAID4_MODULE_DESC;

    /* set write size */
    if (enc->xfer->xd_params.put.size < 0) {
        free(io_context);
        LOG_RETURN(-EINVAL, "bad input encoder size to write when building "
                   "raid_io_context");
    }

    io_context->to_write = (enc->xfer->xd_params.put.size + 1) / 2;

    /* Allocate the extent array */
    io_context->written_extents = g_array_new(FALSE, TRUE,
                                              sizeof(struct extent));
    g_array_set_clear_func(io_context->written_extents,
                           free_extent_address_buff);
    io_context->to_release_media = g_hash_table_new_full(g_str_hash,
                                                         g_str_equal, free,
                                                         free);
    io_context->n_released_media = 0;
    io_context->repl_count = 3;

    /* Empty PUT does not need any IO */
    if (io_context->to_write == 0)
        enc->done = true;

    if (enc->xfer->xd_fd < 0) {
        free(io_context->written_extents);
        free(io_context->to_release_media);
        free(io_context);
        LOG_RETURN(-EBADF, "Invalid encoder xfer file descriptor in empty "
                               "PUT decode create");
    }

    return 0;
}

static int layout_raid4_decode(struct pho_encoder *dec)
{
    struct raid_io_context *io_context;
    int i;

    if (!dec->is_decoder)
        LOG_RETURN(-EINVAL, "ask to create a decoder on an encoder");

    io_context = xcalloc(1, sizeof(*io_context));

    /*
     * The ops field is set early to allow the caller to call the destroy
     * function on error.
     */
    dec->ops = &RAID4_ENCODER_OPS;
    dec->priv_enc = io_context;

    /* Initialize raid4-specific state */
    io_context->ops = &RAID4_OPS;
    io_context->cur_extent_idx = 0;
    io_context->requested_alloc = false;
    io_context->written_extents = NULL;
    io_context->to_release_media = NULL;
    io_context->n_released_media = 0;
    io_context->to_write = 0;
    io_context->nb_needed_media = 2;
    io_context->repl_count = 3;

    /* Fill out the encoder appropriately */

    /*
     * Size is the sum of the extent sizes, dec->layout->wr_size is not
     * positioned properly by the dss
     */
    if (dec->layout->ext_count % 3 != 0)
        LOG_RETURN(-EINVAL, "layout extents count (%d) is not a multiple of 3",
                  dec->layout->ext_count);

    /* set read size : badly named "to_write" */
    for (i = 0; i < dec->layout->ext_count; i = i + 3)
        io_context->to_write += dec->layout->extents[i].size;

    /* Empty GET does not need any IO */
    if (io_context->to_write == 0) {
        dec->done = true;
        if (dec->xfer->xd_fd < 0)
            LOG_RETURN(-EBADF, "Invalid encoder xfer file descriptor in empty "
                               "GET decode create");
    }

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
