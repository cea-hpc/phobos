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

#include <assert.h>

#include "lrs_cfg.h"
#include "lrs_device.h"
#include "lrs_sched.h"

#include "pho_common.h"
#include "pho_daemon.h"
#include "pho_dss.h"
#include "pho_io.h"
#include "pho_ldm.h"
#include "pho_srl_common.h"
#include "pho_type_utils.h"

struct medium_switch_context {
    /** Whether the device is responsible for the error or not.  */
    bool failure_on_device;
    /** cf. release_medium_on_dev_only_failure in dev_load */
    bool release_medium;
    /** cf. free_medium in dev_load */
    bool free_medium;
};

static int dev_medium_switch(struct lrs_dev *dev,
                             struct media_info *medium_to_load,
                             struct medium_switch_context *context);
static int dev_medium_switch_mount(struct lrs_dev *dev,
                                   struct media_info *medium_to_load,
                                   struct medium_switch_context *context);
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

    rc = get_cfg_sync_time_ms_value(family, &handle->sync_time_ms);
    if (rc)
        return rc;

    rc = get_cfg_sync_nb_req_value(family, &handle->sync_nb_req);
    if (rc)
        return rc;

    rc = get_cfg_sync_wsize_value(family, &handle->sync_wsize_kb);
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
                                  struct lrs_dev **dev,
                                  struct lrs_sched *sched)
{
    int rc;

    *dev = xcalloc(1, sizeof(**dev));

    (*dev)->ld_dss_dev_info = dev_info_dup(info);
    if (!(*dev)->ld_dss_dev_info)
        GOTO(err_dev, rc = -ENOMEM);

    sync_params_init(&(*dev)->ld_sync_params);

    rc = dss_init(&(*dev)->ld_device_thread.dss);
    if (rc)
        GOTO(err_info, rc);

    (*dev)->ld_response_queue = sched->response_queue;
    (*dev)->ld_ongoing_format = &sched->ongoing_format;
    (*dev)->sched_req_queue = &sched->incoming;
    (*dev)->sched_retry_queue = &sched->retry_queue;
    (*dev)->ld_handle = handle;
    (*dev)->ld_sub_request = NULL;
    (*dev)->ld_mnt_path[0] = 0;

    if ((*dev)->ld_dss_dev_info->rsc.model) {
        /* not every family has a model set */
        rc = lrs_dev_technology(*dev, &(*dev)->ld_technology);
        if (rc && rc != -ENODATA)
            /* Do not terminate the device thread if the model is not found in
             * the configuration. Only the fair_share algorithm needs it.
             */
            LOG_GOTO(err_dss, rc, "Failed to read device technology");

        rc = 0; /* -ENODATA is not an error here */
    }

    rc = dev_thread_init(*dev);
    if (rc)
        GOTO(err_techno, rc);

    g_ptr_array_add(handle->ldh_devices, *dev);

    /* Explicitly initialize to NULL so that lrs_dev_info_clean can call cleanup
     * functions.
     */
    (*dev)->ld_dss_media_info = NULL;
    (*dev)->ld_sys_dev_state.lds_model = NULL;
    (*dev)->ld_sys_dev_state.lds_serial = NULL;

    return 0;

err_techno:
    free((void *)(*dev)->ld_technology);
err_dss:
    dss_fini(&(*dev)->ld_device_thread.dss);
err_info:
    g_ptr_array_free((*dev)->ld_sync_params.tosync_array, true);
    dev_info_free((*dev)->ld_dss_dev_info, 1);
err_dev:
    free(*dev);

    return rc;
}

/* sub_request_free can be used as glib callback */
void sub_request_free(struct sub_request *sub_req)
{
    if (!sub_req)
        return;

    sched_req_free(sub_req->reqc);
    free(sub_req);
}

static void sub_request_free_wrapper(gpointer sub_req, gpointer _null_user_data)
{
    (void)_null_user_data;

    sub_request_free((struct sub_request *)sub_req);
}

static void lrs_dev_info_clean(struct lrs_dev_hdl *handle,
                               struct lrs_dev *dev)
{
    free((void *)dev->ld_technology);
    media_info_free(dev->ld_dss_media_info);
    dev->ld_dss_media_info = NULL;

    ldm_dev_state_fini(&dev->ld_sys_dev_state);

    g_ptr_array_foreach(dev->ld_sync_params.tosync_array,
                        sub_request_free_wrapper, NULL);
    g_ptr_array_unref(dev->ld_sync_params.tosync_array);
    sub_request_free(dev->ld_sub_request);
    dev_info_free(dev->ld_dss_dev_info, 1);
    dss_fini(&dev->ld_device_thread.dss);

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
                          sched->lock_handle.lock_hostname,
                          rsc_family2str(sched->family),
                          name,
                          rsc_adm_status2str(PHO_RSC_ADM_ST_UNLOCKED));
    if (rc)
        return rc;

    rc = dss_device_get(&sched->sched_thread.dss, &filter, &dev_list,
                        &dev_count);
    dss_filter_free(&filter);
    if (rc)
        return rc;

    if (dev_count == 0) {
        pho_info("Device (%s:%s) not found: check device status and host",
                 rsc_family2str(sched->family), name);
        rc = -ENXIO;
        goto free_list;
    }

    rc = lrs_dev_init_from_info(handle, dev_list, &dev, sched);
    if (rc)
        goto free_list;

    rc = check_and_take_device_lock(sched, dev_list);
    if (rc)
        lrs_dev_hdl_del(handle, handle->ldh_devices->len, rc, sched);

free_list:
    dss_res_free(dev_list, dev_count);
    return rc;
}

int lrs_dev_hdl_del(struct lrs_dev_hdl *handle, int index, int rc,
                    struct lrs_sched *sched)
{
    struct lrs_dev *dev;

    if (index >= handle->ldh_devices->len)
        return -ERANGE;

    dev = (struct lrs_dev *)g_ptr_array_remove_index_fast(handle->ldh_devices,
                                                          index);

    thread_signal_stop_on_error(&dev->ld_device_thread, rc);
    rc = thread_wait_end(&dev->ld_device_thread);
    if (rc < 0)
        pho_error(rc, "device thread '%s' terminated with error",
                  dev->ld_dss_dev_info->rsc.id.name);

    io_sched_remove_device(&sched->io_sched_hdl, dev);
    lrs_dev_info_clean(handle, dev);

    return 0;
}

int lrs_dev_hdl_trydel(struct lrs_dev_hdl *handle, int index)
{
    struct timespec wait_for_fast_del = {
        .tv_sec = 0,
        .tv_nsec = 100000000
    };
    int *threadrc = NULL;
    struct timespec now;
    struct lrs_dev *dev;
    int rc;

    if (index >= handle->ldh_devices->len)
        return -ERANGE;

    dev = lrs_dev_hdl_get(handle, index);
    thread_signal_stop(&dev->ld_device_thread);

    rc = clock_gettime(CLOCK_REALTIME, &now);
    if (rc) {
        rc = pthread_tryjoin_np(dev->ld_device_thread.tid,
                                (void **)&threadrc);
    } else {
        struct timespec deadline;

        deadline = add_timespec(&now, &wait_for_fast_del);
        rc = pthread_timedjoin_np(dev->ld_device_thread.tid,
                                  (void **)&threadrc, &deadline);
    }

    if (rc == EBUSY || rc == ETIMEDOUT)
        return -EAGAIN;
    if (rc)
        return -rc;

    if (*threadrc < 0)
        pho_error(*threadrc, "device thread '%s' terminated with error",
                  dev->ld_dss_dev_info->rsc.id.name);

    g_ptr_array_remove_fast(handle->ldh_devices, dev);
    lrs_dev_info_clean(handle, dev);

    return 0;
}

int lrs_dev_hdl_retrydel(struct lrs_dev_hdl *handle, struct lrs_dev *dev)
{
    int *threadrc;
    int rc;

    rc = pthread_tryjoin_np(dev->ld_device_thread.tid, (void **)&threadrc);

    if (rc == EBUSY)
        return -EAGAIN;
    if (rc)
        return -rc;

    if (*threadrc < 0)
        pho_error(*threadrc, "device thread '%s' terminated with error",
                  dev->ld_dss_dev_info->rsc.id.name);

    g_ptr_array_remove_fast(handle->ldh_devices, dev);
    lrs_dev_info_clean(handle, dev);

    return 0;
}

int lrs_dev_hdl_load(struct lrs_sched *sched, struct lrs_dev_hdl *handle)
{
    struct dev_info *dev_list = NULL;
    int dev_count = 0;
    int rc;
    int i;

    /* get all admin unlocked devices from DB for the given family */
    rc = dss_get_usable_devices(&sched->sched_thread.dss, sched->family,
                                sched->lock_handle.lock_hostname,
                                &dev_list, &dev_count);
    if (rc)
        return rc;

    for (i = 0; i < dev_count; i++) {
        struct lrs_dev *dev;
        int rc2;

        rc2 = lrs_dev_init_from_info(handle, &dev_list[i], &dev, sched);
        if (rc2) {
            rc = rc ? : rc2;
            continue;
        }

        rc2 = check_and_take_device_lock(sched, &dev_list[i]);
        if (rc2) {
            lrs_dev_hdl_del(handle, handle->ldh_devices->len - 1, rc2, sched);
            rc = rc ? : rc2;
        }
    }

    if (handle->ldh_devices->len == 0)
        rc = -ENXIO;

    dss_res_free(dev_list, dev_count);
    return rc;
}

void lrs_dev_hdl_clear(struct lrs_dev_hdl *handle, struct lrs_sched *sched)
{
    int rc;
    int i;

    for (i = 0; i < handle->ldh_devices->len; i++) {
        struct lrs_dev *dev;

        dev = (struct lrs_dev *)g_ptr_array_index(handle->ldh_devices, i);
        thread_signal_stop(&dev->ld_device_thread);
    }

    for (i = handle->ldh_devices->len - 1; i >= 0; i--) {
        struct lrs_dev *dev;

        dev = (struct lrs_dev *)g_ptr_array_remove_index(handle->ldh_devices,
                                                         i);
        rc = thread_wait_end(&dev->ld_device_thread);
        if (rc < 0)
            pho_error(rc, "device thread '%s' terminated with error",
                      dev->ld_dss_dev_info->rsc.id.name);
        io_sched_remove_device(&sched->io_sched_hdl, dev);
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

static const struct timespec MINSLEEP = {
    .tv_sec = 0,
    .tv_nsec = 10000000, /* 10 ms */
};

static int compute_wakeup_date(struct lrs_dev *dev, struct timespec *date)
{
    struct timespec *oldest_tosync = &dev->ld_sync_params.oldest_tosync;
    struct timespec diff;
    struct timespec now;
    int rc;

    rc = clock_gettime(CLOCK_REALTIME, &now);
    if (rc)
        LOG_RETURN(-errno, "clock_gettime: unable to get CLOCK_REALTIME");

    if (oldest_tosync->tv_sec == 0 && oldest_tosync->tv_nsec == 0) {
        *date = add_timespec(&now, &dev->ld_handle->sync_time_ms);
    } else {
        *date = add_timespec(oldest_tosync, &dev->ld_handle->sync_time_ms);

        diff = diff_timespec(date, &now);
        if (cmp_timespec(&diff, &MINSLEEP) == -1)
            *date = add_timespec(&MINSLEEP, &now);
    }

    return 0;
}

/* On success, it returns:
 * - ETIMEDOUT  the thread received no signal before the timeout
 * - 0          the thread received a signal
 *
 * Negative error codes reported by this function are fatal for the thread.
 */
static int dev_wait_for_signal(struct lrs_dev *dev)
{
    struct timespec time;
    int rc;

    rc = compute_wakeup_date(dev, &time);
    if (rc)
        return rc;

    return thread_signal_timed_wait(&dev->ld_device_thread, &time);
}

void queue_release_response(struct tsqueue *response_queue,
                            struct req_container *reqc)
{
    struct tosync_medium *tosync_media = reqc->params.release.tosync_media;
    size_t n_tosync_media = reqc->params.release.n_tosync_media;
    struct resp_container *respc = NULL;
    pho_resp_release_t *resp_release;
    size_t i;

    respc = xmalloc(sizeof(*respc));
    respc->socket_id = reqc->socket_id;
    respc->resp = xmalloc(sizeof(*respc->resp));

    pho_srl_response_release_alloc(respc->resp, n_tosync_media);

    /* Build the answer */
    respc->resp->req_id = reqc->req->id;
    resp_release = respc->resp->release;
    for (i = 0; i < n_tosync_media; i++) {
        resp_release->med_ids[i]->family = tosync_media[i].medium.family;
        resp_release->med_ids[i]->name =
            xstrdup_safe(tosync_media[i].medium.name);
    }

    tsqueue_push(response_queue, respc);
}

/* This function MUST be called with a lock on \p req */
bool is_request_tosync_ended(struct req_container *req)
{
    size_t i;

    for (i = 0; i < req->params.release.n_tosync_media; i++)
        if (req->params.release.tosync_media[i].status == SUB_REQUEST_TODO)
            return false;

    return true;
}

/**
 *  TODO: will become a device thread static function when all media operations
 *  will be moved to device thread
 */
void clean_tosync_array(struct lrs_dev *dev, int rc)
{
    GPtrArray *tosync_array = dev->ld_sync_params.tosync_array;

    MUTEX_LOCK(&dev->ld_mutex);
    while (tosync_array->len) {
        struct sub_request *req = tosync_array->pdata[tosync_array->len - 1];
        struct tosync_medium *tosync_medium =
            &req->reqc->params.release.tosync_media[req->medium_index];
        bool should_send_error = false;
        bool is_tosync_ended = false;

        g_ptr_array_remove_index(tosync_array, tosync_array->len - 1);

        MUTEX_LOCK(&req->reqc->mutex);

        if (!rc) {
            tosync_medium->status = SUB_REQUEST_DONE;
        } else {
            if (!req->reqc->params.release.rc) {
                /* this is the first ERROR of this request */
                req->reqc->params.release.rc = rc;
                should_send_error = true;
            }

            tosync_medium->status = SUB_REQUEST_ERROR;
        }

        is_tosync_ended = is_request_tosync_ended(req->reqc);

        MUTEX_UNLOCK(&req->reqc->mutex);

        if (should_send_error)
            queue_error_response(dev->ld_response_queue, rc, req->reqc);

        if (is_tosync_ended) {
            if (!req->reqc->params.release.rc)
                queue_release_response(dev->ld_response_queue, req->reqc);
        } else {
            req->reqc = NULL;   /* only the last device free reqc */
        }

        sub_request_free(req);
    }

    /* sync operation acknowledgement */
    dev->ld_sync_params.tosync_size = 0;
    dev->ld_sync_params.oldest_tosync.tv_sec = 0;
    dev->ld_sync_params.oldest_tosync.tv_nsec = 0;
    dev->ld_needs_sync = false;
    MUTEX_UNLOCK(&dev->ld_mutex);
}

/**
 * Return true if a is older or equal to b
 */
static bool is_older_or_equal(struct timespec a, struct timespec b)
{
    if (a.tv_sec > b.tv_sec ||
        (a.tv_sec == b.tv_sec && a.tv_nsec > b.tv_nsec))
        return false;

    return true;
}

static inline void update_oldest_tosync(struct timespec *oldest_to_update,
                                         struct timespec candidate)
{
    if ((oldest_to_update->tv_sec == 0 && oldest_to_update->tv_nsec == 0) ||
        is_older_or_equal(candidate, *oldest_to_update))
        *oldest_to_update = candidate;
}

static int tosync_rc(struct req_container *reqc, int index)
{
    return reqc->params.release.tosync_media[index].client_rc;
}

void push_new_sync_to_device(struct lrs_dev *dev, struct req_container *reqc,
                             size_t medium_index)
{
    struct sync_params *sync_params = &dev->ld_sync_params;
    struct sub_request *req_tosync;

    ENTRY;

    req_tosync = xmalloc(sizeof(*req_tosync));
    req_tosync->reqc = reqc;
    req_tosync->medium_index = medium_index;

    MUTEX_LOCK(&dev->ld_mutex);
    if (tosync_rc(reqc, medium_index) != 0)
        dev->ld_last_client_rc = tosync_rc(reqc, medium_index);

    g_ptr_array_add(sync_params->tosync_array, req_tosync);
    sync_params->tosync_size +=
        reqc->params.release.tosync_media[medium_index].written_size;
    update_oldest_tosync(&sync_params->oldest_tosync, reqc->received_at);

    MUTEX_UNLOCK(&dev->ld_mutex);

    thread_signal(&dev->ld_device_thread);
}

/**
 * update the dev->ld_sync_params.oldest_tosync by scrolling the tosync_array
 *
 * This function must be called with a lock on \p dev .
 */
static void update_queue_oldest_tosync(struct lrs_dev *dev)
{
    GPtrArray *tosync_array = dev->ld_sync_params.tosync_array;
    guint i;

    if (tosync_array->len == 0) {
        dev->ld_sync_params.oldest_tosync.tv_sec = 0;
        dev->ld_sync_params.oldest_tosync.tv_nsec = 0;
        return;
    }

    for (i = 0; i < tosync_array->len; i++) {
        struct sub_request *req_tosync = tosync_array->pdata[i];

        update_oldest_tosync(&dev->ld_sync_params.oldest_tosync,
                             req_tosync->reqc->received_at);
    }
}

/** remove from tosync_array when error occurs on other device */
static void remove_canceled_sync(struct lrs_dev *dev)
{
    GPtrArray *tosync_array = dev->ld_sync_params.tosync_array;
    bool need_oldest_update = false;
    int i;

    MUTEX_LOCK(&dev->ld_mutex);
    for (i = tosync_array->len - 1; i >= 0; i--) {
        struct sub_request *req_tosync = tosync_array->pdata[i];
        bool is_tosync_ended = false;

        MUTEX_LOCK(&req_tosync->reqc->mutex);

        if (req_tosync->reqc->params.release.rc) {
            struct release_params *release_params;
            struct tosync_medium *tosync_medium;

            g_ptr_array_remove_index_fast(tosync_array, i);
            release_params = &req_tosync->reqc->params.release;
            tosync_medium =
                &release_params->tosync_media[req_tosync->medium_index];

            dev->ld_sync_params.tosync_size -= tosync_medium->written_size;
            need_oldest_update = true;

            tosync_medium->status = SUB_REQUEST_CANCEL;
            is_tosync_ended = is_request_tosync_ended(req_tosync->reqc);
        }

        MUTEX_UNLOCK(&req_tosync->reqc->mutex);

        if (is_tosync_ended)
            sub_request_free(req_tosync);
    }

    if (need_oldest_update)
        update_queue_oldest_tosync(dev);

    MUTEX_UNLOCK(&dev->ld_mutex);
}

static bool is_past(struct timespec t)
{
    struct timespec now;
    int rc;

    rc = clock_gettime(CLOCK_REALTIME, &now);
    if (rc) {
        pho_error(-errno, "Unable to get CLOCK_REALTIME to check delay");
        return true;
    }

    return is_older_or_equal(t, now);
}

static void check_needs_sync(struct lrs_dev_hdl *handle, struct lrs_dev *dev)
{
    struct sync_params *sync_params = &dev->ld_sync_params;

    MUTEX_LOCK(&dev->ld_mutex);
    dev->ld_needs_sync = sync_params->tosync_array->len > 0 &&
                      (sync_params->tosync_array->len >= handle->sync_nb_req ||
                       is_past(add_timespec(&sync_params->oldest_tosync,
                                            &handle->sync_time_ms)) ||
                       sync_params->tosync_size >= handle->sync_wsize_kb);
    dev->ld_needs_sync |= (!running && sync_params->tosync_array->len > 0);
    dev->ld_needs_sync |= (thread_is_stopping(&dev->ld_device_thread) &&
                           sync_params->tosync_array->len > 0);
    /* Trigger a sync on error. We won't do a sync but the status of the device
     * and medium will be updated accordingly by dev_sync.
     */
    dev->ld_needs_sync |= (dev->ld_last_client_rc != 0);
    MUTEX_UNLOCK(&dev->ld_mutex);
}

int medium_sync(struct lrs_dev *dev)
{
    struct media_info *media_info = dev->ld_dss_media_info;
    const char *fsroot = dev->ld_mnt_path;
    struct io_adapter_module *ioa;
    struct dss_handle *dss;
    struct pho_log log;
    int rc;

    ENTRY;

    rc = get_io_adapter(media_info->fs.type, &ioa);
    if (rc)
        LOG_RETURN(rc, "No suitable I/O adapter for filesystem type: '%s'",
                   fs_type2str(media_info->fs.type));

    dss = &dev->ld_device_thread.dss;
    init_pho_log(&log, &dev->ld_dss_dev_info->rsc.id,
                 &dev->ld_dss_media_info->rsc.id, PHO_LTFS_SYNC);

    rc = ioa_medium_sync(ioa, fsroot, &log.message);
    emit_log_after_action(dss, &log, PHO_LTFS_SYNC, rc);

    pho_debug("sync: medium=%s rc=%d", media_info->rsc.id.name, rc);
    if (rc)
        LOG_RETURN(rc, "Cannot flush media at: %s", fsroot);

    return rc;
}

/** Update media_info stats and push its new state to the DSS */
static int lrs_dev_media_update(struct lrs_dev *dev, size_t size_written,
                                int media_rc, long long nb_new_obj)
{
    struct media_info *media_info = dev->ld_dss_media_info;
    struct dss_handle *dss = &dev->ld_device_thread.dss;
    const char *fsroot = dev->ld_mnt_path;
    struct ldm_fs_space space = {0};
    struct fs_adapter_module *fsa;
    uint64_t fields = 0;
    int rc2, rc = 0;

    if (media_info->fs.status == PHO_FS_STATUS_EMPTY && !media_rc) {
        media_info->fs.status = PHO_FS_STATUS_USED;
        fields |= FS_STATUS;
    }

    rc2 = get_fs_adapter(media_info->fs.type, &fsa);
    if (rc2) {
        rc = rc ? : rc2;
        pho_error(rc2,
                  "Invalid filesystem type for '%s' (database may be "
                  "corrupted)", fsroot);
        pho_error(rc2, "setting medium '%s' to failed",
                  media_info->rsc.id.name);
        media_info->rsc.adm_status = PHO_RSC_ADM_ST_FAILED;
        fields |= ADM_STATUS;
    } else {
        struct pho_log log;

        init_pho_log(&log, &dev->ld_dss_dev_info->rsc.id,
                     &dev->ld_dss_media_info->rsc.id, PHO_LTFS_DF);

        rc2 = ldm_fs_df(fsa, fsroot, &space, &log.message);
        emit_log_after_action(dss, &log, PHO_LTFS_DF, rc2);
        if (rc2) {
            rc = rc ? : rc2;
            pho_error(rc2, "Cannot retrieve media usage information");
            pho_error(rc2, "setting medium '%s' to failed",
                      media_info->rsc.id.name);
            media_info->rsc.adm_status = PHO_RSC_ADM_ST_FAILED;
            fields |= ADM_STATUS;
        } else {
            media_info->stats.phys_spc_used = space.spc_used;
            media_info->stats.phys_spc_free = space.spc_avail;
            fields |= PHYS_SPC_USED | PHYS_SPC_FREE;
            if (media_info->stats.phys_spc_free == 0) {
                media_info->fs.status = PHO_FS_STATUS_FULL;
                fields |= FS_STATUS;
            }
        }
    }

    if (media_rc) {
        media_info->rsc.adm_status = PHO_RSC_ADM_ST_FAILED;
        pho_error(media_rc, "setting medium '%s' to failed",
                  media_info->rsc.id.name);
        fields |= ADM_STATUS;
    } else {
        if (nb_new_obj) {
            media_info->stats.nb_obj = nb_new_obj;
            fields |= NB_OBJ_ADD;
        }

        if (size_written) {
            media_info->stats.logc_spc_used = size_written;
            fields |= LOGC_SPC_USED_ADD;
        }
    }

    /* TODO update nb_load, nb_errors, last_load */

    assert(fields);
    rc2 = dss_media_set(dss, media_info, 1, DSS_SET_UPDATE, fields);
    if (rc2)
        rc = rc ? : rc2;

    return rc;
}

/* Sync dev, update the media in the DSS, and flush tosync_array */
static int dev_sync(struct lrs_dev *dev)
{
    struct sync_params *sync_params = &dev->ld_sync_params;
    int rc = 0;
    int rc2;

    MUTEX_LOCK(&dev->ld_mutex);

    /* Do not sync on error as we don't know what happened on the tape. */
    if (dev->ld_last_client_rc == 0)
        rc = medium_sync(dev);
    else
        /* this will cause the device thread to stop */
        rc = dev->ld_last_client_rc;

    rc2 = lrs_dev_media_update(dev, sync_params->tosync_size, rc,
                               sync_params->tosync_array->len);
    dev->ld_last_client_rc = 0;

    MUTEX_UNLOCK(&dev->ld_mutex);
    if (rc2) {
        rc = rc ? : rc2;
        pho_error(rc2, "Cannot update media information");
    }

    clean_tosync_array(dev, rc);

    return rc;
}

int dev_umount(struct lrs_dev *dev)
{
    struct fs_adapter_module *fsa;
    struct dss_handle *dss;
    struct pho_log log;
    int rc;

    ENTRY;

    dss = &dev->ld_device_thread.dss;
    init_pho_log(&log, &dev->ld_dss_dev_info->rsc.id,
                 &dev->ld_dss_media_info->rsc.id, PHO_LTFS_UMOUNT);

    pho_info("umount: medium '%s' in device '%s' mounted at '%s'",
             dev->ld_dss_media_info->rsc.id.name,
             dev->ld_dev_path,
             dev->ld_mnt_path);
    rc = get_fs_adapter(dev->ld_dss_media_info->fs.type, &fsa);
    if (rc)
        LOG_GOTO(out_err, rc,
                 "Unable to get fs adapter '%s' to unmount medium '%s' from "
                 "device '%s'", fs_type_names[dev->ld_dss_media_info->fs.type],
                 dev->ld_dss_media_info->rsc.id.name, dev->ld_dev_path);

    rc = ldm_fs_umount(fsa, dev->ld_dev_path, dev->ld_mnt_path, &log.message);
    emit_log_after_action(dss, &log, PHO_LTFS_UMOUNT, rc);
    clean_tosync_array(dev, rc);
    if (rc)
        LOG_GOTO(out_err, rc, "Failed to unmount device '%s' mounted at '%s'",
                 dev->ld_dev_path, dev->ld_mnt_path);

    /* update device state and unset mount path */
    MUTEX_LOCK(&dev->ld_mutex);
    dev->ld_op_status = PHO_DEV_OP_ST_LOADED;
    dev->ld_mnt_path[0] = '\0';
    MUTEX_UNLOCK(&dev->ld_mutex);

    return 0;

out_err:
    MUTEX_LOCK(&dev->ld_mutex);
    dev->ld_op_status = PHO_DEV_OP_ST_FAILED;
    MUTEX_UNLOCK(&dev->ld_mutex);

    return rc;
}

static int dss_medium_release(struct dss_handle *dss, struct media_info *medium)
{
    int rc;

    pho_debug("unlock: medium '%s'", medium->rsc.id.name);
    rc = dss_unlock(dss, DSS_MEDIA, medium, 1, false);
    if (rc)
        LOG_RETURN(rc,
                   "Error when releasing medium '%s' with current lock "
                   "(hostname %s, owner %d)", medium->rsc.id.name,
                   medium->lock.hostname, medium->lock.owner);

    pho_lock_clean(&medium->lock);
    return 0;
}

static int dss_device_release(struct dss_handle *dss, struct dev_info *dev)
{
    int rc;

    pho_verb("unlock: device '%s'", dev->rsc.id.name);
    rc = dss_unlock(dss, DSS_DEVICE, dev, 1, false);
    if (rc)
        LOG_RETURN(rc,
                   "Error when releasing device '%s' with current lock "
                   "(hostname %s, owner %d)", dev->rsc.id.name,
                   dev->lock.hostname, dev->lock.owner);

    pho_lock_clean(&dev->lock);
    return 0;
}

int dev_unload(struct lrs_dev *dev)
{
    /* let the library select the target location */
    struct media_info *medium_to_unlock_free = NULL;
    struct lib_handle lib_hdl;
    int rc2;
    int rc;

    ENTRY;

    pho_verb("unload: '%s' from '%s'", dev->ld_dss_media_info->rsc.id.name,
             dev->ld_dev_path);

    rc = wrap_lib_open(dev->ld_dss_dev_info->rsc.id.family, &lib_hdl);
    if (rc)
        LOG_GOTO(out, rc,
                 "Unable to open lib '%s' to unload medium '%s' from device "
                 "'%s'", rsc_family_names[dev->ld_dss_dev_info->rsc.id.family],
                 dev->ld_dss_media_info->rsc.id.name, dev->ld_dev_path);

    rc = ldm_lib_unload(&lib_hdl, dev->ld_dss_dev_info->rsc.id.name,
                        dev->ld_dss_media_info->rsc.id.name);
    if (rc != 0)
        /* Set operational failure state on this drive. It is incomplete since
         * the error can originate from a defective tape too...
         *  - consider marking both as failed.
         *  - consider maintaining lists of errors to diagnose and decide who to
         *    exclude from the cool game.
         */
        LOG_GOTO(out_close, rc, "Media move failed");

    MUTEX_LOCK(&dev->ld_mutex);
    dev->ld_op_status = PHO_DEV_OP_ST_EMPTY;
    medium_to_unlock_free = dev->ld_dss_media_info;
    dev->ld_dss_media_info = NULL;
    MUTEX_UNLOCK(&dev->ld_mutex);

out_close:
    rc2 = ldm_lib_close(&lib_hdl);
    if (rc2)
        rc = rc ? : rc2;

out:
    if (!rc) {
        rc = dss_medium_release(&dev->ld_device_thread.dss,
                                medium_to_unlock_free);
        media_info_free(medium_to_unlock_free);
    } else {
        MUTEX_LOCK(&dev->ld_mutex);
        dev->ld_op_status = PHO_DEV_OP_ST_FAILED;
        MUTEX_UNLOCK(&dev->ld_mutex);
    }

    return rc;
}

/**
 * If a medium is into dev, umount, unload and release its locks.
 */
static int dev_empty(struct lrs_dev *dev)
{
    int rc;

    ENTRY;

    if (dev->ld_op_status == PHO_DEV_OP_ST_EMPTY)
        return 0;

    /* Umount if needed */
    if (dev->ld_op_status == PHO_DEV_OP_ST_MOUNTED) {
        rc = dev_umount(dev);
        if (rc)
            return rc;
    }

    /* Follow up on unload if needed */
    if (dev->ld_op_status == PHO_DEV_OP_ST_LOADED)
        return dev_unload(dev);

    LOG_RETURN(-EINVAL,
               "We cannot empty device '%s' which is in '%s' op "
               "status.", dev->ld_dev_path, op_status2str(dev->ld_op_status));
}

static int dss_set_medium_to_failed(struct dss_handle *dss,
                                    struct media_info *media_info)
{
    pho_error(0, "setting medium '%s' to failed", media_info->rsc.id.name);
    media_info->rsc.adm_status = PHO_RSC_ADM_ST_FAILED;
    return dss_media_set(dss, media_info, 1, DSS_SET_UPDATE, ADM_STATUS);
}

static void fail_release_free_medium(struct lrs_dev *dev,
                                     struct media_info **medium,
                                     bool free_medium)
{
    int rc;

    rc = dss_set_medium_to_failed(&dev->ld_device_thread.dss, *medium);
    if (rc) {
        pho_error(rc,
                  "Warning we keep medium %s locked because we can't set it to "
                  "failed into DSS", (*medium)->rsc.id.name);
    } else {
        rc = dss_medium_release(&dev->ld_device_thread.dss, *medium);
        if (rc)
            pho_error(rc,
                      "Error when releasing medium %s after setting it to "
                      "status failed", (*medium)->rsc.id.name);
    }

    if (free_medium) {
        MUTEX_LOCK(&dev->ld_mutex);
        media_info_free(*medium);
        *medium = NULL;
        MUTEX_UNLOCK(&dev->ld_mutex);
    }
}

static void fail_release_device(struct lrs_dev *dev)
{
    int rc;

    dev->ld_dss_dev_info->rsc.adm_status = PHO_RSC_ADM_ST_FAILED;
    pho_error(0, "setting device '%s' to failed",
              lrs_dev_name(dev));

    rc = dss_device_update_adm_status(&dev->ld_device_thread.dss,
                                      dev->ld_dss_dev_info, 1);
    if (rc) {
        pho_error(rc,
                  "unable to set device '%s' status to '%s' in DSS, "
                  "we won't release the corresponding DSS lock",
                  lrs_dev_name(dev),
                  rsc_adm_status2str(PHO_RSC_ADM_ST_FAILED));
        return;
    }

    rc = dss_device_release(&dev->ld_device_thread.dss,
                            dev->ld_dss_dev_info);
    if (rc)
        pho_error(rc, "unable to release DSS lock of device '%s'",
                  lrs_dev_name(dev));
}

int dev_load(struct lrs_dev *dev, struct media_info **medium,
             bool release_medium_on_dev_only_failure, bool free_medium)
{
    struct lib_handle lib_hdl;
    int rc2;
    int rc;

    ENTRY;

    dev->ld_sub_request->failure_on_medium = false;
    pho_verb("load: '%s' into '%s'", (*medium)->rsc.id.name, dev->ld_dev_path);

    /* get handle to the library depending on device type */
    rc = wrap_lib_open(dev->ld_dss_dev_info->rsc.id.family, &lib_hdl);
    if (rc) {
        MUTEX_LOCK(&dev->ld_mutex);
        dev->ld_op_status = PHO_DEV_OP_ST_FAILED;
        MUTEX_UNLOCK(&dev->ld_mutex);

        if (release_medium_on_dev_only_failure) {
            rc2 = dss_medium_release(&dev->ld_device_thread.dss, *medium);
            if (rc2)
                pho_error(rc2,
                          "Error when releasing a medium during device load "
                          "error");
        }

        return rc;
    }

    rc = ldm_lib_load(&lib_hdl, dev->ld_dss_dev_info->rsc.id.name,
                      (*medium)->rsc.id.name);
    if (rc) {
        /* Set operationnal failure state on this drive. It is incomplete since
         * the error can originate from a defect tape too...
         *  - consider marking both as failed.
         *  - consider maintaining lists of errors to diagnose and decide who to
         *    exclude from the cool game.
         */
        MUTEX_LOCK(&dev->ld_mutex);
        dev->ld_op_status = PHO_DEV_OP_ST_FAILED;
        MUTEX_UNLOCK(&dev->ld_mutex);
        dev->ld_sub_request->failure_on_medium = true;
        fail_release_free_medium(dev, medium, free_medium);
        LOG_GOTO(out_close, rc, "Media move failed");
    }

    /* update device status */
    MUTEX_LOCK(&dev->ld_mutex);
    dev->ld_op_status = PHO_DEV_OP_ST_LOADED;
    dev->ld_dss_media_info = *medium;
    if (free_medium)
        *medium = NULL;
    MUTEX_UNLOCK(&dev->ld_mutex);
    rc = 0;

out_close:
    rc2 = ldm_lib_close(&lib_hdl);
    if (rc2) {
        MUTEX_LOCK(&dev->ld_mutex);
        dev->ld_op_status = PHO_DEV_OP_ST_FAILED;
        MUTEX_UNLOCK(&dev->ld_mutex);
        pho_error(rc2, "Unable to close lib");
        rc = rc ? : rc2;
    }

    return rc;
}

int dev_format(struct lrs_dev *dev, struct fs_adapter_module *fsa, bool unlock)
{
    struct media_info *medium = dev->ld_dss_media_info;
    struct ldm_fs_space space = {0};
    struct dss_handle *dss;
    uint64_t fields = 0;
    struct pho_log log;
    int rc;

    ENTRY;

    dss = &dev->ld_device_thread.dss;
    init_pho_log(&log, &dev->ld_dss_dev_info->rsc.id,
                 &dev->ld_dss_media_info->rsc.id, PHO_LTFS_FORMAT);

    pho_verb("format: medium '%s'", medium->rsc.id.name);

    rc = ldm_fs_format(fsa, dev->ld_dev_path, medium->rsc.id.name, &space,
                       &log.message);
    emit_log_after_action(dss, &log, PHO_LTFS_FORMAT, rc);
    if (rc)
        LOG_RETURN(rc, "Cannot format medium '%s'", medium->rsc.id.name);

    MUTEX_LOCK(&dev->ld_mutex);
    /* Systematically use the media ID as filesystem label */
    strncpy(medium->fs.label, medium->rsc.id.name, sizeof(medium->fs.label));
    medium->fs.label[sizeof(medium->fs.label) - 1] = '\0';
    fields |= FS_LABEL;

    medium->stats.nb_obj = 0;
    medium->stats.logc_spc_used = 0;
    medium->stats.phys_spc_used = space.spc_used;
    medium->stats.phys_spc_free = space.spc_avail;
    fields |= NB_OBJ | LOGC_SPC_USED | PHYS_SPC_USED | PHYS_SPC_FREE;

    /* Post operation: update media information in DSS */
    medium->fs.status = PHO_FS_STATUS_EMPTY;
    fields |= FS_STATUS;

    if (unlock) {
        pho_verb("Removing admin lock on media '%s' after format as requested "
                 "by client", medium->rsc.id.name);
        medium->rsc.adm_status = PHO_RSC_ADM_ST_UNLOCKED;
        fields |= ADM_STATUS;
    }

    MUTEX_UNLOCK(&dev->ld_mutex);
    rc = dss_media_set(&dev->ld_device_thread.dss, medium, 1, DSS_SET_UPDATE,
                       fields);
    if (rc)
        LOG_RETURN(rc, "Failed to update state of media '%s' after format",
                   medium->rsc.id.name);

    return 0;
}

static void queue_format_response(struct tsqueue *response_queue,
                                  struct req_container *reqc)
{
    struct resp_container *respc = NULL;

    respc = xmalloc(sizeof(*respc));
    respc->socket_id = reqc->socket_id;
    respc->resp = xmalloc(sizeof(*respc->resp));

    pho_srl_response_format_alloc(respc->resp);

    /* Build the answer */
    respc->resp->req_id = reqc->req->id;
    respc->resp->format->med_id->family = reqc->req->format->med_id->family;
    respc->resp->format->med_id->name =
        xstrdup_safe(reqc->req->format->med_id->name);

    tsqueue_push(response_queue, respc);
}

static bool medium_is_loaded(struct lrs_dev *dev, struct media_info *medium)
{
    return (dev->ld_op_status == PHO_DEV_OP_ST_LOADED) &&
        !strcmp(dev->ld_dss_media_info->rsc.id.name,
                medium->rsc.id.name);
}

static int dev_handle_format(struct lrs_dev *dev)
{
    struct sub_request *subreq = dev->ld_sub_request;
    struct medium_switch_context context = {0};
    struct media_info *medium_to_format;
    struct req_container *reqc;
    bool loaded = false;
    int rc;

    reqc = dev->ld_sub_request->reqc;
    medium_to_format = reqc->params.format.medium_to_format;
    context.free_medium = false;
    context.release_medium = true;

    if (medium_is_loaded(dev, medium_to_format)) {
        /* medium to format already loaded, use existing dev->ld_dss_media_info,
         * free reqc->params.format.medium_to_format
         */
        pho_info("medium %s to format is already loaded into device %s",
                 dev->ld_dss_media_info->rsc.id.name,
                 dev->ld_dss_dev_info->rsc.id.name);
        goto format;
    }

    rc = dev_medium_switch(dev, medium_to_format, &context);
    if (rc == -EBUSY) {
        pho_warn("Trying to load a busy medium to format, try again later");
        return 0;
    }
    if (rc && !subreq->failure_on_medium) {
        /* Push format request back if the error is not caused by the medium */
        /* TODO: use sched retry queue */
        tsqueue_push(dev->sched_req_queue, reqc);
        goto out;
    }

    MUTEX_LOCK(&dev->ld_mutex);
    reqc->params.format.medium_to_format = NULL;
    MUTEX_UNLOCK(&dev->ld_mutex);
    if (rc)
        goto out_response;

    loaded = true;

format:
    rc = dev_format(dev, reqc->params.format.fsa, reqc->req->format->unlock);

out_response:
    if (rc)
        queue_error_response(dev->ld_response_queue, rc, reqc);
    else
        queue_format_response(dev->ld_response_queue, reqc);

out:
    MUTEX_LOCK(&dev->ld_mutex);
    dev->ld_sub_request = NULL;
    format_medium_remove(dev->ld_ongoing_format, medium_to_format);
    /* free medium_to_format is not reused */
    if (rc && !loaded) {
        /* free medium_to_format is not reused */
        media_info_free(medium_to_format);
        reqc->params.format.medium_to_format = NULL;
    }

    sub_request_free(subreq);
    MUTEX_UNLOCK(&dev->ld_mutex);
    return rc;
}

bool locked_cancel_rwalloc_on_error(struct sub_request *sub_request,
                                    bool *ended)
{
    struct req_container *reqc = sub_request->reqc;
    struct rwalloc_medium  *rwalloc_medium;

    *ended = false;
    if (!reqc->params.rwalloc.rc)
        return false;

    rwalloc_medium = &reqc->params.rwalloc.media[sub_request->medium_index];
    rwalloc_medium->status = SUB_REQUEST_CANCEL;
    media_info_free(rwalloc_medium->alloc_medium);
    rwalloc_medium->alloc_medium = NULL;
    *ended = is_rwalloc_ended(reqc);
    return true;
}

/**
 * Cancel this sub request if there is already an error
 *
 * If a previous error is detected with a not null rc in reqc, the medium of
 * this sub_request is freed and set to NULL and its status is set as
 * SUB_REQUEST_CANCEL.
 * If the request is ended, reqc is freed
 * If the request is canceled sub_request->reqc is set to NULL.
 *
 * @param[in]   sub_request     the rwalloc sub_request to check
 *
 * @return  True if there was an error and the medium is cancelled, false
 *          otherwise.
 */
static bool cancel_subrequest_on_error(struct sub_request *sub_request)
{
    pho_req_t *req = sub_request->reqc->req;
    bool canceled = false;
    bool ended = false;

    if (pho_request_is_format(req))
        return false;

    MUTEX_LOCK(&sub_request->reqc->mutex);
    canceled = locked_cancel_rwalloc_on_error(sub_request, &ended);
    MUTEX_UNLOCK(&sub_request->reqc->mutex);

    if (canceled) {
        if (ended)
            sched_req_free(sub_request->reqc);

        sub_request->reqc = NULL;
    }

    return canceled;
}

/**
 * Fill response container for sub requests
 *
 * In addition of filling the medium which is allocated, this function
 * also fill the dev which is elected. The dev piece of information is not
 * dedicated to the final response sent to the client, but it is a piece of
 * information internal to the LRS in case we need to cancel a request on a
 * device based on the response container.
 *
 * @param[in]   dev         device in charge of this sub request
 * @param[in]   sub_request rwalloc sub request
 */
static void fill_rwalloc_resp_container(struct lrs_dev *dev,
                                        struct sub_request *sub_request)
{
    struct resp_container *respc = sub_request->reqc->params.rwalloc.respc;
    pho_resp_t *resp = respc->resp;

    if (pho_request_is_read(sub_request->reqc->req)) {
        pho_resp_read_elt_t *rresp;

        rresp = resp->ralloc->media[sub_request->medium_index];
        rresp->fs_type = dev->ld_dss_media_info->fs.type;
        rresp->addr_type = dev->ld_dss_media_info->addr_type;
        rresp->root_path = xstrdup(dev->ld_mnt_path);
        rresp->med_id->name = xstrdup(dev->ld_dss_media_info->rsc.id.name);
        rresp->med_id->family = dev->ld_dss_media_info->rsc.id.family;
    } else {
        pho_resp_write_elt_t *wresp;

        wresp = resp->walloc->media[sub_request->medium_index];
        wresp->avail_size = dev->ld_dss_media_info->stats.phys_spc_free;
        wresp->med_id->family = dev->ld_dss_media_info->rsc.id.family;
        wresp->root_path = xstrdup(dev->ld_mnt_path);
        wresp->med_id->name = xstrdup(dev->ld_dss_media_info->rsc.id.name);
        wresp->fs_type = dev->ld_dss_media_info->fs.type;
        wresp->addr_type = dev->ld_dss_media_info->addr_type;
    }
}

static bool rwalloc_can_be_requeued(struct sub_request *sub_request)
{
    struct req_container *reqc = sub_request->reqc;
    struct read_media_list *list;
    pho_req_read_t *ralloc;

    if (pho_request_is_write(reqc->req))
        return true;

    if (!sub_request->failure_on_medium)
        return true;

    ralloc = reqc->req->ralloc;
    list = &reqc->params.rwalloc.media_list;

    /* if possible: prepare a new candidate medium and requeue. */
    return rml_nb_usable_media(list) > ralloc->n_required;
}

/**
 * Set sub_request result in request
 *
 * If request is ended, a response is queued and the request is freed and set
 * to NULL.
 *
 * If rc is not NULL, request is requeued as an error in sched unless we face an
 * error on a medium for a read alloc request with no more available medium.
 *
 * Unless the request is requeued on error with no error on the medium, the
 * medium of this sub_request is freed and set to NULL in the request.
 *
 * @param[in]   dev                     device
 * @param[in]   sub_request             rwalloc sub_request in which we set the
 *                                      result
 * @param[in]   sub_request_rc          return code (0 means "done" with no
 *                                      error, different from 0 means we faced
 *                                      an error)
 * @param[out]  sub_request_requeued    set to true if sub_request is requeued
 *                                      in the scheduler
 * @param[out]  canceled                Set to true if the sub_request is
 *                                      canceled when filling results
 *
 * @return  a negative error code on failure
 */
static void handle_rwalloc_sub_request_result(struct lrs_dev *dev,
                                              struct sub_request *sub_request,
                                              int sub_request_rc,
                                              bool *sub_request_requeued,
                                              bool *canceled)
{
    struct req_container *reqc = sub_request->reqc;
    struct rwalloc_medium  *rwalloc_medium;
    bool free_medium = true;
    bool ended = false;

    *sub_request_requeued = false;
    *canceled = false;
    MUTEX_LOCK(&reqc->mutex);
    rwalloc_medium = &reqc->params.rwalloc.media[sub_request->medium_index];
    *canceled = locked_cancel_rwalloc_on_error(sub_request, &ended);
    if (*canceled)
        goto out_free;

    if (!sub_request_rc) {
        rwalloc_medium->status = SUB_REQUEST_DONE;
        fill_rwalloc_resp_container(dev, sub_request);

        goto try_send_response;
    }

    /* sub_request_rc is not null */
    if (rwalloc_can_be_requeued(sub_request)) {
        /* requeue failed request in the scheduler */
        *sub_request_requeued = true;
        if (!sub_request->failure_on_medium)
            free_medium = false;

        pho_debug("Requeuing %p (%s) on error", sub_request->reqc,
                  pho_srl_request_kind_str(sub_request->reqc->req));
        tsqueue_push(dev->sched_retry_queue, sub_request);
        goto out_free;
    } else {
        /* First fatal error on rwalloc */
        reqc->params.rwalloc.rc = sub_request_rc;
        rwalloc_medium->status = SUB_REQUEST_ERROR;
        queue_error_response(dev->ld_response_queue, sub_request_rc,
                             sub_request->reqc);
        rwalloc_cancel_DONE_devices(reqc);
    }

try_send_response:
    ended = is_rwalloc_ended(reqc);
    if (!sub_request_rc && ended) {
        tsqueue_push(dev->ld_response_queue, reqc->params.rwalloc.respc);
        reqc->params.rwalloc.respc = NULL;
    }

out_free:
    if (free_medium) {
        media_info_free(rwalloc_medium->alloc_medium);
        rwalloc_medium->alloc_medium = NULL;
    }

    MUTEX_UNLOCK(&reqc->mutex);
    if (ended) {
        sched_req_free(reqc);
        sub_request->reqc = NULL;
    }
}

char *mount_point(const char *id)
{
    const char *mnt_cfg;
    char *mnt_out;

    mnt_cfg = PHO_CFG_GET(cfg_lrs, PHO_CFG_LRS, mount_prefix);
    if (mnt_cfg == NULL)
        return NULL;

    /* mount the device as PHO_MNT_PREFIX<id> */
    if (asprintf(&mnt_out, "%s%s", mnt_cfg, id) < 0)
        return NULL;

    return mnt_out;
}

int dev_mount(struct lrs_dev *dev)
{
    struct dss_handle *dss = &dev->ld_device_thread.dss;
    struct fs_adapter_module *fsa;
    struct pho_log log;
    char *mnt_root;
    const char *id;
    int rc;

    init_pho_log(&log, &dev->ld_dss_dev_info->rsc.id,
                 &dev->ld_dss_media_info->rsc.id, PHO_LTFS_MOUNT);

    rc = get_fs_adapter(dev->ld_dss_media_info->fs.type, &fsa);
    if (rc)
        LOG_RETURN(rc, "Unable to get fs adapter to mount a medium");

    rc = ldm_fs_mounted(fsa, dev->ld_dev_path, dev->ld_mnt_path,
                        sizeof(dev->ld_mnt_path));
    if (rc == 0) {
        MUTEX_LOCK(&dev->ld_mutex);
        dev->ld_op_status = PHO_DEV_OP_ST_MOUNTED;
        MUTEX_UNLOCK(&dev->ld_mutex);
        return 0;
    }

    /**
     * @TODO If library indicates a medium is in the drive but the drive
     * doesn't, we need to query the drive to load the tape.
     */

    id = basename(dev->ld_dev_path);
    if (id == NULL)
        LOG_RETURN(-EINVAL, "Unable to get dev path basename");

    /* mount the device as PHO_MNT_PREFIX<id> */
    mnt_root = mount_point(id);
    if (!mnt_root)
        LOG_RETURN(-ENOMEM, "Unable to get mount point of %s", id);

    pho_info("mount: medium '%s' in device '%s' ('%s') as '%s'",
             dev->ld_dss_media_info->rsc.id.name,
             dev->ld_dev_path,
             dev->ld_dss_dev_info->rsc.id.name,
             mnt_root);

    rc = ldm_fs_mount(fsa, dev->ld_dev_path, mnt_root,
                      dev->ld_dss_media_info->fs.label,
                      &log.message);
    emit_log_after_action(dss, &log, PHO_LTFS_MOUNT, rc);
    if (rc)
        LOG_GOTO(out_free, rc, "Failed to mount '%s' in device '%s'",
                 dev->ld_dss_media_info->rsc.id.name,
                 dev->ld_dev_path);

    /* update device state and set mount point */
    MUTEX_LOCK(&dev->ld_mutex);
    dev->ld_op_status = PHO_DEV_OP_ST_MOUNTED;
    strncpy(dev->ld_mnt_path, mnt_root, sizeof(dev->ld_mnt_path));
    dev->ld_mnt_path[sizeof(dev->ld_mnt_path) - 1] = '\0';
    MUTEX_UNLOCK(&dev->ld_mutex);

out_free:
    free(mnt_root);

    return rc;
}

bool dev_mount_is_writable(struct lrs_dev *dev)
{
    enum fs_type fs_type = dev->ld_dss_media_info->fs.type;
    const char *fs_root = dev->ld_mnt_path;
    struct ldm_fs_space fs_info = {0};
    struct fs_adapter_module *fsa;
    struct dss_handle *dss;
    struct pho_log log;
    int rc;

    dss = &dev->ld_device_thread.dss;
    init_pho_log(&log, &dev->ld_dss_dev_info->rsc.id,
                 &dev->ld_dss_media_info->rsc.id, PHO_LTFS_DF);

    rc = get_fs_adapter(fs_type, &fsa);
    if (rc) {
        pho_error(rc, "No FS adapter found for '%s' (type %d)",
                  fs_root, fs_type);
        return false;
    }

    rc = ldm_fs_df(fsa, fs_root, &fs_info, &log.message);
    emit_log_after_action(dss, &log, PHO_LTFS_DF, rc);
    if (rc) {
        pho_error(rc, "Cannot retrieve media usage information");
        return false;
    }

    return !(fs_info.spc_flags & PHO_FS_READONLY);
}

static int dev_handle_read_write(struct lrs_dev *dev)
{
    struct sub_request *sub_request = dev->ld_sub_request;
    struct req_container *reqc = sub_request->reqc;
    struct medium_switch_context context = {0};
    struct media_info *medium_to_alloc;
    bool sub_request_requeued = false;
    bool io_ended = false;
    bool cancel = false;
    bool locked = false;
    int rc = 0;

    ENTRY;

    if (cancel_subrequest_on_error(sub_request)) {
        io_ended = true;
        goto out_free;
    }

    /*
     * We call dev_load with release_medium_on_dev_only_failure at false
     * because the request will be pushed to the retry queue of the sched
     * with an already locked medium ready to be use in a new device.
     */
    context.release_medium = false;
    context.free_medium = true;
    medium_to_alloc =
        reqc->params.rwalloc.media[sub_request->medium_index].alloc_medium;

    rc = dev_medium_switch_mount(dev, medium_to_alloc, &context);
    if (rc == -EBUSY) {
        pho_warn("Trying to load a busy medium to %s, try again later",
                 pho_srl_request_kind_str(reqc->req));
        return 0;
    }
    if (!rc || sub_request->failure_on_medium)
        reqc->params.rwalloc.media[sub_request->medium_index].alloc_medium =
            NULL;
    if (rc) {
        io_ended = true;
        goto alloc_result;
    }

alloc_result:
    MUTEX_LOCK(&dev->ld_mutex);
    locked = true;
    handle_rwalloc_sub_request_result(dev, sub_request, rc,
                                      &sub_request_requeued,
                                      &cancel);
    if (cancel)
        io_ended = true;

out_free:
    if (!locked)
        MUTEX_LOCK(&dev->ld_mutex);

    if (!io_ended && !sub_request_requeued)
        dev->ld_ongoing_io = true;

    dev->ld_sub_request = NULL;
    if (!sub_request_requeued) {
        sub_request->reqc = NULL;
        sub_request_free(sub_request);
    }

    MUTEX_UNLOCK(&dev->ld_mutex);

    if (context.failure_on_device)
        return rc;

    return 0;
}

/**
 * Manage a format request at device thread end.
 *
 * If format_request:
 *     if error with corresponding medium loaded,
 *         send a response error and free the format request,
 *     else
 *         request medium DSS lock and free the format request medium info,
 *         requeue the request with releasing the format.
 */
static void cancel_pending_format(struct lrs_dev *device)
{
    struct req_container *format_request;
    int rc = 0;

    if (!device->ld_sub_request)
        return;

    format_request = device->ld_sub_request->reqc;

    if (device->ld_device_thread.status &&
        !format_request->params.format.medium_to_format) {
        /*
         * A NULL medium_to_format field in the format request means the medium
         * has been transfered to the device.
         */
        format_medium_remove(device->ld_ongoing_format,
                             device->ld_dss_media_info);
        queue_error_response(device->ld_response_queue,
                             device->ld_device_thread.status,
                             format_request);

        sub_request_free(device->ld_sub_request);
    } else {
        if (format_request->params.format.medium_to_format) {
            struct media_info *medium_to_format =
                format_request->params.format.medium_to_format;

            format_medium_remove(device->ld_ongoing_format, medium_to_format);
            rc = dss_medium_release(&device->ld_device_thread.dss,
                                    medium_to_format);
            if (rc) {
                pho_error(rc, "setting medium '%s' to failed",
                          medium_to_format->rsc.id.name);
                medium_to_format->rsc.adm_status = PHO_RSC_ADM_ST_FAILED;
                rc = dss_media_set(&device->ld_device_thread.dss,
                                   medium_to_format, 1, DSS_SET_UPDATE,
                                   ADM_STATUS);
                if (rc)
                    pho_error(rc,
                              "Unable to set medium '%s' into DSS as "
                              "PHO_RSC_ADM_ST_FAILED although we failed to "
                              "release the corresponding lock",
                              medium_to_format->rsc.id.name);
            }

            media_info_free(medium_to_format);
            format_request->params.format.medium_to_format = NULL;
        } else {
            format_medium_remove(device->ld_ongoing_format,
                                 device->ld_dss_media_info);
        }

        if (!rc) {
            /* TODO: use sched error queue */
            tsqueue_push(device->sched_req_queue, format_request);
            free(device->ld_sub_request);
        } else {
            queue_error_response(device->ld_response_queue, rc, format_request);

            sub_request_free(device->ld_sub_request);
        }
    }

    device->ld_sub_request = NULL;
}

/**
 * Cleanup medium when the device thread stops without error
 * - no medium: nothing to do
 * - mounted: umount and release medium
 * - loaded: simply release the medium
 *
 * We don't unload the tape when stopping the LRS.
 *
 * Finaly, we release the lock on the device.
 */
static int dev_cleanup_medium_at_stop(struct lrs_dev *device)
{
    int rc;

    if (dev_is_mounted(device)) {
        rc = dev_umount(device);
        if (rc)
            LOG_RETURN(rc,
                       "unable to unmount medium '%s' in device '%s' during "
                       "regular exit",
                       device->ld_dss_media_info->rsc.id.name,
                       lrs_dev_name(device));
    }

    if (dev_is_loaded(device)) {
        rc = dss_medium_release(&device->ld_device_thread.dss,
                                device->ld_dss_media_info);
        if (rc)
            LOG_RETURN(rc,
                       "unable to release DSS lock of medium '%s' in device "
                       "'%s' during regular exit",
                       device->ld_dss_media_info->rsc.id.name,
                       lrs_dev_name(device));

        MUTEX_LOCK(&device->ld_mutex);
        media_info_free(device->ld_dss_media_info);
        device->ld_dss_media_info = NULL;
        MUTEX_UNLOCK(&device->ld_mutex);
    }

    rc = dss_device_release(&device->ld_device_thread.dss,
                            device->ld_dss_dev_info);
    if (rc)
        LOG_RETURN(rc,
                   "Unable to release DSS lock of device '%s' during "
                   "regular exit",
                   lrs_dev_name(device));

    return 0;
}

/**
 * Cleanup the device thread on error. Since we are in an error, do not touch
 * the medium (i.e. do not unmount).
 *
 * We simply fail the medium and remove its lock if the device is loaded.
 * Then we fail the device.
 */
static void dev_cleanup_on_error(struct lrs_dev *device)
{
    if (device->ld_dss_media_info)
        fail_release_free_medium(device, &device->ld_dss_media_info, true);

    clean_tosync_array(device, device->ld_device_thread.status);

    MUTEX_LOCK(&device->ld_mutex);
    device->ld_op_status = PHO_DEV_OP_ST_FAILED;
    MUTEX_UNLOCK(&device->ld_mutex);

    if (device->ld_dss_dev_info)
        fail_release_device(device);
}

static void dev_thread_end(struct lrs_dev *device)
{
    /* prevent any new scheduled request to this device */
    if (thread_is_running(&device->ld_device_thread)) {
        MUTEX_LOCK(&device->ld_mutex);
        device->ld_device_thread.state = THREAD_STOPPING;
        MUTEX_UNLOCK(&device->ld_mutex);
    }

    cancel_pending_format(device);

    if (!device->ld_device_thread.status) {
        int rc = dev_cleanup_medium_at_stop(device);

        if (rc) {
            pho_error(rc, "failed to cleanup stopping device '%s'",
                      lrs_dev_name(device));
            MUTEX_LOCK(&device->ld_mutex);
            device->ld_device_thread.status = rc;
            MUTEX_UNLOCK(&device->ld_mutex);
        }
    }

    if (device->ld_device_thread.status)
        dev_cleanup_on_error(device);

    device->ld_ongoing_io = false;
}

/**
 * Main device thread loop.
 */
static void *lrs_dev_thread(void *tdata)
{
    struct lrs_dev *device = (struct lrs_dev *)tdata;
    struct thread_info *thread;

    thread = &device->ld_device_thread;

    while (!thread_is_stopped(thread)) {
        int rc = 0;

        if (device->ld_sub_request &&
            cancel_subrequest_on_error(device->ld_sub_request)) {
            MUTEX_LOCK(&device->ld_mutex);
            sub_request_free(device->ld_sub_request);
            device->ld_sub_request = NULL;
            MUTEX_UNLOCK(&device->ld_mutex);
        }

        remove_canceled_sync(device);
        if (!device->ld_needs_sync)
            check_needs_sync(device->ld_handle, device);

        if (thread_is_stopping(thread) && !device->ld_ongoing_io &&
            !device->ld_sub_request &&
            device->ld_sync_params.tosync_array->len == 0) {
            pho_debug("Switching to stopped");
            thread->state = THREAD_STOPPED;
        }

        if (!device->ld_ongoing_io) {
            if (device->ld_needs_sync) {
                rc = dev_sync(device);
                if (rc)
                    LOG_GOTO(end_thread, thread->status = rc,
                             "device thread '%s': fatal error syncing device",
                             device->ld_dss_dev_info->rsc.id.name);
            }

            if (device->ld_sub_request) {
                pho_req_t *req = device->ld_sub_request->reqc->req;

                if (pho_request_is_format(req))
                    rc = dev_handle_format(device);
                else if (pho_request_is_read(req) || pho_request_is_write(req))
                    rc = dev_handle_read_write(device);
                else
                    pho_error(rc = -EINVAL,
                              "device thread '%s': "
                              "invalid type (%s) in ld_sub_request",
                              device->ld_dss_dev_info->rsc.id.name,
                              pho_srl_request_kind_str(req));

                if (rc)
                    LOG_GOTO(end_thread, thread->status = rc,
                             "device thread '%s': fatal error handling "
                             "ld_sub_request",
                             device->ld_dss_dev_info->rsc.id.name);
            }
        }

        if (!thread_is_stopped(thread)) {
            rc = dev_wait_for_signal(device);
            if (rc < 0)
                LOG_GOTO(end_thread, thread->status = rc,
                         "device thread '%s': fatal error",
                         device->ld_dss_dev_info->rsc.id.name);
        }
    }

end_thread:
    dev_thread_end(device);
    pthread_exit(&device->ld_device_thread.status);
}

static int dev_thread_init(struct lrs_dev *device)
{
    int rc;

    pthread_mutex_init(&device->ld_mutex, NULL);

    rc = thread_init(&device->ld_device_thread, lrs_dev_thread, device);
    if (rc)
        LOG_RETURN(rc, "Could not create device thread");

    return 0;
}

int wrap_lib_open(enum rsc_family dev_type, struct lib_handle *lib_hdl)
{
    const char *lib_dev;
    int rc = -1;

    /* non-tape/RADOS cases: dummy lib adapter (no open required) */
    if (dev_type != PHO_RSC_TAPE && dev_type != PHO_RSC_RADOS_POOL)
        return get_lib_adapter(PHO_LIB_DUMMY, &lib_hdl->ld_module);

    /* tape case */
    if (dev_type == PHO_RSC_TAPE)
        rc = get_lib_adapter(PHO_LIB_SCSI, &lib_hdl->ld_module);

    /* RADOS case */
    if (dev_type == PHO_RSC_RADOS_POOL)
        rc = get_lib_adapter(PHO_LIB_RADOS, &lib_hdl->ld_module);

    if (rc)
        LOG_RETURN(rc, "Failed to get library adapter");

    /* For now, one single configurable path to library device.
     * This will have to be changed to manage multiple libraries.
     */
    lib_dev = PHO_CFG_GET(cfg_lrs, PHO_CFG_LRS, lib_device);
    if (!lib_dev)
        LOG_RETURN(rc, "Failed to get default library device from config");

    return ldm_lib_open(lib_hdl, lib_dev);
}

int lrs_dev_technology(const struct lrs_dev *dev, const char **techno)
{
    const char *supported_list_csv;
    size_t nb_technologies;
    char **supported_list;
    char **device_models;
    int rc = 0;
    size_t i;

    ENTRY;

    supported_list_csv = PHO_CFG_GET(cfg_tape_model, PHO_CFG_TAPE_MODEL,
                                     supported_list);
    if (supported_list_csv == NULL)
        LOG_RETURN(-EINVAL, "Failed to read 'supported_list' in 'tape_model'");

    get_val_csv(supported_list_csv, &supported_list, &nb_technologies);

    for (i = 0; i < nb_technologies; i++) {
        const char *device_model_csv;
        char *section_name;
        size_t nb_drives;
        size_t j;

        rc = asprintf(&section_name, "drive_type \"%s_drive\"",
                      supported_list[i]);
        if (rc == -1)
            goto free_supported_list;

        rc = pho_cfg_get_val(section_name, "models", &device_model_csv);
        if (rc) {
            free(section_name);
            if (rc == -ENODATA)
                /* Not every elements of supported_list necessarily has a drive
                 * type in the config.
                 */
                continue;

            LOG_GOTO(free_supported_list, rc,
                     "failed to read 'drive_rw' in '%s'", section_name);
        }

        get_val_csv(device_model_csv, &device_models, &nb_drives);

        free(section_name);

        for (j = 0; j < nb_drives; j++) {
            if (!strcmp(device_models[j], dev->ld_dss_dev_info->rsc.model)) {
                *techno = supported_list[i];
                supported_list[i] = NULL; // do not free it later
                break;
            }
        }

        for (j = 0; j < nb_drives; j++)
            free(device_models[j]);
        free(device_models);

        if (*techno)
            break;
    }

free_supported_list:
    for (i = 0; i < nb_technologies; i++)
        free(supported_list[i]);
    free(supported_list);

    return rc;
}

/* This function is only called when we expect a loaded device.
 * Return 0 only if the device is loaded.
 */
static int check_device_state_for_load(struct lrs_dev *dev)
{
    if (dev_is_failed(dev))
        LOG_RETURN(-EINVAL,
                   "cannot use failed device '%s' to load a medium",
                   lrs_dev_name(dev));

    if (dev_is_empty(dev))
        LOG_RETURN(-EINVAL,
                   "device '%s' received a %s sub request with no medium but "
                   "the device state is %s, abort",
                   lrs_dev_name(dev),
                   pho_srl_request_kind_str(dev->ld_sub_request->reqc->req),
                   op_status2str(dev->ld_op_status));

    return 0;
}

/**
 * Load \p medium_to_load in \p dev
 *
 * This function makes sure that we unload any tape currently in \p dev by
 * unmounting the medium first then unloading it.
 *
 * \param[in/out] dev            the device on which to load \p medium_to_load
 * \param[in]     medium_to_load the medium to load into \p dev
 *                               It can be NULL if we want to keep the same
 *                               medium in the device but still make sure that
 *                               it is in the proper state (eg. mount it if
 *                               necessary).
 * \param[in/out] context        Additional context to specify the actions to
 *                               take and whether they where successful or not.
 *
 * \return                       0 on success, negative POSIX error code on
 *                               error
 *
 */
static int dev_medium_switch(struct lrs_dev *dev,
                             struct media_info *medium_to_load,
                             struct medium_switch_context *context)
{
    struct sub_request *sub_req;
    struct req_container *reqc;
    int rc;

    sub_req = dev->ld_sub_request;
    reqc = sub_req->reqc;

    if (!medium_to_load) {
        /* medium already loaded in device */
        pho_debug("medium_to_alloc for device '%s' is NULL", lrs_dev_name(dev));
        rc = check_device_state_for_load(dev);
        if (rc) {
            sub_req->failure_on_medium = true;
            return rc;
        }

        return 0;
    }

    rc = dev_empty(dev);
    if (rc) {
        context->failure_on_device = true;
        LOG_RETURN(rc, "failed to empty device '%s' to load medium '%s'"
                   "for %s request",
                   lrs_dev_name(dev), medium_to_load->rsc.id.name,
                   pho_srl_request_kind_str(reqc->req));
    }

    pho_debug("Will load medium '%s' in device '%s'", lrs_dev_name(dev),
              medium_to_load->rsc.id.name);
    rc = dev_load(dev, &medium_to_load, context->release_medium,
                  context->free_medium);
    if (rc == -EBUSY)
        return rc;

    if (rc) {
        context->failure_on_device = true;
        LOG_RETURN(rc,
                   "failed to load medium '%s' in device '%s' for %s request",
                   medium_to_load->rsc.id.name,
                   lrs_dev_name(dev),
                   pho_srl_request_kind_str(reqc->req));
    }

    return 0;
}

/**
 * Load and mount \p medium_to_mount in \p dev
 *
 * This fonction work in a similar way to dev_medium_switch but ensure that the
 * medium is mounted after the load.
 */
static int dev_medium_switch_mount(struct lrs_dev *dev,
                                   struct media_info *medium_to_mount,
                                   struct medium_switch_context *context)
{
    struct sub_request *subreq;
    struct req_container *reqc;
    int rc;

    subreq = dev->ld_sub_request;
    reqc = subreq->reqc;

    rc = dev_medium_switch(dev, medium_to_mount, context);
    if (rc)
        return rc;

    if (dev_is_mounted(dev))
        return 0;

    rc = dev_mount(dev);
    if (rc) {
        context->failure_on_device = true;
        subreq->failure_on_medium = true;
        MUTEX_LOCK(&dev->ld_mutex);
        dev->ld_op_status = PHO_DEV_OP_ST_FAILED;
        MUTEX_UNLOCK(&dev->ld_mutex);
        pho_error(rc,
                  "failed to mount medium '%s' in device '%s' "
                  "for %s request. Will try another medium if possible.",
                  // FIXME use medium_to_mount when it no longer set to NULL
                  dev->ld_dss_media_info->rsc.id.name,
                  lrs_dev_name(dev),
                  pho_srl_request_kind_str(reqc->req));
        /* Set the medium to failed early to make sure that the scheduler won't
         * reuse it.
         */
        fail_release_free_medium(dev, &dev->ld_dss_media_info, true);
        return rc;
    }

    /* LTFS can cunningly mount almost-full tapes as read-only, and so would
     * damaged disks. Mark the media as full, let it be mounted and try to find
     * a new one.
     */
    if (pho_request_is_write(reqc->req) && !dev_mount_is_writable(dev)) {
        int rc2;

        pho_warn("Media '%s' OK but mounted R/O for %s request, "
                 "marking full and retrying...",
                 dev->ld_dss_media_info->rsc.id.name,
                 pho_srl_request_kind_str(reqc->req));
        subreq->failure_on_medium = true;
        rc = -ENOSPC;

        MUTEX_LOCK(&dev->ld_mutex);
        dev->ld_dss_media_info->fs.status = PHO_FS_STATUS_FULL;
        MUTEX_UNLOCK(&dev->ld_mutex);
        rc2 = dss_media_set(&dev->ld_device_thread.dss,
                            dev->ld_dss_media_info, 1,
                            DSS_SET_UPDATE, FS_STATUS);
        if (rc2) {
            rc = rc2;
            context->failure_on_device = true;
            LOG_RETURN(rc, "Unable to update DSS media '%s' status to FULL",
                       dev->ld_dss_media_info->rsc.id.name);
        }
    }

    return 0;
}
