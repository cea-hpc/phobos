/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2017 CEA/DAM.
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
 * \brief  Phobos Local Resource Scheduler (LRS)
 */
#ifndef _PHO_LRS_H
#define _PHO_LRS_H

#include <stdint.h>

#include "pho_srl_lrs.h"
#include "pho_types.h"

struct dss_handle;
struct dev_descr;

/**
 * Local Resource Scheduler instance, manages media and local devices for the
 * actual IO to be performed.
 */
struct lrs {
    struct dss_handle *dss;         /**< Associated DSS */
    struct dev_descr  *devices;     /**< List of available devices */
    size_t             dev_count;   /**< Number of devices */
    char              *lock_owner;  /**< Lock owner name for this LRS
                                      *  (contains hostname and tid)
                                      */
    GQueue *req_queue;              /**< Queue for all but release requests */
    GQueue *release_queue;          /**< Queue for release requests */
};

/**
 * Initialize a new LRS bound to a given DSS.
 *
 * @param[in]   lrs The LRS to be initialized.
 * @param[in]   dss The DSS that will be used by \a lrs. Initialized (dss_init),
 *                  managed and deinitialized (dss_fini) externally by the
 *                  caller.
 *
 * @return 0 on success, -1 * posix error code on failure
 */
int lrs_init(struct lrs *lrs, struct dss_handle *dss);

/**
 * Free all resources associated with this LRS except for the dss, which must be
 * deinitialized by the caller if necessary.
 *
 * @param[in]   lrs The LRS to be deinitialized.
 */
void lrs_fini(struct lrs *lrs);

/**
 * Identify medium-global error codes.
 * Typically useful to trigger custom procedures when a medium becomes
 * read-only.
 */
static inline bool is_media_global_error(int errcode)
{
    return errcode == -ENOSPC || errcode == -EROFS || errcode == -EDQUOT;
}

/**
 * Load and format a media to the given fs type.
 *
 * @param(in)   lrs     Initialized LRS.
 * @param(in)   id      Media ID for the media to format.
 * @param(in)   fs      Filesystem type (only PHO_FS_LTFS for now).
 * @param(in)   unlock  Unlock tape if successfully formated.
 * @return 0 on success, negative error code on failure.
 *
 * @TODO: this should be integrated into the LRS protocol
 */
int lrs_format(struct lrs *lrs, const struct media_id *id,
               enum fs_type fs, bool unlock);

/**
 * Make the LRS aware of a new device
 *
 * @FIXME This is a temporary API waiting for a transparent way to add devices
 * while running (to be done with the LRS protocol).
 */
int lrs_device_add(struct lrs *lrs, const struct dev_info *devi);

/**
 * Enqueue a request to be handled by the LRS. The request is guaranteed to be
 * answered at some point.
 *
 * Takes ownership of the request and will free it later on.
 *
 * @param(in)   lrs     The LRS that will handle the request.
 * @param(in)   req     The serialized request buffer.
 *
 * @return 0 on success, -1 * posix error code on failure.
 */
int lrs_request_enqueue(struct lrs *lrs, struct pho_ubuff req);

/**
 * Get responses for all handled pending requests (enqueued with
 * lrs_request_enqueue).
 *
 * @param(in)   lrs     The LRS from which to get the responses.
 * @param(out)  resps   An array of responses (to be freed by the caller with
 *                      lrs_responses_free).
 * @param(out)  n_resps Number of reponses in \a resps.
 *
 * @return 0 on success, -1 * posix error code on failure.
 */
int lrs_responses_get(struct lrs *lrs, struct pho_ubuff **resps,
                      size_t *n_resps);

#endif
