/*
 *  All rights reserved (c) 2014-2023 CEA/DAM.
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
#ifndef RAID_COMMON_H
#define RAID_COMMON_H

#include "pho_layout.h"

struct read_io_context {
    pho_resp_read_t *resp;
    size_t to_read;
    struct extent **extents;
};

struct write_io_context {
    pho_resp_write_t *resp;
    size_t to_write;
    GString *user_md;
    struct extent *extents;

    /** Extents written (appended as they are written) */
    GArray *written_extents;

    /**
     * Set of media to release (key: media_id str, value: refcount),
     * used to ensure that all written media have also been released
     * (and therefore flushed) when writing.
     *
     * We use a refcount as value to manage multiple extents written on
     * same medium.
     */
    GHashTable *to_release_media;

    /**
     * Number of released media
     *
     * Incremented for each medium release response. A medium used two
     * different times for two different extents will increment two
     * times this counter.
     *
     * Appart from null-sized put, the end of the write is checked by
     * nb_released_media == written_extents->len
     */
    size_t n_released_media;
};

struct raid_io_context {
    /** Name of the RAID layout (stored on the medium) */
    const char *name;
    /** Writing and reading operations */
    const struct raid_ops *ops;
    /** Whether an unanswered medium allocation has been requested by the
     * encoder or not
     */
    bool requested_alloc;
    /** Buffers used by the layout, only the layout knows how many buffer it
     * allocates
     */
    struct pho_buff *buffers;
    size_t current_split;
    /** Number of data extents for this layout. This doesn't include parity
     * extents and replication.
     */
    size_t n_data_extents;
    /** Number of parity or replication extents. */
    size_t n_parity_extents;
    /** POSIX I/O Descriptor used to interact with the Xfer's FD */
    struct pho_io_descr posix;
    /** I/O descriptors used to read or write extents */
    struct pho_io_descr *iods;
    union {
        struct read_io_context read;
        struct write_io_context write;
    };
};

struct raid_ops {
    int (*write_split)(struct pho_encoder *enc, size_t split_size);
    int (*read_split)(struct pho_encoder *enc, int out_fd);
    int (*get_block_size)(struct pho_encoder *enc, size_t *block_size);
};

int raid_encoder_init(struct pho_encoder *enc,
                      const struct module_desc *module,
                      const struct pho_enc_ops *enc_ops,
                      const struct raid_ops *raid_ops);

int raid_decoder_init(struct pho_encoder *dec,
                      const struct module_desc *module,
                      const struct pho_enc_ops *enc_ops,
                      const struct raid_ops *raid_ops);

int raid_encoder_step(struct pho_encoder *enc, pho_resp_t *resp,
                      pho_req_t **reqs, size_t *n_reqs);

void raid_encoder_destroy(struct pho_encoder *enc);

size_t n_total_extents(struct raid_io_context *io_context);

#endif
