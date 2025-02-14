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

#include <stdbool.h>
#include <unistd.h>

static int write_with_xor(struct pho_data_processor *dec,
                          struct pho_io_descr *iod1,
                          struct pho_io_descr *iod2,
                          bool second_part_missing)
{
    struct raid_io_context *io_context = dec->private_processor;
    struct pho_io_descr *posix = &io_context->posix;
    size_t buf_size = io_context->buffers[0].size;
    struct extent *split_extents;
    size_t written = 0;
    size_t split_size;
    int rc;
    int i;

    ENTRY;

    split_extents = dec->layout->extents +
        io_context->current_split * n_total_extents(io_context);

    split_size = split_extents[0].size + split_extents[1].size;

    while (true) {
        ssize_t part1_size;
        ssize_t part2_size;

        part1_size = ioa_read(iod1->iod_ioa, iod1,
                              io_context->buffers[0].buff, buf_size);
        if (part1_size < 0)
            LOG_RETURN(part1_size, "Failed to read file");
        pho_debug("part1_size: %ld", part1_size);

        if (io_context->read.check_hash) {
            rc = extent_hash_update(&io_context->hashes[0],
                                    io_context->buffers[0].buff, part1_size);
            if (rc)
                return rc;
        }

        // XOR
        part2_size = ioa_read(iod2->iod_ioa, iod2,
                              io_context->buffers[1].buff,
                              buf_size);
        if (part2_size < 0)
            LOG_RETURN(part2_size, "Failed to read file");

        pho_debug("part2_size: %ld", part2_size);

        if (io_context->read.check_hash) {
            rc = extent_hash_update(&io_context->hashes[1],
                                    io_context->buffers[1].buff, part2_size);
            if (rc)
                return rc;
        }

        if (part1_size != part2_size) {
            /* Since the xor is always associated with iod2 and each buffer is
             * padded during put, the xor should always be a multiple of the
             * buffer size.
             */
            assert(part1_size < part2_size);
            memset(io_context->buffers[0].buff + part1_size, 0,
                   part2_size - part1_size);
        }

        buffer_xor(&io_context->buffers[0], &io_context->buffers[1],
                   &io_context->buffers[2], buf_size);

        rc = ioa_write(posix->iod_ioa, posix,
                       second_part_missing ?
                           io_context->buffers[0].buff :
                           io_context->buffers[2].buff,
                       second_part_missing ?
                           part1_size :
                           part2_size);
        if (rc)
            return rc;

        written += second_part_missing ?  part1_size : part2_size;
        rc = ioa_write(posix->iod_ioa, posix,
                       second_part_missing ?
                           io_context->buffers[2].buff :
                           io_context->buffers[0].buff,
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

    if (io_context->read.check_hash) {
        for (i = 0; i < io_context->n_data_extents; i++) {
            rc = extent_hash_digest(&io_context->hashes[i]);
            if (rc)
                return rc;

            rc = extent_hash_compare(&io_context->hashes[i],
                                     io_context->read.extents[i]);
            if (rc)
                return rc;
        }
    }

    return 0;
}

static int write_without_xor(struct pho_data_processor *dec,
                             struct pho_io_descr *iod1,
                             struct pho_io_descr *iod2)
{
    struct raid_io_context *io_context = dec->private_processor;
    struct pho_io_descr *posix = &io_context->posix;
    size_t written = 0;
    ssize_t data_read;
    size_t read_size;
    size_t to_write;
    int rc;
    int i;

    ENTRY;

    to_write = io_context->read.extents[0]->size +
        io_context->read.extents[1]->size;
    read_size = io_context->buffers[0].size;

    while (written < to_write) {

        data_read = ioa_read(iod1->iod_ioa, iod1,
                             io_context->buffers[0].buff,
                             read_size);
        if (data_read < 0)
            LOG_RETURN(data_read, "Failed to read file");

        rc = ioa_write(posix->iod_ioa, posix, io_context->buffers[0].buff,
                       data_read);
        if (rc < 0)
            LOG_RETURN(rc, "Failed to write in file");

        if (io_context->read.check_hash) {
            rc = extent_hash_update(&io_context->hashes[0],
                                    io_context->buffers[0].buff,
                                    data_read);
            if (rc)
                return rc;
        }

        written += data_read;
        data_read = ioa_read(iod2->iod_ioa, iod2,
                             io_context->buffers[0].buff,
                             read_size);
        if (data_read < 0)
            LOG_RETURN(data_read, "Failed to read file");

        rc = ioa_write(posix->iod_ioa, posix, io_context->buffers[0].buff,
                       data_read);
        if (rc < 0)
            LOG_RETURN(rc, "Failed to write in file");

        if (io_context->read.check_hash) {
            rc = extent_hash_update(&io_context->hashes[1],
                                    io_context->buffers[0].buff,
                                    data_read);
            if (rc)
                return rc;
        }

        written += data_read;
    }

    if (io_context->read.check_hash) {
        for (i = 0; i < io_context->n_data_extents; i++) {
            rc = extent_hash_digest(&io_context->hashes[i]);
            if (rc)
                return rc;

            rc = extent_hash_compare(&io_context->hashes[i],
                                     io_context->read.extents[i]);
            if (rc)
                return rc;
        }
    }

    return 0;
}

/* has_part1 and has_xor are tested first as it is easier to check for their
 * presence.
 *
 * We might have the following combinations:
 * - part1 + part2
 * - part1 + xor
 * - part2 + xor
 *
 * If part1 is present, since the extents are sorted by layout index, it is
 * necessarily in the first position of the list. The same way, if the xor is
 * present, it is necessarily in the second position.
 */
int raid4_read_split(struct pho_data_processor *decoder)
{
    struct raid_io_context *io_context = decoder->private_processor;
    struct pho_io_descr *iods = io_context->iods;
    bool has_part1 = (io_context->read.extents[0]->layout_idx % 3) == 0;
    bool has_xor = (io_context->read.extents[1]->layout_idx % 3) == 2;
    bool has_part2 = !has_part1 || !has_xor;

    ENTRY;

    if (has_part1 && has_part2)
        return write_without_xor(decoder, &iods[0], &iods[1]);
    else if (has_part1 && has_xor)
        return write_with_xor(decoder, &iods[0], &iods[1], !has_part2);
    else if (has_part2 && has_xor)
        return write_with_xor(decoder, &iods[0], &iods[1], !has_part2);

    pho_error(0, "%s: unexpected split combination, abort.", __func__);
    abort();
}

static int read_extra_parity_byte(struct raid_io_context *io_context)
{
    char one_read_byte;
    ssize_t read_size;
    int rc;

    read_size = ioa_read(io_context->iods[1].iod_ioa, &io_context->iods[1],
                         &one_read_byte, 1);

    if (read_size < 0)
        LOG_RETURN(read_size, "reading one additional parity byte fails");

    if (read_size == 0)
        LOG_RETURN(-EIO, "unable to read one additional parity byte");

    io_context->iods[1].iod_size += 1;

    rc = extent_hash_update(&io_context->hashes[1], &one_read_byte, 1);
    if (rc)
        return rc;

    return 0;
}

int raid4_read_into_buff(struct pho_data_processor *proc)
{
    size_t buffer_offset = proc->reader_offset - proc->buffer_offset;
    struct raid_io_context *io_context =
        (struct raid_io_context *)proc->private_reader;
    size_t inside_split_offset = proc->reader_offset -
                                 io_context->current_split_offset;
    char *buff_start = proc->buff.buff + buffer_offset;
    bool with_extent_0;
    size_t to_read;
    bool with_xor;

   /*
    * If true extent 0 is present, since the extents are sorted by layout index,
    * it is necessarily in the first position of the list. The same way,
    * if the xor is present, it is necessarily in the second position.
    */
    with_extent_0 = (io_context->read.extents[0]->layout_idx % 3 == 0);
    with_xor = (io_context->read.extents[1]->layout_idx % 3 == 2);

    /* limit : object -> split -> buffer */
    to_read = min(proc->object_size - proc->reader_offset,
                  io_context->current_split_size - inside_split_offset);
    to_read = min(to_read, proc->buff.size - buffer_offset);

    while (to_read) {
        char *parity_buff = NULL;
        size_t parity_count = 0;
        char *data_buff = NULL;
        size_t extent_to_read;
        size_t extent_index;
        int rc;
        int i;

        /*
         * When we have (with_xor && !with_extent_0),
         * the parity extent must replace the extent 0 and must be read first.
         */
        if (with_xor && !with_extent_0)
            extent_index = 1;
        else
            extent_index = 0;

        /* limit : extent -> chunk */
        extent_to_read = min(to_read,
                             io_context->read.extents[extent_index]->size -
                                 io_context->iods[extent_index].iod_size);
        extent_to_read = min(extent_to_read,
                             io_context->current_split_chunk_size);

        /* We read extent 0 and extent 1 to be able to compute parity */
        for (i = 0;
             i < 2;
             i++, buff_start += extent_to_read,
                 extent_index = (extent_index + 1) % 2) {
            extent_to_read = min(extent_to_read, to_read);
            if (with_xor) {
                if (extent_index == 1) {
                    parity_buff = buff_start;
                    parity_count = extent_to_read;
                } else {
                    data_buff = buff_start;
                }
            }

            if (extent_to_read == 0)
                goto check_extra_parity_byte;

            rc = data_processor_read_into_buff(proc,
                                               &io_context->iods[extent_index],
                                               extent_to_read);
            if (rc)
                return rc;

            to_read -= extent_to_read;

            if (io_context->read.check_hash) {
                rc = extent_hash_update(&io_context->hashes[extent_index],
                                        buff_start, extent_to_read);
                if (rc)
                    return rc;
            }

            /*
             * If the parity extent is replacing a shorter data extent, we need
             * to read extra bytes only to fill the hash.
             */
check_extra_parity_byte:
            if (io_context->read.check_hash && with_xor && extent_index == 1 &&
                to_read == 0 &&
                proc->reader_offset - io_context->current_split_offset ==
                    io_context->current_split_size &&
                io_context->read.extents[extent_index]->size >
                    io_context->iods[extent_index].iod_size) {
                assert(io_context->read.extents[extent_index]->size -
                           io_context->iods[extent_index].iod_size == 1);
                rc = read_extra_parity_byte(io_context);
                if (rc)
                    return rc;
            }
        }

        /* compute XOR parity */
        if (with_xor && parity_count > 0) {
            /*
             * zero padding
             *
             * Only the last read chunk could be shorter than the first one.
             */
            if (parity_count > extent_to_read)
                memset(data_buff + extent_to_read, 0,
                       parity_count - extent_to_read);

            xor_in_place(data_buff, parity_buff, parity_count);
        }
    }

    return 0;
}
