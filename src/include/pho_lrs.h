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
 * @defgroup pho_lrs_protocol Structures describing the LRS protocol.
 * @{
 */

/** Current versions of the LRS protocol */
#define PHO_LRS_PROTOCOL_VERSION    0

/** LRS request kind */
enum pho_lrs_req_kind {
    /**
     * Ask the LRS to allocate media with given properties to be able to write
     * data on them.
     */
    LRS_REQ_MEDIA_WRITE_ALLOC,

    /**
     * Ask the LRS to allocate given media (by id) to be able to read from
     * them.
     */
    LRS_REQ_MEDIA_READ_ALLOC,

    /**
     * Ask the LRS to release medias and flush them. A medium must be released
     * and flushed to consider the IO complete from the request emitter side.
     */
    LRS_REQ_MEDIA_RELEASE,

    /** pho_lrs_req_kind upper bound, not a valid request kind */
    LRS_REQ_MAX,
};

enum pho_lrs_resp_kind {
    /**
     * Provide a list of allocated (mounted) medias with write permission and
     * details to access them.
     */
    LRS_RESP_MEDIA_WRITE_ALLOC,

    /**
     * Provide a list of allocated (mounted) medias with read permission and
     * details to access them
     */
    LRS_RESP_MEDIA_READ_ALLOC,

    /** Confirm that some medias have been successfully released. */
    LRS_RESP_MEDIA_RELEASE,

    /**
     * Signals an error when processing a request, can be an answer to any
     * request.
     */
    LRS_RESP_ERROR,

    /** pho_lrs_resp_kind upper bound, not a valid response kind */
    LRS_RESP_MAX,
};

/** Request for one write accessible medium */
struct media_write_req {
    size_t size;        /**< Amount of data to be written on the medium */
    struct tags tags;   /**< Tags to be satisfied */
};

/** Body of write allocation request */
struct media_write_alloc_req {
    int n_medias;                   /**< Number of distinct medias to request */
    struct media_write_req *medias; /**< Write allocation requests */
};

/**
 * Body of read allocation request, request n_required medias among n_medias
 * media_ids provided (n_required <= n_medias).
 */
struct media_read_alloc_req {
    int n_medias;               /**< Number of medias in `media_ids`*/
    int n_required;             /**< Number of medias to actually allocate among
                                  *  the ones specified in medias_id
                                  */
    struct media_id *media_ids; /**< Ids of the requested medias */
};

/** Release request for one medium */
struct media_release_req_elt {
    struct media_id id;     /**< Id of the medium to release */
    int rc;                 /**< Outcome of the performed IO (0 or -errno) */
    size_t size_written;    /**< Amount of bytes written on this medium (0 if
                              *  the medium was only read).
                              */
};

/** Body of the release request */
struct media_release_req {
    int n_medias;                           /**< Number of medias to release */
    struct media_release_req_elt *medias;   /**< Descriptions of the medias to
                                              *  release.
                                              */
};

/** An LRS protocol request, emitted by layout modules */
struct pho_lrs_req {
    uint32_t protocol_version;  /**< Protocol version for this request */
    size_t req_id;              /**< Request id to match its future response */
    enum pho_lrs_req_kind kind; /**< Kind of request */
    union {
        struct media_write_alloc_req walloc;
        struct media_read_alloc_req ralloc;
        struct media_release_req release;
    } body;                     /**< Body of the request (depends on `kind`) */
};

/** Write allocation response for one medium */
struct media_write_resp_elt {
    struct media_id media_id;       /**< Id of the allocated medium */
    size_t avail_size;              /**< Size available on this medium
                                      *  (potentially less than what was asked
                                      *  for)
                                      */
    char *root_path;                /**< Mount point of this medium */
    enum fs_type fs_type;           /**< Type of filesystem */
    enum address_type addr_type;    /**< Type of address */
};

/** Body of the write allocation response */
struct media_write_alloc_resp {
    int n_medias;                        /**< Number of allocated medias */
    struct media_write_resp_elt *medias; /**< Description of allocated medias */
};

/** Read allocation response for one medium */
struct media_read_resp_elt {
    struct media_id media_id;
    char *root_path;
    enum fs_type fs_type;
    enum address_type addr_type;
};

/** Body of the read allocation response */
struct media_read_alloc_resp {
    int n_medias;                       /**< Number of allocated medias */
    struct media_read_resp_elt *medias; /**< Description of allocated medias */
};

/** Body of the release allocation reponse */
struct media_release_resp {
    int n_medias;                       /**< Number of released medias */
    struct media_id *media_ids;         /**< Ids of released medias */
};

/** Body of the error response */
struct error_resp {
    int rc;                          /**< Error code, -errno for homogeneity */
    enum pho_lrs_req_kind req_kind;  /**< Kind of request that raised the error
                                       */
};

/** An LRS protocol response, emitted by the LRS */
struct pho_lrs_resp {
    uint32_t protocol_version;      /**< Protocol version for this response */
    size_t req_id;                  /**< Request ID, to be matched with the
                                      *  corresponding request
                                      */
    enum pho_lrs_resp_kind kind;    /**< Kind of response */
    union {
        struct media_write_alloc_resp walloc;
        struct media_read_alloc_resp ralloc;
        struct media_release_resp release;
        struct error_resp error;
    } body;                     /**< Body of the response (depends on `kind`) */
};

/** @} end of pho_lrs_protocol group */

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
 * @param(in)   req     The request to be handled.
 *
 * @return 0 on success, -1 * posix error code on failure.
 */
int lrs_request_enqueue(struct lrs *lrs, struct pho_lrs_req *req);

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
int lrs_responses_get(struct lrs *lrs, struct pho_lrs_resp **resps,
                      size_t *n_resps);

/** Free one pho_lrs_req and all associated allocations. */
void lrs_req_free(struct pho_lrs_req *req);

/** Free one pho_lrs_resp and all associated allocations. */
void lrs_resp_free(struct pho_lrs_resp *resp);

/** Free an array of responses. */
void lrs_responses_free(struct pho_lrs_resp *resps, size_t n_resps);

/** Get a static string representation of a request kind. */
const char *lrs_req_kind_str(enum pho_lrs_req_kind kind);

/** Get a static string representation of a response kind. */
const char *lrs_resp_kind_str(enum pho_lrs_resp_kind kind);

#endif
