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
#ifndef RAID_COMMON_H
#define RAID_COMMON_H

#include <stdbool.h>

#include "pho_layout.h"

#include <openssl/evp.h>
#if HAVE_XXH128
#include <xxhash.h>
#endif

#if HAVE_XXH128
#define DEFAULT_XXH128 "true"
#define DEFAULT_MD5    "false"
#else
#define DEFAULT_XXH128 "false"
#define DEFAULT_MD5    "true"
#endif
#define DEFAULT_CHECK_HASH "true"

struct extent_hash {
#if HAVE_XXH128
    XXH128_hash_t xxh128;
    XXH3_state_t *xxh128context;
#endif
    unsigned char md5[MD5_BYTE_LENGTH];
    EVP_MD_CTX   *md5context;
};

struct read_io_context {
    pho_resp_t *resp;           /* copied read alloc resp */
    size_t to_read;             /*< Remaining size to read per extent
                                 *  aggregated on all splits
                                 */
    /*
     * OUTPUT FD: [0123456]
     * RAID 1 with one replica, SPLIT 0 : extent_0 [0123]  extent_1 [0123]
     *                                            <sz0r1> = 4
     * RAID 1 with one replica; SPLIT 1 : extent_2 [456]  extent_3 [456]
     *                                            <sz2r1> = 3
     * RAID 1 init to_read: 7 = <sz0r1> + <sz2r1>
     * RAID 1 init to_read is the size of the object.
     *
     * RAID 4 SPLIT 0 : extent_0 [01] extent_1 [23] extent_2 [PP]
     *                         <sz0r4> = 2
     * RAID 4 SPLIT 1 : extent_3 [45] extent_4 [6-] extent_5 [PP]
     *                         <sz3r4> = 2
     * RAID 4 init to_read: 4 = <sz0r4> + <sz3r4>
     */
    struct extent **extents;
    bool check_hash;
};

struct delete_io_context {
    pho_resp_read_t *resp;          /*< Response for the I/O operation */
    size_t to_delete;               /*< Number of extents to delete */
};

struct write_io_context {
    bool all_is_written;
    bool released;              /*< true when we receive all release ack */
    size_t to_write;            /*< Whole object remaining size to write */
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
    size_t current_split_size;
    size_t current_split_offset;
    size_t current_split_chunk_size;
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
        struct delete_io_context delete;
        struct write_io_context write;
    };

    /**
     * A list of hashes that are computed while the extent is written.
     * The number of elements in this list is layout specific. The list
     * must be allocated by the encoder.
     *
     * When reading an extent, the hash can be recomputed and checked with the
     * hash in the DSS.
     */
    struct extent_hash *hashes;
    /** Size of \p hashes, initialized by the layout */
    size_t nb_hashes;
};

struct raid_ops {
    int (*get_reader_chunk_size)(struct pho_data_processor *proc,
                                 size_t *chunk_size);
    int (*read_into_buff)(struct pho_data_processor *proc);
    int (*write_from_buff)(struct pho_data_processor *proc);
    int (*set_extra_attrs)(struct pho_data_processor *proc);
};

int raid_encoder_init(struct pho_data_processor *encoder,
                      const struct module_desc *module,
                      const struct pho_proc_ops *enc_ops,
                      const struct raid_ops *raid_ops);

int raid_decoder_init(struct pho_data_processor *decoder,
                      const struct module_desc *module,
                      const struct pho_proc_ops *enc_ops,
                      const struct raid_ops *raid_ops);

int raid_eraser_init(struct pho_data_processor *eraser,
                     const struct module_desc *module,
                     const struct pho_proc_ops *enc_ops,
                     const struct raid_ops *raid_ops);

int raid_reader_processor_step(struct pho_data_processor *proc,
                               pho_resp_t *resp, pho_req_t **reqs,
                               size_t *n_reqs);
int raid_writer_processor_step(struct pho_data_processor *proc,
                               pho_resp_t *resp, pho_req_t **reqs,
                               size_t *n_reqs);
int raid_eraser_processor_step(struct pho_data_processor *proc,
                               pho_resp_t *resp, pho_req_t **reqs,
                               size_t *n_reqs);

int raid_processor_step(struct pho_data_processor *proc, pho_resp_t *resp,
                        pho_req_t **reqs, size_t *n_reqs);

/**
 * Generic implementation of raid_ops::delete_split
 */
int raid_delete_split(struct pho_data_processor *eraser);

/**
 * Generic implementation of pho_layout_module_ops::locate
 *
 * This function takes 2 additional parameters: n_data_extents and
 * n_parity_extents. It will locate an object whose layout requires
 * n_data_extents to be available on the host to be read. The total number of
 * extents of this object per split is n_data_extents + n_parity_extents.
 */
int raid_locate(struct dss_handle *dss, struct layout_info *layout,
                size_t n_data_extents, size_t n_parity_extents,
                const char *focus_host, char **hostname,
                int *nb_new_lock);

void raid_reader_processor_destroy(struct pho_data_processor *proc);
void raid_writer_processor_destroy(struct pho_data_processor *proc);
void raid_eraser_processor_destroy(struct pho_data_processor *proc);

size_t n_total_extents(struct raid_io_context *io_context);

int extent_hash_init(struct extent_hash *hash, bool use_md5, bool use_xxhash);

int extent_hash_reset(struct extent_hash *hash);

void extent_hash_fini(struct extent_hash *hash);

int extent_hash_update(struct extent_hash *hash, char *buffer, size_t size);

int extent_hash_digest(struct extent_hash *hash);

void extent_hash_copy(struct extent_hash *hash, struct extent *extent);

int extent_hash_compare(struct extent_hash *hash, struct extent *extent);

struct pho_ext_loc make_ext_location(struct pho_data_processor *proc, size_t i,
                                     int idx, enum processor_type type);

int get_object_size_from_layout(struct layout_info *layout);

#endif
