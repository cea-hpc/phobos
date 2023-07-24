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

static void free_extent_address_buff(void *void_extent)
{
    struct extent *extent = void_extent;

    free(extent->address.buff);
}

static struct raid_ops RAID4_OPS = {
    .write      = multiple_enc_write_chunk,
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

static const struct pho_layout_module_ops LAYOUT_RAID4_OPS = {
    .encode = layout_raid4_encode,
    .decode = NULL,
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
