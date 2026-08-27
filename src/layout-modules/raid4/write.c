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

    rc = sprintf(buff, "%d", extent->layout_idx);
    if (rc < 0)
        LOG_RETURN(rc = -errno, "Unable to convert layout index to string");

    pho_attr_set(&iod->iod_attrs, PHO_EA_RAID4_EXTENT_INDEX_NAME, buff);

    return 0;
}

static int set_raid4_md(struct raid_io_context *io_context, size_t chunk_size,
                        enum processor_type type)
{
    struct output_io_context *output = raid_output_io_context(io_context, type);
    size_t n_extents = get_n_extents(io_context, type);
    struct pho_io_descr *iods = io_context->iods;
    struct extent *extents = output->extents;
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

    return set_raid4_md(io_context, io_context->current_split_chunk_size,
                        proc->type);
}

static bool raid4_should_write_extent(size_t *extent_to_write,
                                      size_t extent_idx)
{
    return extent_to_write == NULL || *extent_to_write == extent_idx;
}

static size_t raid4_iod_idx(struct pho_data_processor *proc, int extent_idx)
{
    return is_rebuilder(proc) ? 0 : extent_idx;
}

static int raid4_write_extent_from_buff(struct pho_data_processor *proc,
                                        int extent_idx, size_t to_write,
                                        off_t buff_offset, char *hash_buff)
{
    struct raid_io_context *io_context =
        &((struct raid_io_context *)proc->private_writer)[proc->current_target];
    size_t iod_idx = raid4_iod_idx(proc, extent_idx);
    struct pho_io_descr *iods = io_context->iods;
    int rc;

    rc = data_processor_write_from_buff(proc, &iods[iod_idx], to_write,
                                        buff_offset);
    if (rc)
        LOG_RETURN(rc,
                   "raid4 unable to write %zu bytes in extent %d at "
                   "offset %zu", to_write, extent_idx, proc->writer_offset);

    iods[iod_idx].iod_size += to_write;

    return extent_hash_update(&io_context->hashes[iod_idx], hash_buff,
                              to_write);
}

static int raid4_write_stripes_from_buff(struct pho_data_processor *proc,
                                         size_t to_write, size_t extent_0_size,
                                         size_t extent_1_size,
                                         size_t *extent_to_write)
{
    struct raid_io_context *io_context =
        &((struct raid_io_context *)proc->private_writer)[proc->current_target];
    size_t extent_0_iod_idx = raid4_iod_idx(proc, 0);
    size_t extent_1_iod_idx = raid4_iod_idx(proc, 1);
    struct pho_io_descr *iods = io_context->iods;
    int rc;

    while (to_write) {
        char *buff_start = proc->buff.buff +
                           (proc->writer_offset - proc->buffer_offset);
        size_t to_write_extent_0;
        size_t to_write_extent_1;
        size_t written_size;

        to_write_extent_0 = min(to_write,
                                extent_0_size -
                                    iods[extent_0_iod_idx].iod_size);
        to_write_extent_0 = min(to_write_extent_0,
                                io_context->current_split_chunk_size);

        to_write_extent_1 = min(to_write - to_write_extent_0,
                                extent_1_size -
                                    iods[extent_1_iod_idx].iod_size);
        to_write_extent_1 = min(to_write_extent_1,
                                io_context->current_split_chunk_size);

        written_size = to_write_extent_0 + to_write_extent_1;

        if (raid4_should_write_extent(extent_to_write, 0)) {
            rc = raid4_write_extent_from_buff(proc, 0, to_write_extent_0, 0,
                                              buff_start);
            if (rc)
                return rc;
        }

        if (raid4_should_write_extent(extent_to_write, 1)) {
            rc = raid4_write_extent_from_buff(proc, 1, to_write_extent_1,
                                              to_write_extent_0,
                                              buff_start + to_write_extent_0);
            if (rc)
                return rc;
        }

        if (raid4_should_write_extent(extent_to_write, 2)) {
            /*
             * fill parity bytes in buffer
             *
             * If needed, we reach the end of the input object, there is no data
             * after the ones we already write from the buffer. We can add
             * zeros at the end without overwriting effective input bytes.
             * We have enbough place in the buffer to set additional zeros
             * because the buffer size is the lcm of our stripe size.
             */
            if (to_write_extent_1 < to_write_extent_0)
                memset(buff_start + to_write_extent_0 + to_write_extent_1, 0,
                       to_write_extent_0 - to_write_extent_1);

            /* compute in place the parity extent */
            xor_in_place(buff_start, buff_start + to_write_extent_0,
                         to_write_extent_0);

            rc = raid4_write_extent_from_buff(proc, 2, to_write_extent_0,
                                              to_write_extent_0,
                                              buff_start + to_write_extent_0);
            if (rc)
                return rc;
        }

        proc->writer_offset += written_size;
        to_write -= written_size;
    }

    if (proc->writer_offset == proc->reader_offset)
        proc->buffer_offset = proc->writer_offset;

    return 0;
}

int raid4_rebuild_from_buff(struct pho_data_processor *proc)
{
    struct raid_io_context *io_context = proc->private_writer;
    size_t extent_to_write;
    size_t extent_1_size;
    size_t extent_0_size;
    size_t to_write;

    extent_to_write = io_context->rebuild.output.extents[0].layout_idx % 3;
    extent_1_size = io_context->rebuild.current_split_extent_size;
    extent_0_size = extent_1_size + io_context->rebuild.extent_remainder;

    to_write = proc->reader_offset - proc->writer_offset;

    return raid4_write_stripes_from_buff(proc, to_write, extent_0_size,
                                         extent_1_size, &extent_to_write);
}

int raid4_write_from_buff(struct pho_data_processor *proc)
{
    struct raid_io_context *io_context =
        &((struct raid_io_context *)proc->private_writer)[proc->current_target];
    size_t inside_split_offset = proc->writer_offset -
                                 io_context->current_split_offset;
    size_t to_write;
    int rc;

    /* limit: split -> buffer */
    to_write = min(io_context->current_split_size - inside_split_offset,
                   proc->reader_offset - proc->writer_offset);

    rc = raid4_write_stripes_from_buff(proc, to_write,
                                       io_context->write.output.extents[0].size,
                                       io_context->write.output.extents[1].size,
                                       NULL);
    if (rc)
        return rc;

    if (proc->writer_offset >= proc->object_size)
        io_context->write.all_is_written = true;

    return 0;
}
