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

int raid4_extra_attrs(struct pho_data_processor *proc)
{
    struct raid_io_context *io_context =
        &((struct raid_io_context *)proc->private_writer)[proc->current_target];

    return set_raid4_md(io_context, io_context->current_split_chunk_size);
}

int raid4_write_from_buff(struct pho_data_processor *proc)
{
    struct raid_io_context *io_context =
        &((struct raid_io_context *)proc->private_writer)[proc->current_target];
    size_t inside_split_offset = proc->writer_offset -
                                 io_context->current_split_offset;
    struct pho_io_descr *iods = io_context->iods;
    size_t to_write;
    int rc;

    /* limit: split -> buffer */
    to_write = min(io_context->current_split_size - inside_split_offset,
                   proc->reader_offset - proc->writer_offset);

    /* write chunk by chunk */
    while (to_write) {
        char *buff_start = proc->buff.buff +
                           (proc->writer_offset - proc->buffer_offset);
        size_t to_write_extent_0;
        size_t to_write_extent_1;

        /* limit: extent -> chunk */
        to_write_extent_0 = min(to_write,
                                io_context->write.extents[0].size -
                                    iods[0].iod_size);
        to_write_extent_0 = min(to_write_extent_0,
                                io_context->current_split_chunk_size);

        /* write the data extent 0 */
        rc = data_processor_write_from_buff(proc, &iods[0], to_write_extent_0,
                                            0);
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

        /* limit: extent -> chunk */
        to_write_extent_1 = min(to_write,
                                io_context->write.extents[1].size -
                                    iods[1].iod_size);
        to_write_extent_1 = min(to_write_extent_1,
                                io_context->current_split_chunk_size);

        /* write the data extent 1 */
        rc = data_processor_write_from_buff(proc, &iods[1], to_write_extent_1,
                                            0);
        if (rc)
            LOG_RETURN(rc,
                       "raid4 unable to write %zu bytes in data extent 0 at "
                       "offset %zu", to_write_extent_1, proc->writer_offset);

        iods[1].iod_size += to_write_extent_1;
        proc->writer_offset += to_write_extent_1;
        if (proc->writer_offset >= proc->object_size)
            io_context->write.all_is_written = true;

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
        rc = data_processor_write_from_buff(
            proc, &iods[2], to_write_extent_0,
            /* The XOR is computed inplace at buff_start + to_write_extent_0.
             * Since we wrote to_write_extent_0 + to_write_extent_1 bytes,
             * we need to move back to_write_extent_1 bytes to find the XOR.
             */
            -to_write_extent_1
        );
        if (rc)
            LOG_RETURN(rc,
                       "raid4 unable to write %zu bytes in parity extent at "
                       "offset %zu", to_write_extent_0, proc->writer_offset);

        iods[2].iod_size += to_write_extent_0;
        rc = extent_hash_update(&io_context->hashes[2],
                                buff_start + to_write_extent_0,
                                to_write_extent_0);
        if (rc)
            return rc;
    }

    if (proc->writer_offset == proc->reader_offset)
        proc->buffer_offset = proc->writer_offset;
    return 0;
}
