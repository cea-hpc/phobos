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
 * \brief  Phobos Local Resource Scheduler (LRS)
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "lrs_cfg.h"
#include "lrs_device.h"
#include "lrs_sched.h"

#include "pho_common.h"
#include "pho_type_utils.h"

/* XXX: this should probably be alligned with the synchronization timeout */
#define DEVICE_THREAD_WAIT_MS 1000

static int dev_thread_init(struct lrs_dev *device);
static void sync_params_init(struct sync_params *params);

static inline long ms2sec(long ms)
{
    return ms / 1000;
}

static inline long ms2nsec(long ms)
{
    return (ms % 1000) * 1000000;
}

int lrs_dev_hdl_init(struct lrs_dev_hdl *handle, enum rsc_family family)
{
    int rc;

    handle->ldh_devices = g_ptr_array_new();

    rc = get_cfg_time_threshold_value(family, &handle->sync_time_threshold);
    if (rc)
        return rc;

    rc = get_cfg_nb_req_threshold_value(family, &handle->sync_nb_req_threshold);
    if (rc)
        return rc;

    rc = get_cfg_written_size_threshold_value(
        family, &handle->sync_written_size_threshold);
    if (rc)
        return rc;

    return 0;
}

void lrs_dev_hdl_fini(struct lrs_dev_hdl *handle)
{
    g_ptr_array_unref(handle->ldh_devices);
}

static int lrs_dev_init_from_info(struct lrs_dev_hdl *handle,
                                  struct dev_info *info,
                                  struct lrs_dev **dev)
{
    int rc;

    *dev = calloc(1, sizeof(**dev));
    if (!*dev)
        return -errno;

    (*dev)->ld_dss_dev_info = dev_info_dup(info);
    if (!(*dev)->ld_dss_dev_info) {
        free(*dev);
        return -ENOMEM;
    }

    sync_params_init(&(*dev)->ld_sync_params);

    rc = dev_thread_init(*dev);
    if (rc) {
        g_ptr_array_unref((*dev)->ld_sync_params.tosync_array);
        dev_info_free((*dev)->ld_dss_dev_info, 1);
        free(*dev);
        return rc;
    }

    g_ptr_array_add(handle->ldh_devices, *dev);

    /* Explicitly initialize to NULL so that lrs_dev_info_clean can call cleanup
     * functions.
     */
    (*dev)->ld_dss_media_info = NULL;
    (*dev)->ld_sys_dev_state.lds_model = NULL;
    (*dev)->ld_sys_dev_state.lds_serial = NULL;

    return 0;
}

static void lrs_dev_info_clean(struct lrs_dev_hdl *handle,
                               struct lrs_dev *dev)
{
    dev_thread_wait_end(dev);

    media_info_free(dev->ld_dss_media_info);
    dev->ld_dss_media_info = NULL;

    ldm_dev_state_fini(&dev->ld_sys_dev_state);

    g_ptr_array_foreach(dev->ld_sync_params.tosync_array,
                        sched_request_tosync_free_wrapper,
                        NULL);
    g_ptr_array_unref(dev->ld_sync_params.tosync_array);
    dev_info_free(dev->ld_dss_dev_info, 1);
    dss_fini(&dev->ld_device_thread.ld_dss);

    free(dev);
}

int lrs_dev_hdl_add(struct lrs_sched *sched,
                    struct lrs_dev_hdl *handle,
                    const char *name)
{
    struct dev_info *dev_list = NULL;
    struct dss_filter filter;
    struct lrs_dev *dev;
    int dev_count = 0;
    int rc;

    rc = dss_filter_build(&filter,
                          "{\"$AND\": ["
                          "  {\"DSS::DEV::host\": \"%s\"},"
                          "  {\"DSS::DEV::family\": \"%s\"},"
                          "  {\"DSS::DEV::serial\": \"%s\"},"
                          "  {\"DSS::DEV::adm_status\": \"%s\"}"
                          "]}",
                          sched->lock_hostname,
                          rsc_family2str(sched->family),
                          name,
                          rsc_adm_status2str(PHO_RSC_ADM_ST_UNLOCKED));
    if (rc)
        return rc;

    rc = dss_device_get(&sched->dss, &filter, &dev_list, &dev_count);
    dss_filter_free(&filter);
    if (rc)
        return rc;

    if (dev_count == 0) {
        pho_info("Device (%s:%s) not found: check device status and host",
                 rsc_family2str(sched->family), name);
        rc = -ENXIO;
        goto free_list;
    }

    rc = lrs_dev_init_from_info(handle, dev_list, &dev);
    if (rc)
        goto free_list;

    rc = dss_init(&dev->ld_device_thread.ld_dss);
    if (rc) {
        g_ptr_array_remove_index_fast(handle->ldh_devices,
                                      handle->ldh_devices->len - 1);
        lrs_dev_info_clean(handle, dev);
        goto free_list;
    }
    dev->ld_response_queue = sched->response_queue;
    dev->ld_handle = handle;

    rc = check_and_take_device_lock(sched, dev_list);
    if (rc)
        lrs_dev_hdl_del(handle, handle->ldh_devices->len);

free_list:
    dss_res_free(dev_list, dev_count);
    return rc;
}

int lrs_dev_hdl_del(struct lrs_dev_hdl *handle, int index)
{
    struct lrs_dev *dev;

    if (index >= handle->ldh_devices->len)
        return -ERANGE;

    dev = (struct lrs_dev *)g_ptr_array_remove_index_fast(handle->ldh_devices,
                                                          index);

    dev_thread_signal_stop(dev);
    lrs_dev_info_clean(handle, dev);

    return 0;
}

int lrs_dev_hdl_load(struct lrs_sched *sched,
                     struct lrs_dev_hdl *handle)
{
    struct dev_info *dev_list = NULL;
    struct dss_filter filter;
    int dev_count = 0;
    int rc;
    int i;

    rc = dss_filter_build(&filter,
                          "{\"$AND\": ["
                          "  {\"DSS::DEV::host\": \"%s\"},"
                          "  {\"DSS::DEV::adm_status\": \"%s\"},"
                          "  {\"DSS::DEV::family\": \"%s\"}"
                          "]}",
                          sched->lock_hostname,
                          rsc_adm_status2str(PHO_RSC_ADM_ST_UNLOCKED),
                          rsc_family2str(sched->family));
    if (rc)
        return rc;

    /* get all admin unlocked devices from DB for the given family */
    rc = dss_device_get(&sched->dss, &filter, &dev_list, &dev_count);
    dss_filter_free(&filter);
    if (rc)
        return rc;

    for (i = 0; i < dev_count; i++) {
        struct lrs_dev *dev;
        int rc2;

        rc2 = lrs_dev_init_from_info(handle, &dev_list[i], &dev);
        if (rc2) {
            rc = rc ? : rc2;
            continue;
        }

        rc2 = dss_init(&dev->ld_device_thread.ld_dss);
        if (rc2) {
            rc = rc ? : rc2;
            g_ptr_array_remove_index_fast(handle->ldh_devices,
                                          handle->ldh_devices->len - 1);
            lrs_dev_info_clean(handle, dev);
            continue;
        }

        dev->ld_response_queue = sched->response_queue;
        dev->ld_handle = handle;

        rc2 = check_and_take_device_lock(sched, &dev_list[i]);
        if (rc2) {
            lrs_dev_hdl_del(handle, handle->ldh_devices->len - 1);
            rc = rc ? : rc2;
        }
    }

    if (handle->ldh_devices->len == 0)
        rc = -ENXIO;

    dss_res_free(dev_list, dev_count);
    return rc;
}

void lrs_dev_hdl_clear(struct lrs_dev_hdl *handle)
{
    int i;

    for (i = 0; i < handle->ldh_devices->len; i++) {
        struct lrs_dev *dev;

        dev = (struct lrs_dev *)g_ptr_array_index(handle->ldh_devices, i);
        dev_thread_signal_stop(dev);
    }

    for (i = handle->ldh_devices->len - 1; i >= 0; i--) {
        struct lrs_dev *dev;

        dev = (struct lrs_dev *)g_ptr_array_remove_index(handle->ldh_devices,
                                                         i);
        lrs_dev_info_clean(handle, dev);
    }
}

struct lrs_dev *lrs_dev_hdl_get(struct lrs_dev_hdl *handle, int index)
{
    return g_ptr_array_index(handle->ldh_devices, index);
}

static void sync_params_init(struct sync_params *params)
{
    params->tosync_array = g_ptr_array_new();
    params->oldest_tosync.tv_sec = 0;
    params->oldest_tosync.tv_nsec = 0;
    params->tosync_size = 0;
}

/**
 * Signal the thread
 *
 * \return 0 on success, -1 * ERROR_CODE on failure
 */
static int lrs_dev_signal(struct thread_info *thread)
{
    int rc;

    MUTEX_LOCK(&thread->ld_signal_mutex);

    rc = pthread_cond_signal(&thread->ld_signal);
    if (rc)
        pho_error(-rc, "Unable to signal device");

    MUTEX_UNLOCK(&thread->ld_signal_mutex);

    return -rc;
}

/* On success, it returns:
 * - ETIMEDOUT  the thread received no signal before the timeout
 * - 0          the thread received a signal
 *
 * Negative error codes reported by this function are fatal for the thread.
 */
static int wait_for_signal(struct thread_info *thread)
{
    struct timespec time;
    int rc;

    /* This should not fail */
    rc = clock_gettime(CLOCK_REALTIME, &time);
    if (rc)
        LOG_RETURN(-errno, "clock_gettime: unable to get CLOCK_REALTIME");

    time.tv_sec += ms2sec(DEVICE_THREAD_WAIT_MS);
    time.tv_nsec += ms2nsec(DEVICE_THREAD_WAIT_MS);
    time.tv_sec += (time.tv_nsec / 1000000000);
    time.tv_nsec %= 1000000000;

    MUTEX_LOCK(&thread->ld_signal_mutex);
    rc = pthread_cond_timedwait(&thread->ld_signal,
                                &thread->ld_signal_mutex,
                                &time);
    MUTEX_UNLOCK(&thread->ld_signal_mutex);
    if (rc != ETIMEDOUT)
        rc = -rc;

    return rc;
}

/**
 * Main device thread loop.
 */
static void *lrs_dev_thread(void *tdata)
{
    struct lrs_dev *device = (struct lrs_dev *)tdata;
    struct thread_info *thread;

    thread = &device->ld_device_thread;

    while (thread->ld_running) {
        int rc;

        dev_check_sync_cancel(device);

        if (!device->ld_needs_sync)
            check_needs_sync(device->ld_handle, device);

        if (device->ld_needs_sync && !device->ld_ongoing_io)
            dev_sync(device);

        rc = wait_for_signal(thread);

        if (rc < 0)
            LOG_GOTO(end_thread, thread->ld_status = rc,
                     "device thread '%s': fatal error",
                     device->ld_dss_dev_info->rsc.id.name);
    }

end_thread:
    pthread_exit(&thread->ld_status);
}

static int dev_thread_init(struct lrs_dev *device)
{
    struct thread_info *thread = &device->ld_device_thread;
    int rc;

    pthread_mutex_init(&device->ld_mutex, NULL);
    pthread_mutex_init(&thread->ld_signal_mutex, NULL);
    pthread_cond_init(&thread->ld_signal, NULL);

    thread->ld_running = true;
    thread->ld_status = 0;

    rc = pthread_create(&thread->ld_tid, NULL, lrs_dev_thread, device);
    if (rc)
        LOG_RETURN(rc, "Could not create device thread");

    return 0;
}

void dev_thread_signal(struct lrs_dev *device)
{
    struct thread_info *thread = &device->ld_device_thread;
    int rc;

    rc = lrs_dev_signal(thread);
    if (rc)
        pho_error(rc, "Error when signaling device (%s, %s) to wake up",
                  device->ld_dss_dev_info->rsc.id.name,
                  device->ld_dev_path);
}

void dev_thread_signal_stop(struct lrs_dev *device)
{
    struct thread_info *thread = &device->ld_device_thread;
    int rc;

    thread->ld_running = false;
    rc = lrs_dev_signal(thread);
    if (rc)
        pho_error(rc, "Error when signaling device (%s, %s) to stop it",
                  device->ld_dss_dev_info->rsc.id.name, device->ld_dev_path);
}

void dev_thread_wait_end(struct lrs_dev *device)
{
    struct thread_info *thread = &device->ld_device_thread;
    int *threadrc = NULL;
    int rc;

    rc = pthread_join(thread->ld_tid, (void **)&threadrc);
    if (rc)
        pho_error(rc, "Error while waiting for device thread");

    if (*threadrc < 0)
        pho_error(*threadrc, "device thread '%s' terminated with error",
                  device->ld_dss_dev_info->rsc.id.name);
}
