/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2022 CEA/DAM.
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
 * \brief  Phobos Local Resource Scheduler (LRS) - scheduler
 */
#ifndef _PHO_LRS_SCHED_H
#define _PHO_LRS_SCHED_H

#include <pthread.h>
#include <stdint.h>

#include "pho_dss.h"
#include "pho_srl_lrs.h"
#include "pho_types.h"

#include "lrs_device.h"

/**
 * Local Resource Scheduler instance, manages media and local devices for the
 * actual IO to be performed.
 */
struct lrs_sched {
    struct dss_handle  dss;             /**< Associated DSS */
    enum rsc_family    family;          /**< Managed resource family */
    struct lrs_dev_hdl devices;         /**< Handle to device threads */
    const char        *lock_hostname;   /**< Lock hostname for this LRS */
    int                lock_owner;      /**< Lock owner (pid) for this LRS */
    GQueue            *req_queue;       /**< Queue for all but
                                          *  release requests
                                          */
    struct tsqueue    *response_queue;  /**< Queue for responses */
    struct timespec    sync_time_threshold;
                                        /**< Time threshold for medium
                                         *   synchronization
                                         */
    unsigned int       sync_nb_req_threshold;
                                        /**< Number of requests threshold
                                         *   for medium synchronization
                                         */
    unsigned long      sync_written_size_threshold;
                                        /**< Written size threshold for
                                         *   medium synchronization
                                         */
};

/**
 * Enumeration for request/medium synchronization status.
 */
enum tosync_status {
    SYNC_TODO,                      /**< Synchronization is requested */
    SYNC_DONE,                      /**< Synchronization is done */
    SYNC_ERROR,                     /**< Synchronization failed */
    SYNC_CANCEL,                    /**< Synchronization canceled */
};

/**
 * Internal structure for a to-synchronize medium.
 */
struct tosync_medium {
    enum tosync_status status;      /**< Medium synchronization status. */
    struct pho_id medium;           /**< Medium ID. */
    size_t written_size;            /**< Written size on the medium to sync. */
};

/**
 * Internal structure for a not-to-synchronize medium.
 */
struct nosync_medium {
    struct pho_id medium;           /**< Medium ID. */
    size_t written_size;            /**< Size written on the medium */
};

/**
 * Parameters of a request container dedicated to a release request
 */
struct release_params {
    struct tosync_medium *tosync_media; /**< Array of media to synchronize */
    size_t n_tosync_media;              /**< Number of media to synchronize */
    struct nosync_medium *nosync_media; /**< Array of media with no sync */
    size_t n_nosync_media;              /**< Number of media with no sync */
    int rc;                             /**< Global return code, if multiple
                                          * sync failed, only the first one is
                                          * kept.
                                          */
};

/**
 * Request container used by the scheduler to transfer information
 * between a request and its response. For now, this information consists
 * in a socket ID.
 */
struct req_container {
    pthread_mutex_t mutex;          /**< Exclusive access to request. */
    int socket_id;                  /**< Socket ID to pass to the response. */
    pho_req_t *req;                 /**< Request. */
    struct timespec received_at;    /**< Request reception timestamp */
    union {                         /**< Parameters used by the LRS. */
        struct release_params release;
    } params;
};

/**
 * Release memory allocated for params structure of a request container.
 */
static inline void destroy_container_params(struct req_container *cont)
{
    if (pho_request_is_release(cont->req)) {
        free(cont->params.release.tosync_media);
        free(cont->params.release.nosync_media);
    }
}

/** sched_req_free can be used as glib callback */
void sched_req_free(void *reqc);

/**
 * Response container used by the scheduler to transfer information
 * between a request and its response. For now, this information consists
 * in a socket ID.
 */
struct resp_container {
    int socket_id;                  /**< Socket ID got from the request. */
    pho_resp_t *resp;               /**< Response. */
};

/**
 * Allocate and fill an error response into a response container
 *
 * \param[in, out]  resp_cont   Response container filled by prepare_error
 * \param[in]       req_rc      Error code of the response
 * \param[in]       req_cont    Request to answer the response
 *
 * \return                      0 on success, -1 * posix error code on failure.
 */
int prepare_error(struct resp_container *resp_cont, int req_rc,
                  const struct req_container *req_cont);

/**
 * Initialize a new sched bound to a given DSS.
 *
 * \param[in]       sched       The sched to be initialized.
 * \param[in]       family      Resource family managed by the scheduler.
 * \param[in]       resp_queue  Global response queue.
 *
 * \return                      0 on success, -1 * posix error code on failure.
 */
int sched_init(struct lrs_sched *sched, enum rsc_family family,
               struct tsqueue *resp_queue);

/**
 * Free all resources associated with this sched except for the dss, which must
 * be deinitialized by the caller if necessary.
 *
 * \param[in]       sched       The sched to be deinitialized.
 */
void sched_fini(struct lrs_sched *sched);

/**
 * Enqueue a request to be handled by the devices.
 *
 * This function takes ownership of the request.
 * The request container is queued or freed by this call.
 * If an error occurs, this function creates and queues the corresponding
 * error message.
 *
 * The error code returned by this function stands for an error of the LRS
 * daemon itself, not an error about the release request which is managed by
 * an error message.
 */
int sched_release_enqueue(struct lrs_sched *sched, struct req_container *reqc);

/**
 * Get responses for every handled pending requests
 *
 * The responses are encapsulated in a container which owns a socket ID given by
 * the associated request.
 *
 * \param[in]       sched       The sched from which to get the responses.
 * \param[out]      n_resp      The number of responses generated by the sched.
 * \param[out]      respc       The responses issued from the sched process.
 *
 * \return                      0 on success, -1 * posix error code on failure.
 */
int sched_responses_get(struct lrs_sched *sched, int *n_resp,
                        struct resp_container **respc);

/** sched_resp_free can be used as glib callback */
void sched_resp_free(void *respc);

/* Wrapper of sched_request_tosync_free to be used by glib foreach */
void sched_request_tosync_free_wrapper(gpointer _req,
                                       gpointer _null_user_data);

/**
 * Acquire device lock if it is not already set.
 *
 * If lock is already set, check hostname and owner.
 * -EALREADY is returned if dev->lock.hostname is not the same as
 *  sched->lock_hostname.
 *  If dev->lock.owner is not the same as sched->lock_owner, the lock is
 *  re-taken from DSS to update the owner.
 */
int check_and_take_device_lock(struct lrs_sched *sched,
                               struct dev_info *dev);

#endif
