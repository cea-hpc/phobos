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
#include "pho_dss.h"
#include "pho_io.h"
#include "pho_ldm.h"
#include "pho_type_utils.h"

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
                                  struct lrs_dev **dev,
                                  struct lrs_sched *sched)
{
    int rc;

    *dev = calloc(1, sizeof(**dev));
    if (!*dev)
        return -errno;

    (*dev)->ld_dss_dev_info = dev_info_dup(info);
    if (!(*dev)->ld_dss_dev_info)
        GOTO(err_dev, rc = -ENOMEM);

    sync_params_init(&(*dev)->ld_sync_params);

    rc = dss_init(&(*dev)->ld_device_thread.ld_dss);
    if (rc)
        GOTO(err_info, rc);

    (*dev)->ld_request_queue = tsqueue_init();
    (*dev)->ld_response_queue = sched->response_queue;
    (*dev)->ld_handle = handle;

    rc = dev_thread_init(*dev);
    if (rc)
        GOTO(err_dss, rc);

    g_ptr_array_add(handle->ldh_devices, *dev);

    /* Explicitly initialize to NULL so that lrs_dev_info_clean can call cleanup
     * functions.
     */
    (*dev)->ld_dss_media_info = NULL;
    (*dev)->ld_sys_dev_state.lds_model = NULL;
    (*dev)->ld_sys_dev_state.lds_serial = NULL;

    return 0;

err_dss:
    dss_fini(&(*dev)->ld_device_thread.ld_dss);
err_info:
    g_ptr_array_free((*dev)->ld_sync_params.tosync_array, true);
    dev_info_free((*dev)->ld_dss_dev_info, 1);
err_dev:
    free(dev);

    return rc;
}

/* request_tosync_free can be used as glib callback */
static void request_tosync_free(struct request_tosync *req_tosync)
{
    sched_req_free(req_tosync->reqc);
    free(req_tosync);
}

static void request_tosync_free_wrapper(gpointer _req, gpointer _null_user_data)
{
    (void)_null_user_data;

    request_tosync_free((struct request_tosync *)_req);
}

static void lrs_dev_info_clean(struct lrs_dev_hdl *handle,
                               struct lrs_dev *dev)
{
    dev_thread_wait_end(dev);

    media_info_free(dev->ld_dss_media_info);
    dev->ld_dss_media_info = NULL;

    ldm_dev_state_fini(&dev->ld_sys_dev_state);

    g_ptr_array_foreach(dev->ld_sync_params.tosync_array,
                        request_tosync_free_wrapper, NULL);
    g_ptr_array_unref(dev->ld_sync_params.tosync_array);
    dev_info_free(dev->ld_dss_dev_info, 1);
    dss_fini(&dev->ld_device_thread.ld_dss);

    tsqueue_destroy(&dev->ld_request_queue, sched_req_free);

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

    rc = lrs_dev_init_from_info(handle, dev_list, &dev, sched);
    if (rc)
        goto free_list;

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

        rc2 = lrs_dev_init_from_info(handle, &dev_list[i], &dev, sched);
        if (rc2) {
            rc = rc ? : rc2;
            continue;
        }

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
        *date = add_timespec(&now,
                             &dev->ld_handle->sync_time_threshold);
    } else {
        *date = add_timespec(oldest_tosync,
                             &dev->ld_handle->sync_time_threshold);

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
static int wait_for_signal(struct lrs_dev *dev)
{
    struct thread_info *thread = &dev->ld_device_thread;
    struct timespec time;
    int rc;

    rc = compute_wakeup_date(dev, &time);
    if (rc)
        return rc;

    MUTEX_LOCK(&thread->ld_signal_mutex);
    rc = pthread_cond_timedwait(&thread->ld_signal,
                                &thread->ld_signal_mutex,
                                &time);
    MUTEX_UNLOCK(&thread->ld_signal_mutex);
    if (rc != ETIMEDOUT)
        rc = -rc;

    return rc;
}

static int queue_release_response(struct tsqueue *response_queue,
                                  struct req_container *reqc)
{
    struct tosync_medium *tosync_media = reqc->params.release.tosync_media;
    size_t n_tosync_media = reqc->params.release.n_tosync_media;
    struct resp_container *respc = NULL;
    pho_resp_release_t *resp_release;
    size_t i;
    int rc;

    respc = malloc(sizeof(*respc));
    if (!respc)
        LOG_GOTO(err, rc = -ENOMEM, "Unable to allocate respc");

    respc->socket_id = reqc->socket_id;
    respc->resp = malloc(sizeof(*respc->resp));
    if (!respc->resp)
        LOG_GOTO(err_respc, rc = -ENOMEM, "Unable to allocate respc->resp");

    rc = pho_srl_response_release_alloc(respc->resp, n_tosync_media);
    if (rc)
        goto err_respc_resp;

    /* Build the answer */
    respc->resp->req_id = reqc->req->id;
    resp_release = respc->resp->release;
    for (i = 0; i < n_tosync_media; i++) {
        resp_release->med_ids[i]->family = tosync_media[i].medium.family;
        rc = strdup_safe(&resp_release->med_ids[i]->name,
                         tosync_media[i].medium.name);
        if (rc) {
            int j;

            for (j = i; j < n_tosync_media; j++)
                resp_release->med_ids[j]->name = NULL;

            LOG_GOTO(err_release, rc,
                     "Unable to duplicate resp_release->med_ids[%zu]->name", i);
        }
    }

    tsqueue_push(response_queue, respc);

    return 0;

err_release:
    pho_srl_response_free(respc->resp, false);
err_respc_resp:
    free(respc->resp);
err_respc:
    free(respc);
err:
    return queue_error_response(response_queue, rc, reqc);
}

/* This function MUST be called with a lock on \p req */
static inline bool is_request_tosync_ended(struct req_container *req)
{
    size_t i;

    for (i = 0; i < req->params.release.n_tosync_media; i++)
        if (req->params.release.tosync_media[i].status == SYNC_TODO)
            return false;

    return true;
}

/**
 *  TODO: will become a device thread static function when all media operations
 *  will be moved to device thread
 */
int clean_tosync_array(struct lrs_dev *dev, int rc)
{
    GPtrArray *tosync_array = dev->ld_sync_params.tosync_array;
    int internal_rc = 0;

    MUTEX_LOCK(&dev->ld_mutex);
    while (tosync_array->len) {
        struct request_tosync *req = tosync_array->pdata[tosync_array->len - 1];
        struct tosync_medium *tosync_medium =
            &req->reqc->params.release.tosync_media[req->medium_index];
        bool should_send_error = false;
        bool is_tosync_ended = false;
        int rc2;

        g_ptr_array_remove_index(tosync_array, tosync_array->len - 1);

        MUTEX_LOCK(&req->reqc->mutex);

        if (!rc) {
            tosync_medium->status = SYNC_DONE;
        } else {
            if (!req->reqc->params.release.rc) {
                /* this is the first SYNC_ERROR of this request */
                req->reqc->params.release.rc = rc;
                should_send_error = true;
            }

            tosync_medium->status = SYNC_ERROR;
        }

        is_tosync_ended = is_request_tosync_ended(req->reqc);

        MUTEX_UNLOCK(&req->reqc->mutex);

        if (should_send_error) {
            rc2 = queue_error_response(dev->ld_response_queue, rc, req->reqc);
            if (rc2)
                internal_rc = internal_rc ? : rc2;
        }

        if (is_tosync_ended) {
            if (!req->reqc->params.release.rc) {
                rc2 = queue_release_response(dev->ld_response_queue, req->reqc);
                if (rc2)
                    internal_rc = internal_rc ? : rc2;
            }

            request_tosync_free(req);
        }
    }

    /* sync operation acknowledgement */
    dev->ld_sync_params.tosync_size = 0;
    dev->ld_sync_params.oldest_tosync.tv_sec = 0;
    dev->ld_sync_params.oldest_tosync.tv_nsec = 0;
    dev->ld_needs_sync = false;
    MUTEX_UNLOCK(&dev->ld_mutex);

    return internal_rc;
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

int push_new_sync_to_device(struct lrs_dev *dev, struct req_container *reqc,
                            size_t medium_index)
{
    struct sync_params *sync_params = &dev->ld_sync_params;
    struct request_tosync *req_tosync;

    req_tosync = malloc(sizeof(*req_tosync));
    if (!req_tosync)
        LOG_RETURN(-ENOMEM, "Unable to allocate req_tosync");

    req_tosync->reqc = reqc;
    req_tosync->medium_index = medium_index;
    MUTEX_LOCK(&dev->ld_mutex);
    g_ptr_array_add(sync_params->tosync_array, req_tosync);
    sync_params->tosync_size +=
        reqc->params.release.tosync_media[medium_index].written_size;
    update_oldest_tosync(&sync_params->oldest_tosync, reqc->received_at);
    MUTEX_UNLOCK(&dev->ld_mutex);

    dev_thread_signal(dev);

    return 0;
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
        struct request_tosync *req_tosync = tosync_array->pdata[i];

        update_oldest_tosync(&dev->ld_sync_params.oldest_tosync,
                             req_tosync->reqc->received_at);
    }
}

/** remove from tosync_array when error occurs on other device */
static void dev_check_sync_cancel(struct lrs_dev *dev)
{
    GPtrArray *tosync_array = dev->ld_sync_params.tosync_array;
    bool need_oldest_update = false;
    int i;

    MUTEX_LOCK(&dev->ld_mutex);
    for (i = tosync_array->len - 1; i >= 0; i--) {
        struct request_tosync *req_tosync = tosync_array->pdata[i];
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

            tosync_medium->status = SYNC_CANCEL;
            is_tosync_ended = is_request_tosync_ended(req_tosync->reqc);
        }

        MUTEX_UNLOCK(&req_tosync->reqc->mutex);

        if (is_tosync_ended)
            request_tosync_free(req_tosync);
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
                      (sync_params->tosync_array->len >=
                           handle->sync_nb_req_threshold ||
                       is_past(add_timespec(&sync_params->oldest_tosync,
                                            &handle->sync_time_threshold)) ||
                       sync_params->tosync_size >=
                           handle->sync_written_size_threshold);
    dev->ld_needs_sync |= (!running && sync_params->tosync_array->len > 0);
    MUTEX_UNLOCK(&dev->ld_mutex);
}

static int medium_sync(struct media_info *media_info, const char *fsroot)
{
    struct io_adapter    ioa;
    int                  rc;

    ENTRY;

    rc = get_io_adapter(media_info->fs.type, &ioa);
    if (rc)
        LOG_RETURN(rc, "No suitable I/O adapter for filesystem type: '%s'",
                   fs_type2str(media_info->fs.type));

    rc = ioa_medium_sync(&ioa, fsroot);
    pho_debug("sync: medium=%s rc=%d", media_info->rsc.id.name, rc);
    if (rc)
        LOG_RETURN(rc, "Cannot flush media at: %s", fsroot);

    return rc;
}

/** Update media_info stats and push its new state to the DSS */
static int lrs_dev_media_update(struct dss_handle *dss,
                                struct media_info *media_info,
                                size_t size_written, int media_rc,
                                const char *fsroot, long long nb_new_obj)
{
    struct ldm_fs_space space = {0};
    struct fs_adapter fsa;
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
        media_info->rsc.adm_status = PHO_RSC_ADM_ST_FAILED;
        fields |= ADM_STATUS;
    } else {
        rc2 = ldm_fs_df(&fsa, fsroot, &space);
        if (rc2) {
            rc = rc ? : rc2;
            pho_error(rc2, "Cannot retrieve media usage information");
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
static void dev_sync(struct lrs_dev *dev)
{
    struct sync_params *sync_params = &dev->ld_sync_params;
    int rc2;
    int rc;

    /* sync operation */
    MUTEX_LOCK(&dev->ld_mutex);
    rc = medium_sync(dev->ld_dss_media_info, dev->ld_mnt_path);
    rc2 = lrs_dev_media_update(&dev->ld_device_thread.ld_dss,
                               dev->ld_dss_media_info,
                               sync_params->tosync_size, rc, dev->ld_mnt_path,
                               sync_params->tosync_array->len);

    MUTEX_UNLOCK(&dev->ld_mutex);
    if (rc2) {
        rc = rc ? : rc2;
        pho_error(rc2, "Cannot update media information");
    }

    rc2 = clean_tosync_array(dev, rc);
    if (rc2) {
        rc = rc ? : rc2;
        pho_error(rc2, "Cannot clean tosync array");
    }

    if (rc) {
        dev->ld_op_status = PHO_DEV_OP_ST_FAILED;
        dev->ld_dss_dev_info->rsc.adm_status = PHO_RSC_ADM_ST_FAILED;
        rc2 = dss_device_update_adm_status(&dev->ld_device_thread.ld_dss,
                                           dev->ld_dss_dev_info,
                                           1);
        if (rc2)
            pho_error(rc2, "Unable to set device as failed into DSS");
    }
}

/**
 * Umount medium of device but let it loaded and locked.
 */
__attribute__ ((unused)) static int dev_umount(struct lrs_dev *dev)
{
    struct fs_adapter fsa;
    int rc, rc2;

    ENTRY;

    pho_info("umount: device '%s' mounted at '%s'",
             dev->ld_dev_path, dev->ld_mnt_path);
    rc = get_fs_adapter(dev->ld_dss_media_info->fs.type, &fsa);
    if (rc)
        LOG_GOTO(out, rc,
                 "Unable to get fs adapter '%s' to unmount medium '%s' from "
                 "device '%s'", fs_type_names[dev->ld_dss_media_info->fs.type],
                 dev->ld_dss_media_info->rsc.id.name, dev->ld_dev_path);

    rc = ldm_fs_umount(&fsa, dev->ld_dev_path, dev->ld_mnt_path);
    rc2 = clean_tosync_array(dev, rc);
    if (rc)
        LOG_GOTO(out, rc, "Failed to unmount device '%s' mounted at '%s'",
                 dev->ld_dev_path, dev->ld_mnt_path);

    /* update device state and unset mount path */
    dev->ld_op_status = PHO_DEV_OP_ST_LOADED;
    dev->ld_mnt_path[0] = '\0';

    if (rc2)
        LOG_GOTO(out, rc = rc2,
                 "Failed to clean tosync array after having unmounted device "
                 "'%s' mounted at '%s'",
                 dev->ld_dev_path, dev->ld_mnt_path);

out:
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

        rc = wait_for_signal(device);

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
