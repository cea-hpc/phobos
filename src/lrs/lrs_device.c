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

    (*dev)->ld_response_queue = sched->response_queue;
    (*dev)->ld_ongoing_format = &sched->ongoing_format;
    (*dev)->sched_req_queue = &sched->req_queue;
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
    sched_req_free(dev->ld_format_request);
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
static int dev_sync(struct lrs_dev *dev)
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

    return rc;
}

/**
 * Umount medium of device but let it loaded and locked.
 */
static int dev_umount(struct lrs_dev *dev)
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

static int dss_medium_release(struct dss_handle *dss, struct media_info *medium)
{
    int rc;

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

    rc = dss_unlock(dss, DSS_DEVICE, dev, 1, false);
    if (rc)
        LOG_RETURN(rc,
                   "Error when releasing device '%s' with current lock "
                   "(hostname %s, owner %d)", dev->rsc.id.name,
                   dev->lock.hostname, dev->lock.owner);

    pho_lock_clean(&dev->lock);
    return 0;
}

/**
 * Unload medium from device
 *
 * - DSS unlock the medium
 * - set drive's ld_op_status to PHO_DEV_OP_ST_EMPTY
 */
static int dev_unload(struct lrs_dev *dev)
{
    /* let the library select the target location */
    struct lib_item_addr    free_slot = { .lia_type = MED_LOC_UNKNOWN };
    struct lib_adapter      lib;
    int                     rc2;
    int                     rc;

    ENTRY;

    pho_verb("Unloading '%s' from '%s'", dev->ld_dss_media_info->rsc.id.name,
             dev->ld_dev_path);

    rc = wrap_lib_open(dev->ld_dss_dev_info->rsc.id.family, &lib);
    if (rc)
        LOG_GOTO(out, rc,
                 "Unable to open lib '%s' to unload medium '%s' from device "
                 "'%s'", rsc_family_names[dev->ld_dss_dev_info->rsc.id.family],
                 dev->ld_dss_media_info->rsc.id.name, dev->ld_dev_path);

    rc = ldm_lib_media_move(&lib, &dev->ld_lib_dev_info.ldi_addr, &free_slot);
    if (rc != 0)
        /* Set operational failure state on this drive. It is incomplete since
         * the error can originate from a defective tape too...
         *  - consider marking both as failed.
         *  - consider maintaining lists of errors to diagnose and decide who to
         *    exclude from the cool game.
         */
        LOG_GOTO(out_close, rc, "Media move failed");

    dev->ld_op_status = PHO_DEV_OP_ST_EMPTY;

out_close:
    rc2 = ldm_lib_close(&lib);
    if (rc2)
        rc = rc ? : rc2;

out:
    if (!rc) {
        rc2 = dss_medium_release(&dev->ld_device_thread.ld_dss,
                                 dev->ld_dss_media_info);
        if (rc2)
            rc = rc ? : rc2;

        media_info_free(dev->ld_dss_media_info);
        dev->ld_dss_media_info = NULL;
    }

    return rc;
}

/**
 * If a medium is into dev, umount, unload and release its locks.
 */
static int dev_empty(struct lrs_dev *dev)
{
    int rc;

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
    media_info->rsc.adm_status = PHO_RSC_ADM_ST_FAILED;
    return dss_media_set(dss, media_info, 1, DSS_SET_UPDATE, ADM_STATUS);
}

static void fail_release_free_medium(struct dss_handle *dss,
                                     struct media_info *medium)
{
    int rc;

    rc = dss_set_medium_to_failed(dss, medium);
    if (rc) {
        pho_error(rc,
                  "Warning we keep medium %s locked because we can't set it to "
                  "failed into DSS", medium->rsc.id.name);
    } else {
        rc = dss_medium_release(dss, medium);
        if (rc)
            pho_error(rc,
                      "Error when releasing medium %s after setting it to "
                      "status failed", medium->rsc.id.name);
    }

    media_info_free(medium);
}

/**
 * Load a medium into a drive or return -EBUSY to retry later
 *
 * The loaded medium is registered as dev->ld_dss_media_info or DSS unlock and
 * freed if error.
 *
 * If an error occurs on a medium that is not registered to
 * dev->ld_dss_media_info, the medium is considered failed, marked as such in
 * the DSS, and freed. WARNING: if we cannot set it to failed into the DSS, the
 * medium DSS lock is not released
 *
 * @param[out]  failure_on_dev  Return false if no error or error not due to the
 *                              device, return true if there is an error due to
 *                              the device
 *
 * @return 0 on success, -error number on error. -EBUSY is returned when a
 * drive to drive medium movement was prevented by the library or if the device
 * is empty.
 */
static int dev_load(struct lrs_dev *dev, struct media_info *medium,
                    bool *failure_on_dev)
{
    struct lib_item_addr medium_addr;
    struct lib_adapter lib;
    int rc2;
    int rc;

    ENTRY;

    *failure_on_dev = false;
    pho_verb("Loading '%s' into '%s'", medium->rsc.id.name, dev->ld_dev_path);

    /* get handle to the library depending on device type */
    rc = wrap_lib_open(dev->ld_dss_dev_info->rsc.id.family, &lib);
    if (rc) {
        *failure_on_dev = true;
        rc2 = dss_medium_release(&dev->ld_device_thread.ld_dss,
                                 dev->ld_dss_media_info);
        if (rc2)
            pho_error(rc2, "Error when releasing a medium during device unload "
                      "error");

        media_info_free(medium);
        return rc;
    }

    /* lookup the requested medium */
    rc = ldm_lib_media_lookup(&lib, medium->rsc.id.name, &medium_addr);
    if (rc) {
        fail_release_free_medium(&dev->ld_device_thread.ld_dss, medium);
        LOG_GOTO(out_close, rc, "Media lookup failed");
    }

    rc = ldm_lib_media_move(&lib, &medium_addr, &dev->ld_lib_dev_info.ldi_addr);
    /* A movement from drive to drive can be prohibited by some libraries.
     * If a failure is encountered in such a situation, it probably means that
     * the state of the library has changed between the moment it has been
     * scanned and the moment the medium and drive have been selected. The
     * easiest solution is therefore to return EBUSY to signal this situation to
     * the caller.
     */
    if (rc == -EINVAL
            && medium_addr.lia_type == MED_LOC_DRIVE
            && dev->ld_lib_dev_info.ldi_addr.lia_type == MED_LOC_DRIVE) {
        pho_debug("Failed to move a medium from one drive to another, trying "
                  "again later");
        /* @TODO: acquire source drive on the fly? */
        GOTO(out_close, rc = -EBUSY);
    } else if (rc) {
        /* Set operationnal failure state on this drive. It is incomplete since
         * the error can originate from a defect tape too...
         *  - consider marking both as failed.
         *  - consider maintaining lists of errors to diagnose and decide who to
         *    exclude from the cool game.
         */
        fail_release_free_medium(&dev->ld_device_thread.ld_dss, medium);
        *failure_on_dev = true;
        LOG_GOTO(out_close, rc, "Media move failed");
    }

    /* update device status */
    dev->ld_op_status = PHO_DEV_OP_ST_LOADED;
    dev->ld_dss_media_info = medium;
    rc = 0;

out_close:
    rc2 = ldm_lib_close(&lib);
    if (rc2) {
        *failure_on_dev = true;
        pho_error(rc2, "Unable to close lib");
        rc = rc ? : rc2;
    }

    return rc;
}

/**
 * Format a medium to the given fs type.
 *
 * \param[in]   dev     Device with a loaded medium to format
 * \param[in]   fsa     Filesystem adapter
 * \param[in]   unlock  Put admin status to "unlocked" on format success
 *
 * \return              0 on success, negative error code on failure
 */
static int dev_format(struct lrs_dev *dev, struct fs_adapter *fsa, bool unlock)
{
    struct media_info *medium = dev->ld_dss_media_info;
    struct ldm_fs_space space = {0};
    uint64_t fields = 0;
    int rc;

    ENTRY;

    pho_verb("format: medium '%s'", medium->rsc.id.name);

    rc = ldm_fs_format(fsa, dev->ld_dev_path, medium->rsc.id.name, &space);
    if (rc)
        LOG_RETURN(rc, "Cannot format media '%s'", medium->rsc.id.name);

    /* Systematically use the media ID as filesystem label */
    strncpy(medium->fs.label, medium->rsc.id.name, sizeof(medium->fs.label));
    medium->fs.label[sizeof(medium->fs.label) - 1] = '\0';
    fields |= FS_LABEL;

    medium->stats.phys_spc_used = space.spc_used;
    medium->stats.phys_spc_free = space.spc_avail;
    fields |= PHYS_SPC_USED | PHYS_SPC_FREE;

    /* Post operation: update media information in DSS */
    medium->fs.status = PHO_FS_STATUS_EMPTY;
    fields |= FS_STATUS;

    if (unlock) {
        pho_verb("Unlocking media '%s' after format", medium->rsc.id.name);
        medium->rsc.adm_status = PHO_RSC_ADM_ST_UNLOCKED;
        fields |= ADM_STATUS;
    }

    rc = dss_media_set(&dev->ld_device_thread.ld_dss, medium, 1, DSS_SET_UPDATE,
                       fields);
    if (rc != 0)
        LOG_RETURN(rc, "Failed to update state of media '%s' after format",
                   medium->rsc.id.name);
    return rc;
}

static int queue_format_response(struct tsqueue *response_queue,
                                 struct req_container *reqc)
{
    struct resp_container *respc = NULL;
    int rc;

    respc = malloc(sizeof(*respc));
    if (!respc)
        LOG_GOTO(send_err, rc = -ENOMEM, "Unable to allocate format respc");

    respc->socket_id = reqc->socket_id;
    respc->resp = malloc(sizeof(*respc->resp));
    if (!respc->resp)
        LOG_GOTO(err_respc, rc = -ENOMEM,
                 "Unable to allocate format respc->resp");

    rc = pho_srl_response_format_alloc(respc->resp);
    if (rc)
        goto err_respc_resp;

    /* Build the answer */
    respc->resp->req_id = reqc->req->id;
    respc->resp->format->med_id->family = reqc->req->format->med_id->family;
    rc = strdup_safe(&respc->resp->format->med_id->name,
                     reqc->req->format->med_id->name);
    if (rc)
        LOG_GOTO(err_format, rc,
                 "Error on duplicating medium name in format response");

    tsqueue_push(response_queue, respc);
    return 0;

err_format:
    pho_srl_response_free(respc->resp, false);
err_respc_resp:
    free(respc->resp);
err_respc:
    free(respc);
send_err:
    return queue_error_response(response_queue, rc, reqc);
}

static int dev_handle_format(struct lrs_dev *dev)
{
    struct media_info *medium_to_format =
        dev->ld_format_request->params.format.medium_to_format;
    int rc;

    if (dev->ld_op_status == PHO_DEV_OP_ST_LOADED &&
        !strcmp(dev->ld_dss_media_info->rsc.id.name,
                medium_to_format->rsc.id.name)) {
        /*
         * medium to format already loaded, use existing dev->ld_dss_media_info,
         * free  dev->ld_format_request->params.format.medium_to_format
         */
        pho_verb("medium %s to format is already loaded into device %s",
                 dev->ld_dss_media_info->rsc.id.name,
                 dev->ld_dss_dev_info->rsc.id.name);
        media_info_free(medium_to_format);
        dev->ld_format_request->params.format.medium_to_format = NULL;
    } else {
        bool failure_on_dev;

        rc = dev_empty(dev);
        if (rc) {
            int rc2;

            pho_error(rc, "Error when emptying device %s to format medium %s",
                      dev->ld_dss_dev_info->rsc.id.name,
                      medium_to_format->rsc.id.name);
            /* release medium that cannot be loaded */
            rc2 = dss_medium_release(&dev->ld_device_thread.ld_dss,
                                     medium_to_format);
            if (rc2) {
                pho_error(rc2,
                          "Unable to release medium %s that we planned to "
                          "format in device '%s'",
                          medium_to_format->rsc.id.name,
                          dev->ld_dss_dev_info->rsc.id.name);
                rc2 = queue_error_response(dev->ld_response_queue, rc2,
                                           dev->ld_format_request);
                if (rc2)
                    pho_error(rc,
                              "Unable to queue format error response for "
                              "medium '%s'", medium_to_format->rsc.id.name);

                goto out;
            } else {
                tsqueue_push(dev->sched_req_queue, dev->ld_format_request);
                LOG_GOTO(out_no_req_free, rc,
                         "Unable to empty device '%s' to format medium '%s', "
                         "format request is requeued",
                         dev->ld_dss_dev_info->rsc.id.name,
                         medium_to_format->rsc.id.name);
            }
        }

        rc = dev_load(dev, medium_to_format, &failure_on_dev);
        if (rc == -EBUSY) {
            pho_warn("Trying to load a busy medium to format, try again later");
            return 0;
        }

        dev->ld_format_request->params.format.medium_to_format = NULL;
        if (rc) {
            if (failure_on_dev) {
                LOG_GOTO(out_response, rc, "Error when loading medium to "
                         "format in device %s",
                         dev->ld_dss_dev_info->rsc.id.name);
            } else {
                pho_error(rc, "Error on medium only when loading to format in "
                          "device %s", dev->ld_dss_dev_info->rsc.id.name);
                rc = queue_error_response(dev->ld_response_queue, rc,
                                          dev->ld_format_request);
                if (rc)
                    pho_error(rc, "Unable to queue format error response");

                goto out;
            }
        }
    }

    rc = dev_format(dev, &dev->ld_format_request->params.format.fsa,
                    dev->ld_format_request->req->format->unlock);

out_response:
    if (rc) {
        int rc2;

        rc2 = queue_error_response(dev->ld_response_queue, rc,
                                   dev->ld_format_request);
        if (rc2)
            pho_error(rc2, "Unable to queue format error response");
    } else {
        rc = queue_format_response(dev->ld_response_queue,
                                   dev->ld_format_request);
        if (rc)
            pho_error(rc, "Unable to queue format response");
    }

out:
    sched_req_free(dev->ld_format_request);

out_no_req_free:
    dev->ld_format_request = NULL;
    dev->ld_ongoing_io = false;
    format_medium_remove(dev->ld_ongoing_format, medium_to_format);
    return rc;
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
    struct req_container *format_request = device->ld_format_request;
    int rc = 0;

    if (!device->ld_format_request)
        return;

    if (device->ld_device_thread.ld_status &&
        !format_request->params.format.medium_to_format) {
        /*
         * A NULL medium_to_format field in the format request means the medium
         * has been transfered to the device.
         */
        format_medium_remove(device->ld_ongoing_format,
                             device->ld_dss_media_info);
        rc = queue_error_response(device->ld_response_queue,
                                  device->ld_device_thread.ld_status,
                                  format_request);
        if (rc)
            pho_error(rc,
                      "Unable to send error for format request of medium '%s'",
                      format_request->req->format->med_id->name);

        sched_req_free(format_request);
    } else {
        if (format_request->params.format.medium_to_format) {
            struct media_info *medium_to_format =
                format_request->params.format.medium_to_format;

            format_medium_remove(device->ld_ongoing_format, medium_to_format);
            rc = dss_medium_release(&device->ld_device_thread.ld_dss,
                                    medium_to_format);
            if (rc) {
                medium_to_format->rsc.adm_status = PHO_RSC_ADM_ST_FAILED;
                rc = dss_media_set(&device->ld_device_thread.ld_dss,
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
            tsqueue_push(device->sched_req_queue, format_request);
        } else {
            rc = queue_error_response(device->ld_response_queue, rc,
                                      format_request);
            if (rc)
                pho_error(rc,
                          "Unable to send error to format request of medium "
                          "'%s'", format_request->req->format->med_id->name);

            sched_req_free(format_request);
        }
    }

    device->ld_format_request = NULL;
}

/**
 * Manage a mounted medium at device thread end.
 *
 * If mounted medium:
 *     if no error:
 *         umount mounted medium. The umount cleans tosync requests,
 *     if error:
 *         set mounted medium as FAILED into DSS and release corresponding
 *         DSS lock except if we failed to set the DSS status to FAILED,
 *         clean tosync requests by sending errors.
 */
static void dev_thread_end_mounted_medium(struct lrs_dev *device)
{
    int rc = 0;

    if (device->ld_op_status != PHO_DEV_OP_ST_MOUNTED)
        return;

    if (!device->ld_device_thread.ld_status) {
        rc = dev_umount(device);
        if (rc) {
            pho_error(rc, "Unable to umount medium '%s' in device '%s' exit",
                      device->ld_dss_media_info->rsc.id.name,
                      device->ld_dss_dev_info->rsc.id.name);
            device->ld_device_thread.ld_status = rc;
        }
    }

    if (device->ld_device_thread.ld_status && device->ld_dss_media_info) {
        fail_release_free_medium(&device->ld_device_thread.ld_dss,
                                 device->ld_dss_media_info);
        device->ld_dss_media_info = NULL;
    }
}

/**
 * Manage a loaded medium at device thread end
 *
 * If loaded medium:
 *     if no error:
 *         let the medium loaded but release medium DSS lock,
 *     if error:
 *         set loaded medium as FAILED into DSS and release corresponding,
 *         DSS lock except if we failed to set the DSS status to FAILED.
 */
static void dev_thread_end_loaded_medium(struct lrs_dev *device)
{
    if (device->ld_op_status != PHO_DEV_OP_ST_LOADED)
        return;

    if (!device->ld_device_thread.ld_status) {
        int rc;

        rc = dss_medium_release(&device->ld_device_thread.ld_dss,
                                device->ld_dss_media_info);
        if (rc) {
            pho_error(rc,
                      "Unable to release DSS lock of medium '%s' of device "
                      "'%s' at device exit",
                      device->ld_dss_media_info->rsc.id.name,
                      device->ld_dss_dev_info->rsc.id.name);
            device->ld_device_thread.ld_status = rc;
        } else {
            media_info_free(device->ld_dss_media_info);
        }
    }

    if (device->ld_device_thread.ld_status && device->ld_dss_media_info)
        fail_release_free_medium(&device->ld_device_thread.ld_dss,
                                 device->ld_dss_media_info);

    device->ld_dss_media_info = NULL;
}

/**
 * Manage device and tosync_array at device thread end.
 *
 * If no error:
 *     release device DSS lock.
 *
 * If error:
 *     - clean tosync array,
 *     - set device op_status to FAILED,
 *     - set device adm_status to FAILED into DSS and release corresponding
 *       DSS lock except if we failed to set the DSS status to FAILED.
 *
 * Set device->ld_ongoing_io to false.
 */
static void dev_thread_end_device(struct lrs_dev *device)
{
    struct dss_handle *dss = &device->ld_device_thread.ld_dss;
    int rc;

    if (!device->ld_device_thread.ld_status) {
        rc = dss_device_release(dss, device->ld_dss_dev_info);
        if (rc) {
            pho_error(rc, "Unable to release DSS lock of device '%s' at exit",
                      device->ld_dss_dev_info->rsc.id.name);
            device->ld_device_thread.ld_status = rc;
        }
    }

    if (device->ld_device_thread.ld_status) {
        rc = clean_tosync_array(device, device->ld_device_thread.ld_status);
        if (rc)
            pho_error(rc,
                      "Failed to clean tosync array of device '%s' at exit",
                      device->ld_dss_dev_info->rsc.id.name);

        device->ld_op_status = PHO_DEV_OP_ST_FAILED;
        if (device->ld_dss_dev_info) {
            device->ld_dss_dev_info->rsc.adm_status = PHO_RSC_ADM_ST_FAILED;
            rc = dss_device_update_adm_status(dss, device->ld_dss_dev_info, 1);
            if (rc) {
                pho_error(rc,
                          "Unable to set device '%s' as PHO_RSC_ADM_ST_FAILED "
                          "into DSS, we don't release the corresponding DSS "
                          "lock", device->ld_dss_dev_info->rsc.id.name);
            } else {
                rc = dss_device_release(dss, device->ld_dss_dev_info);
                if (rc)
                    pho_error(rc,
                              "Unable to release DSS lock of device '%s' at "
                              "device exit",
                              device->ld_dss_dev_info->rsc.id.name);
            }
        }
    }

    device->ld_ongoing_io = false;
}

static void dev_thread_end(struct lrs_dev *device)
{
    /* prevent any new scheduled request to this device */
    device->ld_device_thread.ld_running = false;

    cancel_pending_format(device);
    dev_thread_end_mounted_medium(device);
    dev_thread_end_loaded_medium(device);
    dev_thread_end_device(device);
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

        if (device->ld_needs_sync && !device->ld_ongoing_io) {
            rc = dev_sync(device);
            if (rc)
                LOG_GOTO(end_thread, thread->ld_status = rc, "device thread "
                         "'%s': fatal error syncing device",
                         device->ld_dss_dev_info->rsc.id.name);
        }

        if (device->ld_ongoing_io && device->ld_format_request) {
            rc = dev_handle_format(device);
            if (rc)
                LOG_GOTO(end_thread, thread->ld_status = rc,
                         "device thread '%s': fatal error handling format",
                         device->ld_dss_dev_info->rsc.id.name);
        }

        rc = wait_for_signal(device);

        if (rc < 0)
            LOG_GOTO(end_thread, thread->ld_status = rc,
                     "device thread '%s': fatal error",
                     device->ld_dss_dev_info->rsc.id.name);
    }

end_thread:
    dev_thread_end(device);
    pthread_exit(&device->ld_device_thread.ld_status);
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

void dev_thread_signal_stop_on_error(struct lrs_dev *device, int error_code)
{
    device->ld_device_thread.ld_status = error_code;
    dev_thread_signal_stop(device);
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

int wrap_lib_open(enum rsc_family dev_type, struct lib_adapter *lib)
{
    const char *lib_dev;
    int         rc;

    /* non-tape cases: dummy lib adapter (no open required) */
    if (dev_type != PHO_RSC_TAPE)
        return get_lib_adapter(PHO_LIB_DUMMY, lib);

    /* tape case */
    rc = get_lib_adapter(PHO_LIB_SCSI, lib);
    if (rc)
        LOG_RETURN(rc, "Failed to get library adapter");

    /* For now, one single configurable path to library device.
     * This will have to be changed to manage multiple libraries.
     */
    lib_dev = PHO_CFG_GET(cfg_lrs, PHO_CFG_LRS, lib_device);
    if (!lib_dev)
        LOG_RETURN(rc, "Failed to get default library device from config");

    return ldm_lib_open(lib, lib_dev);
}
