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
 * \brief  LRS Device Thread handling
 */
#ifndef _PHO_LRS_DEVICE_H
#define _PHO_LRS_DEVICE_H

#include <glib.h>
#include <pthread.h>
#include <stdbool.h>

#include "pho_ldm.h"
#include "pho_types.h"

/** Data specific to the device thread. */
struct lrs_dev {
    pthread_t            ld_tid;            /**< ID of the thread handling
                                              *  this device.
                                              */
    pthread_mutex_t      ld_signal_mutex;   /**< Mutex to protect the signal
                                              * access.
                                              */
    pthread_cond_t       ld_signal;         /**< Used to signal the thread
                                              * when new work is available.
                                              */
    bool                 ld_running;        /**< true as long as the thread
                                              * should run.
                                              */
    int                  ld_status;         /**< Return status at the end of
                                              * the execution.
                                              */
};

/** all needed information to select devices */
struct dev_descr {
    pthread_mutex_t      mutex;                 /**< exclusive access */
    struct dev_info     *dss_dev_info;          /**< device info from DSS */
    struct lib_drv_info  lib_dev_info;          /**< device info from library
                                                  *  (for tape drives)
                                                  */
    struct ldm_dev_state sys_dev_state;         /**< device info from system */

    enum dev_op_status   op_status;             /**< operational status of
                                                  *  the device
                                                  */
    char                 dev_path[PATH_MAX];    /**< path to the device */
    struct media_info   *dss_media_info;        /**< loaded media info
                                                  *  from DSS, if any
                                                  */
    char                 mnt_path[PATH_MAX];    /**< mount path
                                                  *  of the filesystem
                                                  */
    bool                 ongoing_io;            /**< one I/O is ongoing */
    bool                 needs_sync;            /**< medium needs to be sync */
    struct {
        GPtrArray        *tosync_array;         /**< array of release requests
                                                  *  with to_sync to do
                                                  */
        struct timespec  oldest_tosync;         /**< oldest release request in
                                                  *  \p tosync_array
                                                  */
        size_t           tosync_size;           /**< total size of release
                                                  *  requests in
                                                  *  \p tosync_array
                                                  */
    } sync_params;                              /**< sync information on the
                                                  *  mounted medium
                                                  */
    struct lrs_dev      device_thread;          /**< Thread specific data */
};

/**
 * Starts the device thread
 *
 * On success, a new thread will be created and will start handling pending
 * requests.
 */
int lrs_dev_init(struct dev_descr *device);

/**
 * Indicate to the device thread that it should stop.
 */
void lrs_dev_signal_stop(struct dev_descr *device);

/**
 * Wait for the device thread termination.
 */
void lrs_dev_wait_end(struct dev_descr *device);

#endif
