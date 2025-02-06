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
 * \brief  Phobos Raid4 Layout plugin
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "raid4.h"

#include <unistd.h>

static int set_extent_extra_attrs(struct extent *extent,
                                  struct pho_io_descr *iod,
                                  size_t chunk_size)
{
    char buff[64];
    int rc;

    rc = sprintf(buff, "%lu", chunk_size);
    if (rc < 0)
        LOG_RETURN(rc = -errno, "Unable to convert extent index to string");

    pho_attr_set(&extent->info, "raid4.chunk_size", buff);
    pho_attr_set(&iod->iod_attrs, "raid4.chunk_size", buff);

    return 0;
}

static int set_raid4_md(struct raid_io_context *io_context, size_t chunk_size)
{
    struct extent *extents = io_context->write.extents;
    size_t n_extents = io_context->n_data_extents +
        io_context->n_parity_extents;
    struct pho_io_descr *iods = io_context->iods;
    int rc = 0;
    size_t i;
    int rc2;

    for (i = 0; i < n_extents; i++) {
        rc2 = set_extent_extra_attrs(&extents[i], &iods[i], chunk_size);
        rc = rc ? : rc2;
    }

    return rc;
}

int raid4_write_split(struct pho_data_processor *encoder, size_t split_size,
                      int target_idx)
{
    struct raid_io_context *io_context =
        &((struct raid_io_context *) encoder->private_processor)[target_idx];
    struct pho_io_descr *posix = &io_context->posix;
    size_t buf_size = io_context->buffers[0].size;
    struct pho_io_descr *iods = io_context->iods;
    size_t left_to_read;
    bool eof = false;
    int rc = 0;
    int i;

    ENTRY;

    left_to_read = min(split_size * 2, io_context->write.to_write);

    rc = set_raid4_md(io_context, buf_size);
    if (rc)
        return rc;

    while (!eof) {
        ssize_t bytes_read1 = 0;
        ssize_t bytes_read2 = 0;

        if (left_to_read < 2 * buf_size)
            /* split the size over the 2 extents otherwise, one extent will
             * exceed the size allocated by the LRS
             */
            buf_size = (left_to_read + 1) / 2;

        bytes_read1 = ioa_read(posix->iod_ioa, posix,
                               io_context->buffers[0].buff,
                               buf_size);
        if (bytes_read1 < 0)
            goto out;

        bytes_read2 = ioa_read(posix->iod_ioa, posix,
                               io_context->buffers[1].buff,
                               buf_size);
        if (bytes_read2 < 0)
            goto out;

        left_to_read -= bytes_read1;
        left_to_read -= bytes_read2;
        eof = (left_to_read == 0);

        rc = ioa_write(iods[0].iod_ioa, &iods[0], io_context->buffers[0].buff,
                       bytes_read1);
        if (rc)
            LOG_GOTO(out, rc, "Unable to write %zu bytes in raid4 write",
                     bytes_read1);

        rc = extent_hash_update(&io_context->hashes[0],
                                io_context->buffers[0].buff,
                                bytes_read1);
        if (rc)
            return rc;

        iods[0].iod_size += bytes_read1;

        rc = ioa_write(iods[1].iod_ioa, &iods[1], io_context->buffers[1].buff,
                       bytes_read2);
        if (rc)
            LOG_GOTO(out, rc, "Unable to write %zu bytes in raid4 write",
                     bytes_read2);

        rc = extent_hash_update(&io_context->hashes[1],
                                io_context->buffers[1].buff,
                                bytes_read2);
        if (rc)
            return rc;

        iods[1].iod_size += bytes_read2;

        /* Add 0 padding at the end of the second buffer to match the size of
         * the first one for the last xor.
         */
        if (eof)
            memset(io_context->buffers[1].buff + bytes_read2, 0,
                   bytes_read1 - bytes_read2);

        buffer_xor(&io_context->buffers[0], &io_context->buffers[1],
                   &io_context->buffers[2], bytes_read1);

        rc = ioa_write(iods[2].iod_ioa, &iods[2], io_context->buffers[2].buff,
                       bytes_read1);
        if (rc)
            LOG_GOTO(out, rc, "Unable to write %zu bytes in raid4 write",
                     bytes_read1);

        rc = extent_hash_update(&io_context->hashes[2],
                                io_context->buffers[2].buff,
                                bytes_read1);
        if (rc)
            return rc;

        iods[2].iod_size += bytes_read1;

    }

    for (i = 0; i < io_context->nb_hashes; i++) {
        rc = extent_hash_digest(&io_context->hashes[i]);
        if (rc)
            return rc;

        rc = extent_hash_copy(&io_context->hashes[i],
                              &io_context->write.extents[i]);
        if (rc)
            return rc;
    }

out:
    return rc;
}

int raid4_write_from_buff(struct pho_data_processor *proc)
{
    struct raid_io_context *io_context =
        (struct raid_io_context *)proc->private_reader;
    size_t inside_split_offset = proc->writer_offset -
                                 io_context->current_split_offset;
    struct pho_io_descr *iods = io_context->iods;
    size_t split_size = io_context->write.extents[0].size +
                        io_context->write.extents[1].size;
    char *buff_start = proc->buff.buff +
                       (proc->writer_offset - proc->buffer_offset);
    size_t to_write;
    int rc;

    /* limit: split -> buffer */
    to_write = min(split_size - inside_split_offset,
                   proc->reader_offset - proc->writer_offset);

    /* write chunk by chunk */
    while (to_write) {
        size_t to_write_extent_0;
        size_t to_write_extent_1;

        /* add chunk level to the limit of extent 0*/
        to_write_extent_0 = min(to_write, io_context->current_split_chunk_size);

        /* write the data extent 0 */
        rc = ioa_write(iods[0].iod_ioa, &iods[0], buff_start,
                       to_write_extent_0);
        if (rc)
            LOG_RETURN(rc,
                       "raid4 unable to write %zu bytes in data extent 0 at "
                       "offset %zu", to_write_extent_0, proc->writer_offset);

        iods[0].iod_size += to_write_extent_0;
        proc->writer_offset += to_write_extent_0;
        rc = extent_hash_update(&io_context->hashes[0], buff_start,
                                to_write_extent_0);
        if (rc)
            return rc;

        to_write -= to_write_extent_0;

        /* add chunk level to the limit of extent 1*/
        to_write_extent_1 = min(to_write, io_context->current_split_chunk_size);

        /* write the data extent 1 */
        rc = ioa_write(iods[1].iod_ioa, &iods[1],
                       buff_start + to_write_extent_0, to_write_extent_1);
        if (rc)
            LOG_RETURN(rc,
                       "raid4 unable to write %zu bytes in data extent 0 at "
                       "offset %zu", to_write_extent_1, proc->writer_offset);

        iods[1].iod_size += to_write_extent_1;
        proc->writer_offset += to_write_extent_1;
        rc = extent_hash_update(&io_context->hashes[1],
                                buff_start + to_write_extent_0,
                                to_write_extent_1);
        if (rc)
            return rc;

        to_write -= to_write_extent_1;

        /*
         * fill parity bytes in buffer
         *
         * If needed, we reach the end of the input object, there is no data
         * after the ones we already write from the buffer. We can add
         * zeros at the end without overwriting effective input bytes.
         * We have enbough place in the buffer to set additional zeros because
         * the buffer size is the lcm of our stripe size.
         */
        if (to_write_extent_1 < to_write_extent_0)
            memset(buff_start + to_write_extent_0 + to_write_extent_1, 0,
                   to_write_extent_0 - to_write_extent_1);

        /* compute in place the parity extent */
        xor_in_place(buff_start, buff_start + to_write_extent_0,
                     to_write_extent_0);

        /* write the parity extent */
        rc = ioa_write(iods[2].iod_ioa, &iods[2],
                       buff_start + to_write_extent_0, to_write_extent_0);
        if (rc)
            LOG_RETURN(rc,
                       "raid4 unable to write %zu bytes in parity extent at offset "
                       "%zu", to_write_extent_0, proc->writer_offset);

        iods[2].iod_size += to_write_extent_0;
        rc = extent_hash_update(&io_context->hashes[2],
                                buff_start + to_write_extent_0,
                                to_write_extent_0);
        if (rc)
            return rc;
    }

    return 0;
}
