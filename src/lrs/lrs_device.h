/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
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
/**
 * \brief  LRS Device Thread handling
 */
#ifndef _PHO_LRS_DEVICE_H
#define _PHO_LRS_DEVICE_H

#include <glib.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdatomic.h>

#include "lrs_thread.h"

#include "pho_dss.h"
#include "pho_ldm.h"
#include "pho_types.h"

struct lrs_sched;
struct lrs_dev;

/**
 * Structure handling thread devices used by the scheduler.
 */
struct lrs_dev_hdl {
    GPtrArray      *ldh_devices;   /**< List of active devices of
                                     *  type lrs_dev
                                     */
    struct timespec sync_time_ms;  /**< Time threshold for medium
                                     *  synchronization
                                     */
    unsigned int    sync_nb_req;   /**< Number of requests
                                     *  threshold for medium
                                     *  synchronization
                                     */
    unsigned long   sync_wsize_kb; /**< Written size threshold for
                                     *  medium synchronization
                                     */
};

/** Request pushed to a device */
struct sub_request {
    struct req_container *reqc;
    size_t medium_index; /**< index of the medium in reqc that this device
                           *  must handle
                           */
    bool failure_on_medium; /**< an error occurs on medium */
};

/**
 * sub_request_free can be used as glib callback
 *
 * @param[in]   sub_req     sub_request to free
 */
void sub_request_free(struct sub_request *sub_req);

/**
 * Cancel sub_request on error
 *
 * Must be called with a lock on sub_request->reqc
 * If a previous error is detected with a non null rc in reqc, the medium of
 * this sub_request is freed and set to NULL and its status is set as
 * SUB_REQUEST_CANCEL.
 *
 * @param[in]   sub_request     the rwalloc sub_request to check
 * @param[out]  ended           Will be set to true if the request is ended and
 *                              must be freed, false otherwise.
 *
 * @return  True if there was an error and the request was cancelled,
 *          false otherwise.
 */
bool locked_cancel_rwalloc_on_error(struct sub_request *sub_request,
                                    bool *ended);

/**
 * Build a mount path for the given identifier.
 *
 * @param[in] id    Unique drive identified on the host.
 *
 * @return The result must be released by the caller using free(3).
 */
char *mount_point(const char *id);

/**
 * Load a medium into a drive or return -EBUSY to retry later
 *
 * The loaded medium is registered as dev->ld_dss_media_info. If
 * free_medium is true, medium is set to NULL.
 *
 * If an error occurs on a medium that is not registered to
 * dev->ld_dss_media_info, the medium is considered failed, marked as such in
 * the DSS, freed and set to NULL if free_medium is true. WARNING: if we
 * cannot set it to failed into the DSS, the medium DSS lock is not released.
 *
 * @return 0 on success, -error number on error. -EBUSY is returned when a
 * drive to drive medium movement was prevented by the library or if the device
 * is empty.
 */
int dev_load(struct lrs_dev *dev, struct media_info *medium);

/**
 * Format a medium to the given fs type.
 *
 * \param[in]   dev     Device with a loaded medium to format
 * \param[in]   fsa     Filesystem adapter module
 * \param[in]   unlock  Put admin status to "unlocked" on format success
 *
 * \return              0 on success, negative error code on failure
 */
int dev_format(struct lrs_dev *dev, struct fs_adapter_module *fsa, bool unlock);

/**
 * Mount the device's loaded medium
 *
 * @param[in] dev   Device already containing a loaded medium
 *
 * @return 0 on success, -error number on error.
 */
int dev_mount(struct lrs_dev *dev);

/**
 * Verify the mounted medium is writable
 *
 * @param[in] dev   Device containing a mounted medium
 *
 * @return true if the medium is writable, false otherwise
 */
bool dev_mount_is_writable(struct lrs_dev *dev);

/**
 * Umount the device's loaded medium, but let it loaded and locked
 *
 * @param[in] dev   Device already containing a loaded medium
 *
 * @return 0 on success, -error number on error.
 */
int dev_umount(struct lrs_dev *dev);

/**
 * Unload medium from device
 *
 * - DSS unlock the medium
 * - set drive's ld_op_status to PHO_DEV_OP_ST_EMPTY
 *
 * @param[in]   dev   The device to unload a medium from
 *
 * @return 0 on success, -error number on error.
 */
int dev_unload(struct lrs_dev *dev);

/**
 * Parameters to check when a synchronization is required.
 */
struct sync_params {
    GPtrArray       *tosync_array;  /**< array of release requests with to_sync
                                      *  set
                                      */
    struct timespec  oldest_tosync; /**< oldest release request in
                                      *  \p tosync_array
                                      */
    size_t           tosync_size;   /**< total size of release requests in
                                      *  \p tosync_array
                                      */
    size_t           tosync_nb_extents;
                                    /**< total number of extents written in
                                     *   \p tosync_array
                                     */
    bool             groupings_to_update;
                                    /**< A new grouping was added to the medium.
                                     *   The grouping field need to be updated.
                                     */
};

struct ongoing_grouping {
    char *grouping;  /**< NULL if no ongoing grouping */
    int socket_id;   /**< Socket id of the ongoing grouping */
};

/**
 * Data specific to the device thread.
 *
 * Note on thread safety:
 *
 * ld_dss_media_info: this field is often accessed from the scheduler or
 * communication thread to check whether a device is available and if it has a
 * medium loaded. Since struct media_info is cached across threads with
 * phobos_global_context::lrs_media_cache, extra care must be taken when
 * accessing it.
 *
 * The cache has been built so that several references to the same medium can be
 * kept in memory. This means that a thread may have an old reference to a
 * struct media_info, but as long as the reference count is not 0, the structure
 * is still valid and wont be leaked (provided that the structure is released to
 * the cache at some point). In practice, this means that as long as a piece of
 * code is interested in constant information in the media_info (e.g.
 * media_info::rsc::id::name), it will be thread safe to access it without any
 * lock as long as it has a reference on the struct media_info.
 *
 * The struct media_info has 3 main lifetimes:
 * - between the moment a medium is loaded and unloaded, this reference is
 *   associated to lrs_dev::ld_dss_media_info;
 * - the media associated to a request;
 *   For example: req_container::params::format::medium_to_format.
 * - the I/O scheduler: some algorithms may need to keep a reference to some
 *   data in the media_info.
 *   For example: in the grouped read algorithm, the life time of a queue is
 *   tied to the life time of the loaded medium: lrs_dev::ld_dss_media_info.
 *
 * Some values in struct media_info can be modified from different threads.
 * This is only true for a struct media_info that is loaded on a device (i.e. a
 * reference is stored on lrs_dev::ld_dss_media_info). Fields of this struct
 * must be modified with the device lock held. Therefore, the following fields
 * must be manipulated with extra care:
 *
 * - media_info::stats
 *
 * lrs_dev::ld_dss_media_info is set to NULL when unloading the medium. The
 * device lock is held during this operation. Another thread that wants to
 * access this pointer must take the device lock then a reference on the medium
 * after checking that it is not NULL. atomic_dev_medium_get does this operation
 * and returns a pointer to the media_info. This pointer may be NULL if the
 * medium was unloaded. The returned medium has an increased reference count so
 * that it is still in the cache for use by the caller.
 */
struct lrs_dev {
    pthread_mutex_t      ld_mutex;              /**< exclusive access */
    struct dev_info     *ld_dss_dev_info;       /**< device info from DSS */
    struct lib_drv_info  ld_lib_dev_info;       /**< device info from library
                                                  *  (for tape drives)
                                                  */
    struct ldm_dev_state ld_sys_dev_state;      /**< device info from system */

    _Atomic enum dev_op_status   ld_op_status;  /**< operational status of the
                                                  * device
                                                  */
    char                 ld_dev_path[PATH_MAX]; /**< path to the device */
    /** loaded media info from DSS, if any */
    struct media_info   *_Atomic ld_dss_media_info;
    char                 ld_mnt_path[PATH_MAX]; /**< mount path of the
                                                  * filesystem
                                                  */
    struct sub_request  *ld_sub_request;        /**< sub request to handle */
    atomic_bool          ld_ongoing_scheduled;  /**< one I/O is going to be
                                                  *  scheduled
                                                  */
    atomic_bool          ld_ongoing_io;         /**< one I/O is ongoing */
    struct ongoing_grouping ld_ongoing_grouping; /**< track on going grouping */
    atomic_bool          ld_needs_sync;         /**< medium needs to be sync */
    struct thread_info   ld_device_thread;      /**< thread handling the actions
                                                  * executed on the device
                                                  */
    struct sync_params   ld_sync_params;        /**< pending synchronization
                                                  * requests
                                                  */
    struct tsqueue      *ld_response_queue;     /**< reference to the response
                                                  * queue
                                                  */
    struct format_media *ld_ongoing_format;     /**< reference to the ongoing
                                                  * format array
                                                  */
    /* TODO: move sched_req_queue use to sched_retry_queue */
    struct tsqueue      *sched_req_queue;       /**< reference to the sched
                                                  * request queue
                                                  */
    struct tsqueue      *sched_retry_queue;     /**< reference to the sched
                                                  * retry queue
                                                  */
    struct lrs_dev_hdl  *ld_handle;
    int                  ld_io_request_type;
        /**< OR-ed enum io_request_type indicating which schedulers currently
         * have access to this device. Modified by
         * io_sched_handle::dispatch_devices.
         */
    int                  ld_last_client_rc;     /**< last I/O error of a client
                                                  *  sent on release.
                                                  */
    const char *ld_technology; /** The technology of the device. For tapes, this
                                 * corresponds the tape generation (e.g. LTO5).
                                 * For dir, it is NULL. This information is only
                                 * used by the fair share dispatch_devices
                                 * algorithm for now.
                                 */
};

static inline bool dev_is_failed(struct lrs_dev *dev)
{
    return dev->ld_op_status == PHO_DEV_OP_ST_FAILED;
}

static inline bool dev_is_loaded(struct lrs_dev *dev)
{
    return dev->ld_op_status == PHO_DEV_OP_ST_LOADED;
}

static inline bool dev_is_mounted(struct lrs_dev *dev)
{
    return dev->ld_op_status == PHO_DEV_OP_ST_MOUNTED;
}

static inline bool dev_is_empty(struct lrs_dev *dev)
{
    return dev->ld_op_status == PHO_DEV_OP_ST_EMPTY;
}

static inline bool dev_is_release_ready(struct lrs_dev *dev)
{
    return dev && !thread_is_stopped(&dev->ld_device_thread);
}

bool is_request_tosync_ended(struct req_container *req);

void queue_release_response(struct tsqueue *response_queue,
                            struct req_container *reqc);

static inline bool dev_is_sched_ready(struct lrs_dev *dev)
{
    return dev && thread_is_running(&dev->ld_device_thread) &&
           !dev->ld_ongoing_io && !dev->ld_needs_sync && !dev->ld_sub_request &&
           !dev->ld_ongoing_scheduled &&
           !dev_is_failed(dev) &&
           (dev->ld_dss_dev_info->rsc.adm_status == PHO_RSC_ADM_ST_UNLOCKED);
}

static inline bool dev_is_online(struct lrs_dev *dev)
{
    return dev && thread_is_running(&dev->ld_device_thread) &&
        (dev->ld_dss_dev_info->rsc.adm_status == PHO_RSC_ADM_ST_UNLOCKED);
}

static inline bool is_device_shared_between_schedulers(struct lrs_dev *dev)
{
    return __builtin_popcount(dev->ld_io_request_type & 0b111) != 0;
}

/**
 * Add a new sync request to a device
 *
 * Must be called with the device lock.
 *
 * \param[in,out]   dev     device to add the sync request
 * \param[in]       reqc    sync request to add
 * \param[in]       medium  index in reqc of the medium to sync
 */
void push_new_sync_to_device(struct lrs_dev *dev, struct req_container *reqc,
                             size_t medium_index);

/**
 * Synchronize the medium of a device
 *
 * \param[in]   dev    device containing the medium to synchronize
 */
int medium_sync(struct lrs_dev *dev);

/**
 * Initialize an lrs_dev_hdl to manipulate devices from the scheduler
 *
 * \param[out]   handle   pointer to an uninitialized handle
 * \param[in]    family   family of the devices handled
 *
 * \return                0 on success, -errno on failure
 */
int lrs_dev_hdl_init(struct lrs_dev_hdl *handle, enum rsc_family family);

/**
 * Undo the work done by lrs_dev_hdl_init
 *
 * \param[in]    handle   pointer to an initialized handle
 */
void lrs_dev_hdl_fini(struct lrs_dev_hdl *handle);

/**
 * Creates a new device thread and add it to the list of registered devices
 *
 * \param[in]    sched    scheduler managing the device
 * \param[in]    handle   initialized device handle
 * \param[in]    name     serial number of the device
 * \param[in]    library  library of the device
 *
 * \return                0 on success, -errno no failure
 */
int lrs_dev_hdl_add(struct lrs_sched *sched,
                    struct lrs_dev_hdl *handle,
                    const char *name, const char *library);

/**
 * Undo the work done by lrs_dev_hdl_add
 *
 * This function is blocking as it waits for the end of the device thread.
 *
 * \param[in]    handle   initialized device handle
 * \param[in]    index    index of the device to remove from the list
 * \param[in]    rc       error which caused the thread to stop
 * \param[in]    sched    reference to the scheduler that owns the device handle
 *
 * \return                0 on success, -errno no failure
 */
int lrs_dev_hdl_del(struct lrs_dev_hdl *handle, int index, int rc,
                    struct lrs_sched *sched);

/**
 * Will try to remove a device thread context.
 *
 * If the device thread is still busy after 100ms, we delay the removal.
 *
 * \param[in]   handle      Device handle.
 * \param[in]   index       Index of the device to remove from the list.
 *
 * \return                  0 on success,
 *                         -EAGAIN if the thread is still busy,
 *                         -errno on failure.
 */
int lrs_dev_hdl_trydel(struct lrs_dev_hdl *handle, int index);

/**
 * Retry to remove a device thread context.
 *
 * If the device thread is still busy, we delay the removal.
 *
 * \param[in]   handle      Device handle.
 * \param[in]   dev         Device to remove from the list.
 *
 * \return                  0 on success,
 *                         -EAGAIN if the thread is still busy,
 *                         -errno on failure.
 */
int lrs_dev_hdl_retrydel(struct lrs_dev_hdl *handle, struct lrs_dev *dev);

/**
 * Load all the devices that are attributed to this LRS from the DSS
 *
 * \param[in]      sched    scheduler managing the device
 * \param[in/out]  handle   initialized device handle
 *
 * \return                0 on success, -errno no failure
 */
int lrs_dev_hdl_load(struct lrs_sched *sched,
                     struct lrs_dev_hdl *handle);

/**
 * Remove all the devices from the handle
 *
 * This function is blocking as it waits for the termination of all threads.
 * Each thread is signaled first and then joined so that they are stopped
 * concurrently.
 *
 * \param[in]  handle  pointer to an initialized handle to clear
 * \param[in]  sched   reference to the scheduler that owns the device handle
 */
void lrs_dev_hdl_clear(struct lrs_dev_hdl *handle, struct lrs_sched *sched);

/**
 * Wrapper arround GLib's getter to retrive devices' structures
 *
 * \param[in]  handle  initialized device handle
 * \param[in]  index   index of the device, must be smaller than the number of
 *                     devices in handle->ldh_devices
 *
 * \return             a pointer to the requested device is returned
 */
struct lrs_dev *lrs_dev_hdl_get(struct lrs_dev_hdl *handle, int index);

/**
 * Wrap library open operations
 *
 * @param[in]   dev_type    Device type
 * @param[in]   library     Library
 * @param[out]  lib_hdl     Library handle
 *
 * @return          0 on success, -1 * posix error code on failure.
 */
int wrap_lib_open(enum rsc_family dev_type, const char *library,
                  struct lib_handle *lib_hdl);

/**
 * Returns the technology of a drive from its model using the configuration for
 * the association.
 *
 * \param[in]  dev     device whose technology name to get
 * \param[out] techno  allocated technology of the device (must be passed to
 *                     free)
 *
 * \return             0 on success, negative POSIX error code on failure
 *            -ENODATA the model of the device was not found in the
 *                     configuration
 */
int lrs_dev_technology(const struct lrs_dev *dev, const char **techno);

static inline const char *lrs_dev_name(struct lrs_dev *dev)
{
    return dev->ld_dss_dev_info->rsc.id.name;
}

static inline const struct pho_id *lrs_dev_id(struct lrs_dev *dev)
{
    return &dev->ld_dss_dev_info->rsc.id;
}

static inline const struct pho_id *lrs_dev_med_id(struct lrs_dev *dev)
{
    return &dev->ld_dss_media_info->rsc.id;
}

/* Swap the medium lrs_dev::ld_dss_media_info with \p medium while holding the
 * device lock.
 *
 * Release the reference of the old lrs_dev::ld_dss_media_info.
 */
void atomic_dev_medium_swap(struct lrs_dev *device, struct media_info *medium);

/* Update the value of lrs_dev::ld_dss_media_info in the medium cache and put it
 * in the device atomically
 */
void atomic_dev_update_loaded_medium(struct lrs_dev *device);

struct media_info *atomic_dev_medium_get(struct lrs_dev *device);

ssize_t atomic_dev_medium_phys_space_free(struct lrs_dev *dev);

void fail_release_medium(struct lrs_dev *dev, struct media_info *medium);

#endif
