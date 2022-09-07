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

#include <glib.h>
#include <pthread.h>
#include <stdint.h>

#include "pho_dss.h"
#include "pho_srl_lrs.h"
#include "pho_types.h"
#include "pho_type_utils.h"

#include "io_sched.h"
#include "lrs_device.h"
#include "lrs_thread.h"
#include "lrs_utils.h"

/* from lrs.c */
extern bool running;

/**
 * Media with ongoing format scheduled requests
 *
 * "media_name" keys and values contains "struct media_info *" rsc.id.name
 * This structure doesn't "own" these rsc.id.name references and never
 * frees them. They must be freed elsewhere.
 *
 * - The scheduler thread checks existing in progress format media to avoid
 *   launching the same format twice.
 * - The scheduler thread pushes new in progress format medium.
 * - The device thread removes formated medium once the format is done.
 */
struct format_media {
    pthread_mutex_t mutex;
    GHashTable *media_name;
};

/**
 * Remove medium from format_media
 */
void format_medium_remove(struct format_media *format_media,
                          struct media_info *medium);

/**
 * Local Resource Scheduler instance, manages media and local devices for the
 * actual IO to be performed.
 */
struct lrs_sched {
    enum rsc_family     family;         /**< Managed resource family */
    struct lrs_dev_hdl  devices;        /**< Handle to device threads */
    struct lock_handle  lock_handle;    /**< Lock information for this LRS */
    struct tsqueue      incoming;       /**< Queue of new requests to schedule
                                          */
    struct tsqueue      retry_queue;    /**< Queue of request sent back by the
                                          *  device thread on error
                                          */
    struct format_media ongoing_format; /**< Ongoing format media */
    struct tsqueue     *response_queue; /**< Queue for responses */
    struct timespec     sync_time_ms;   /**< Time threshold for medium
                                          *  synchronization
                                          */
    unsigned int        sync_nb_req;    /**< Number of requests threshold
                                          *  for medium synchronization
                                          */
    unsigned long       sync_wsize_kb;  /**< Written size threshold for
                                          *  medium synchronization
                                          */
    struct thread_info  sched_thread;   /**< thread handling the actions
                                          *  executed by the scheduler
                                          */
    struct pho_io_sched io_sched;       /**< I/O scheduler abstraction */
};

/**
 * Enumeration for medium request status.
 */
enum sub_request_status {
    SUB_REQUEST_TODO,
    SUB_REQUEST_DONE,
    SUB_REQUEST_ERROR,
    SUB_REQUEST_CANCEL,
};

/**
 * Internal structure for a to-synchronize medium.
 */
struct tosync_medium {
    enum sub_request_status status; /**< Medium synchronization status. */
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
 * Parameters of a request container dedicated to a format request
 */
struct format_params {
    struct media_info *medium_to_format; /**< medium to format */
    struct fs_adapter_module *fsa;       /**< fs_adapter_module to use for
                                           * format
                                           */
};

/**
 * Parameters of a request container dedicated to a notify request
 */
struct notify_params {
    struct lrs_dev *notified_device;    /**< Notified device, which is
                                          *  currently busy, waiting for
                                          *  a device thread to end.
                                          */
};

/**
 * Structure for a medium in a read or write allocation request
 */
struct rwalloc_medium {
    struct media_info *alloc_medium;    /**< Medium to allocate
                                          *  This field is set to NULL when
                                          *  the medium to use is the one which
                                          *  is already loaded or mounted in
                                          *  the device.
                                          */
    enum sub_request_status status;     /**< Medium request status */
};

/**
 * Parameters of a request container dedicated to a read or write alloc request
 */
struct rwalloc_params {
    struct rwalloc_medium *media;   /**< Array of media to alloc */
    size_t n_media;                 /**< Number of media to alloc */
    size_t nb_failed_media;         /**< Number of already failed read candidate
                                      *  media (unused for write request)
                                      */
    int rc;                         /**< Global return code, if multiple sub
                                      *  requests fail, only the first one is
                                      *  kept.
                                      */
    struct resp_container *respc;   /**< Response container */
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
        struct format_params format;
        struct notify_params notify;
        struct rwalloc_params rwalloc;
    } params;
};

/** sched_resp_free can be used as glib callback */
void sched_resp_free(void *respc);
void sched_resp_free_with_cont(void *respc);

/**
 * Release memory allocated for params structure of a request container.
 */
static inline void destroy_container_params(struct req_container *cont)
{
    if (pho_request_is_release(cont->req)) {
        free(cont->params.release.tosync_media);
        free(cont->params.release.nosync_media);
    } else if (pho_request_is_format(cont->req)) {
        media_info_free(cont->params.format.medium_to_format);
    } else if (pho_request_is_read(cont->req) ||
               pho_request_is_write(cont->req)) {
        struct rwalloc_params *rwalloc_params = &cont->params.rwalloc;
        size_t index;

        for (index = 0; index < rwalloc_params->n_media; index++)
            media_info_free(rwalloc_params->media[index].alloc_medium);

        free(rwalloc_params->media);
        sched_resp_free(rwalloc_params->respc);
        free(rwalloc_params->respc);
    }
}

/** sched_req_free can be used as glib callback */
void sched_req_free(void *reqc);

/**
 * Test is the rwalloc request is ended
 *
 * Must be called with lock on reqc.
 *
 * @param[in]   reqc    Request to test.
 *
 * @return  true if the request is ended, else false.
 */
bool is_rwalloc_ended(struct req_container *reqc);

/**
 * Cancel DONE devices
 *
 * Must be called with lock on reqc
 *
 * @param[in]   reqc    Request to cancel.
 */
void rwalloc_cancel_DONE_devices(struct req_container *reqc);

/**
 * Response container used by the scheduler to transfer information
 * between a request and its response. For now, this information consists
 * in a socket ID.
 */
struct resp_container {
    int socket_id;                  /**< Socket ID got from the request. */
    pho_resp_t *resp;               /**< Response. */
    struct lrs_dev **devices;       /**< List of devices which will handle the
                                      * request corresponding to this response.
                                      * (used only for read or write alloc)
                                      */
    size_t devices_len;             /**< size of \p devices */
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
 * Build and queue an error response
 *
 * @param[in,out]   response_queue  Response queue
 * @param[in]       req_rc          Error code
 * @param[in]       reqc            Request to response an error
 *
 * @return  0 on success, -1 * posix error code on failure.
 */
int queue_error_response(struct tsqueue *response_queue, int req_rc,
                         struct req_container *reqc);

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
 * Return true if no device handled by \p sched has an ongoing I/O
 *
 * \param[in]       sched       The scheduler to check
 */
bool sched_has_running_devices(struct lrs_sched *sched);

/**
 * Process release request
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
int process_release_request(struct lrs_sched *sched,
                            struct dss_handle *comm_dss,
                            struct req_container *reqc);

/**
 * Handle queued requests
 *
 * \param[in]       sched       The sched from which to handle requests
 *
 * \return                      0 on success, -1 * posix error code on failure.
 */
int sched_handle_requests(struct lrs_sched *sched);

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

int sched_handle_monitor(struct lrs_sched *sched, json_t *status);

typedef int (*device_select_func_t)(size_t required_size,
                                    struct lrs_dev *dev_curr,
                                    struct lrs_dev **dev_selected);

int select_empty_loaded_mount(size_t required_size,
                              struct lrs_dev *dev_curr,
                              struct lrs_dev **dev_selected);

struct lrs_dev *dev_picker(GPtrArray *devices,
                           enum dev_op_status op_st,
                           device_select_func_t select_func,
                           size_t required_size,
                           const struct tags *media_tags,
                           struct media_info *pmedia,
                           bool is_write);

device_select_func_t get_dev_policy(void);

struct lrs_dev *search_in_use_medium(GPtrArray *devices,
                                     const char *name,
                                     bool *sched_ready);

int sched_select_medium(struct lock_handle *lock_handle,
                        GPtrArray *devices,
                        struct media_info **p_media,
                        size_t required_size,
                        enum rsc_family family,
                        const struct tags *tags,
                        struct req_container *reqc,
                        size_t n_med,
                        size_t not_alloc);

int fetch_and_check_medium_info(struct lock_handle *lock_handle,
                                struct req_container *reqc,
                                struct pho_id *m_id,
                                size_t index,
                                struct media_info **target_medium);

#endif
