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

#include "pho_types.h"
#include "pho_attrs.h"
#include "pho_cfg.h"
#include "pho_common.h"
#include "pho_dss.h"
#include "pho_io.h"
#include "pho_layout.h"
#include "pho_module_loader.h"
#include "pho_srl_common.h"
#include "pho_type_utils.h"
#define EXTENT_TAG_SIZE 128

struct raid_io_context {
    struct io_adapter_module **ioa;  /**< IO adapter to access the current
                                       * storage backend
                                       */
    struct pho_io_descr *iod;        /**< IO decriptor to access the
                                       * current object
                                       */

    struct pho_ext_loc *ext_location; /**< Extent location */
    struct extent *extent;            /**< Extent to fill */
    char *extent_tag;                 /**< Extent name */
    unsigned int cur_extent_idx;      /**< Current extent index */
    bool requested_alloc;             /**< Whether an unanswer medium
                                        * allocation has been requested by
                                        * the encoder or not
                                        */

    size_t to_write;                  /**< Amount of data to read/write */
    size_t total_written;             /**< Amount of data written,
                                        * only used when splicount > 0
                                        */

    char *parts[3];                   /**< Buffer used to write/read */

    /* The following two fields are only used when writing */
    /** Extents written (appended as they are written) */
    GArray *written_extents;

    /**
     * Set of media to release (key: media_id str, value: refcount), used to
     * ensure that all written media have also been released (and therefore
     * flushed) when writing.
     *
     * We use a refcount as value to manage multiple extents written on same
     * medium.
     */
    GHashTable *to_release_media;

    /**
     * Nb media released
     *
     * We increment for each medium release response. Same medium used two
     * different times for two different extents will increment two times this
     * counter.
     *
     * Appart from null sized put, the end of the write is checked by
     * nb_released_media == written_extents->len .
     */
     size_t n_released_media;
};

bool no_more_alloc(struct pho_encoder *enc);

void raid_encoder_destroy(struct pho_encoder *enc);

void raid_build_write_allocation_req(struct pho_encoder *enc,
                                     pho_req_t *req, int repl_count);

int raid_encoder_step(struct pho_encoder *enc, pho_resp_t *resp,
                       pho_req_t **reqs, size_t *n_reqs);

int raid_enc_handle_resp(struct pho_encoder *enc, pho_resp_t *resp,
                          pho_req_t **reqs, size_t *n_reqs);
#endif
