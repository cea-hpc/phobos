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
 * \brief  Phobos Local Resource Scheduler (LRS)
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "health.h"
#include "lrs_cache.h"
#include "lrs_cfg.h"
#include "lrs_device.h"
#include "lrs_sched.h"
#include "lrs_utils.h"
#include "pho_common.h"
#include "pho_daemon.h"
#include "pho_dss.h"
#include "pho_dss_wrapper.h"
#include "pho_io.h"
#include "pho_ldm.h"
#include "pho_srl_common.h"
#include "pho_type_utils.h"

#include <stdatomic.h>
#include <assert.h>
#include <glib.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#include <jansson.h>

static void *lrs_sched_thread(void *sdata);

static int format_media_init(struct format_media *format_media)
{
    int rc;

    rc = pthread_mutex_init(&format_media->mutex, NULL);
    if (rc)
        LOG_RETURN(rc, "Error on initializing format_media mutex");

    format_media->media = g_hash_table_new_full(g_pho_id_hash, g_pho_id_equal,
                                                free, free);
    return 0;
}

static void format_media_clean(struct format_media *format_media)
{
    int rc;

    rc = pthread_mutex_destroy(&format_media->mutex);
    if (rc)
        pho_error(rc, "Error on destroying format_media mutex");

    if (format_media->media) {
        g_hash_table_unref(format_media->media);
        format_media->media = NULL;
    }
}

/**
 * Add a medium to the format list if not already present
 *
 * @return true if medium is added, false if medium is already present
 */
static bool format_medium_add(struct format_media *format_media,
                              struct media_info *medium)
{
    pthread_mutex_lock(&format_media->mutex);
    if (g_hash_table_contains(format_media->media, &medium->rsc.id)) {
        pthread_mutex_unlock(&format_media->mutex);
        return false;
    }

    g_hash_table_insert(format_media->media, pho_id_dup(&medium->rsc.id),
                        pho_id_dup(&medium->rsc.id));
    pthread_mutex_unlock(&format_media->mutex);
    return true;
}

void format_medium_remove(struct format_media *format_media,
                          struct media_info *medium)
{
    pthread_mutex_lock(&format_media->mutex);
    g_hash_table_remove(format_media->media, &medium->rsc.id);
    pthread_mutex_unlock(&format_media->mutex);
}

void sched_req_free(void *reqc)
{
    struct req_container *cont = (struct req_container *)reqc;

    if (!cont)
        return;

    if (cont->req) {
        /* this function frees request specific memory, therefore needs to check
         * the request type internally and dereferences the cont->req
         */
        destroy_container_params(cont);
        pho_srl_request_free(cont->req, true);
    }

    pthread_mutex_destroy(&cont->mutex);
    free(cont);
}

bool is_rwalloc_ended(struct req_container *reqc)
{
    size_t i;

    for (i = 0; i < reqc->params.rwalloc.n_media; i++)
        if (reqc->params.rwalloc.media[i].status == SUB_REQUEST_TODO)
            return false;

    return true;
}

/** check that device info from DB is consistent with actual status */
static int check_dev_info(const struct lrs_dev *dev)
{
    ENTRY;

    if (dev->ld_dss_dev_info->rsc.model == NULL
        || dev->ld_sys_dev_state.lds_model == NULL) {
        if (dev->ld_dss_dev_info->rsc.model != dev->ld_sys_dev_state.lds_model)
            LOG_RETURN(-EINVAL, "%s: missing or unexpected device model",
                       dev->ld_dev_path);
        else
            pho_debug("%s: no device model is set", dev->ld_dev_path);

    } else if (cmp_trimmed_strings(dev->ld_dss_dev_info->rsc.model,
                                   dev->ld_sys_dev_state.lds_model)) {
        LOG_RETURN(-EINVAL, "%s: configured device model '%s' differs from "
                   "actual device model '%s'",
                   dev->ld_dev_path,
                   dev->ld_dss_dev_info->rsc.model,
                   dev->ld_sys_dev_state.lds_model);
    }

    if (dev->ld_sys_dev_state.lds_serial == NULL) {
        if (dev->ld_dss_dev_info->rsc.id.name !=
            dev->ld_sys_dev_state.lds_serial)
            LOG_RETURN(-EINVAL, "%s: missing or unexpected device serial",
                       dev->ld_dev_path);
        else
            pho_debug("%s: no device serial is set", dev->ld_dev_path);
    } else if (strcmp(dev->ld_dss_dev_info->rsc.id.name,
                      dev->ld_sys_dev_state.lds_serial) != 0) {
        LOG_RETURN(-EINVAL, "%s: configured device serial '%s' differs from "
                   "actual device serial '%s'",
                   dev->ld_dev_path,
                   dev->ld_dss_dev_info->rsc.id.name,
                   dev->ld_sys_dev_state.lds_serial);
    }

    return 0;
}

/**
 * Lock the corresponding item into the global DSS and update the local lock
 *
 * @param[in]       dss     DSS handle
 * @param[in]       type    DSS type of the item
 * @param[in]       item    item to lock
 * @param[in, out]  lock    already allocated lock to update
 */
static int take_and_update_lock(struct dss_handle *dss, enum dss_type type,
                                void *item, struct pho_lock *lock)
{
    int rc2;
    int rc;

    pho_lock_clean(lock);
    pho_verb("lock: %s '%s'", dss_type2str(type),
             type == DSS_DEVICE ?
             ((struct dev_info *)item)->rsc.id.name :
             type == DSS_MEDIA ?
             ((struct media_info *)item)->rsc.id.name :
             "???");
    rc = dss_lock(dss, type, item, 1);
    if (rc)
        pho_error(rc, "Unable to get lock on item for refresh");

    /* update lock values */
    rc2 = dss_lock_status(dss, type, item, 1, lock);
    if (rc2) {
        pho_error(rc2, "Unable to get status of new lock while refreshing");
        /* try to unlock before exiting */
        if (rc == 0) {
            dss_unlock(dss, type, item, 1, false);
            rc = rc2;
        }

        /* put a wrong lock value */
        lock->hostname = NULL;
        lock->owner = -1;
        lock->timestamp.tv_sec = 0;
        lock->timestamp.tv_usec = 0;
    }

    return rc;
}

/**
 * If lock->owner is different from lock_handle->lock_owner, renew the lock with
 * the current owner (PID).
 */
static int check_renew_owner(struct lock_handle *lock_handle,
                             enum dss_type type, void *item,
                             struct pho_lock *lock)
{
    int rc;

    if (lock->owner != lock_handle->lock_owner) {
        pho_warn("'%s' is already locked by owner %d, owner %d will "
                 "take ownership of this device",
                 dss_type_names[type], lock->owner, lock_handle->lock_owner);

        /**
         * Unlocking here is dangerous if there is another process than the
         * LRS on the same node that also acquires locks. If it becomes the case
         * we have to warn and return an error and we must not take the
         * ownership of this resource again.
         */
        /* unlock previous owner */
        rc = dss_unlock(lock_handle->dss, type, item, 1, true);
        if (rc)
            LOG_RETURN(rc,
                       "Unable to clear previous lock (hostname: %s, owner "
                       " %d) on item",
                       lock->hostname, lock->owner);

        /* get the lock again */
        rc = take_and_update_lock(lock_handle->dss, type, item,
                                  lock);
        if (rc)
            LOG_RETURN(rc, "Unable to get and refresh lock");
    }

    return 0;
}

/**
 * First, check that lock->hostname is the same as lock_handle->lock_hostname.
 * If not, -EALREADY is returned.
 *
 * Then, if lock->owner is different from lock_handle->lock_owner, renew the
 * lock with the current owner (PID) by calling check_renew_owner.
 */
static int check_renew_lock(struct lock_handle *lock_handle, enum dss_type type,
                            void *item, struct pho_lock *lock)
{
    if (strcmp(lock->hostname, lock_handle->lock_hostname)) {
        pho_verb("Resource already locked by host %s instead of %s",
                 lock->hostname, lock_handle->lock_hostname);
        return -EALREADY;
    }

    return check_renew_owner(lock_handle, type, item, lock);
}

int check_and_take_device_lock(struct lrs_sched *sched,
                               struct dev_info *dev)
{
    int rc;

    if (dev->lock.hostname) {
        rc = check_renew_lock(&sched->lock_handle, DSS_DEVICE, dev, &dev->lock);
        if (rc)
            LOG_RETURN(rc,
                       "Unable to check and renew lock of one of our devices "
                       "'%s'", dev->rsc.id.name);
    } else {
        rc = take_and_update_lock(&sched->sched_thread.dss, DSS_DEVICE, dev,
                                  &dev->lock);
        if (rc)
            LOG_RETURN(rc,
                       "Unable to acquire and update lock on device '%s'",
                       dev->rsc.id.name);
    }

    return 0;
}

/**
 * If a lock exists in the medium or in the DSS, check owner and renew it, else
 * take the lock.
 *
 * @param[in]       lock_handle     DSS lock handle
 * @param[in, out]  medium          medium to lock. The lock is updated if
 *                                  needed.
 * @return          0 if success, else a negative error code
 */
static int ensure_medium_lock(struct lock_handle *lock_handle,
                              struct media_info *medium)
{
    int rc = 0;

    /* check lock from DSS if it is not already filled */
    if (!medium->lock.hostname) {
        rc = dss_lock_status(lock_handle->dss, DSS_MEDIA, medium, 1,
                             &medium->lock);
        if (rc == -ENOLCK)
            rc = 0;

        if (rc)
            LOG_RETURN(rc, "Unable to status lock");
    }

    /* Check lock if it exists */
    if (medium->lock.hostname) {
        rc = check_renew_lock(lock_handle, DSS_MEDIA, medium, &medium->lock);
    } else {
    /* Try to take lock */
        rc = take_and_update_lock(lock_handle->dss, DSS_MEDIA, medium,
                                  &medium->lock);
    }

    return rc;
}

/**
 * Retrieve media info from DSS for the given ID.
 * @param pmedia[out] returned pointer to a media_info structure
 *                    allocated by this function.
 * @param id[in]      ID of the media.
 */
static int sched_fill_media_info(struct lock_handle *lock_handle,
                                 struct media_info **pmedia,
                                 const struct pho_id *id)
{
    int rc;

    pho_debug("Retrieving media info for (family '%s', name '%s', library "
              "'%s')",
              rsc_family2str(id->family), id->name, id->library);

    *pmedia = lrs_medium_acquire(id);
    if (!*pmedia)
        return -errno;

    if (!(*pmedia)->lock.hostname)
        goto log;

    rc = check_renew_lock(lock_handle, DSS_MEDIA, *pmedia,
                          &(*pmedia)->lock);
    if (rc) {
        if (rc == -EALREADY)
            pho_error(rc,
                      "Media (family '%s', name '%s', library '%s') is locked "
                      "by (hostname: %s, owner: %d)",
                      rsc_family2str(id->family), id->name, id->library,
                      (*pmedia)->lock.hostname, (*pmedia)->lock.owner);
        else
            pho_error(rc,
                      "Error while checking media (family '%s', name '%s', "
                      "library '%s') locked with hostname '%s' and owner '%d'",
                      rsc_family2str(id->family), id->name, id->library,
                      (*pmedia)->lock.hostname, (*pmedia)->lock.owner);
    }

log:
    pho_debug("%s: spc_free=%zd", (*pmedia)->rsc.id.name,
              (*pmedia)->stats.phys_spc_free);

    return 0;
}

int sched_fill_dev_info(struct lrs_sched *sched, struct lib_handle *lib_hdl,
                        struct lrs_dev *dev)
{
    struct dev_adapter_module *deva;
    struct dev_info *devi;
    int rc;

    ENTRY;

    if (dev == NULL)
        return -EINVAL;

    devi = dev->ld_dss_dev_info;

    lrs_medium_release(dev->ld_dss_media_info);
    dev->ld_dss_media_info = NULL;
    dev->ld_op_status = PHO_DEV_OP_ST_EMPTY;

    rc = get_dev_adapter(devi->rsc.id.family, &deva);
    if (rc)
        return rc;

    /* get path for the given serial */
    rc = ldm_dev_lookup(deva, devi->rsc.id.name, dev->ld_dev_path,
                        sizeof(dev->ld_dev_path));
    if (rc) {
        pho_debug("Device lookup failed: serial '%s'", devi->rsc.id.name);
        return rc;
    }

    /* now query device by path */
    ldm_dev_state_fini(&dev->ld_sys_dev_state);
    rc = ldm_dev_query(deva, dev->ld_dev_path, &dev->ld_sys_dev_state);
    if (rc) {
        pho_debug("Failed to query device '%s'", dev->ld_dev_path);
        return rc;
    }

    /* compare returned device info with info from DB */
    rc = check_dev_info(dev);
    if (rc)
        return rc;

    /* Query the library about the drive location and whether it contains
     * a media.
     */
    rc = ldm_lib_drive_lookup(lib_hdl, devi->rsc.id.name,
                              &dev->ld_lib_dev_info);
    if (rc) {
        pho_warn("Failed to query the library about device (family '%s', name "
                 "'%s', library '%s')",
                 rsc_family2str(devi->rsc.id.family), devi->rsc.id.name,
                 devi->rsc.id.library);
        return rc;
    }

    if (dev->ld_lib_dev_info.ldi_full) {
        struct fs_adapter_module *fsa;
        struct media_info *medium;
        struct pho_id *medium_id;

        dev->ld_op_status = PHO_DEV_OP_ST_LOADED;
        medium_id = &dev->ld_lib_dev_info.ldi_medium_id;

        pho_debug("Device '%s' (S/N '%s') contains medium (family '%s', name "
                  "'%s', library '%s')", dev->ld_dev_path, devi->rsc.id.name,
                  rsc_family2str(medium_id->family), medium_id->name,
                  medium_id->library);

        /* get media info for loaded drives */
        rc = sched_fill_media_info(&sched->lock_handle, &medium, medium_id);
        if (rc) {
            if (rc == -ENXIO)
                pho_error(rc,
                          "Device '%s' (S/N '%s') contains medium (family "
                          "'%s', name '%s', library '%s'), but this medium "
                          "cannot be found",
                          dev->ld_dev_path, devi->rsc.id.name,
                          rsc_family2str(medium_id->family), medium_id->name,
                          medium_id->library);

            else if (rc == -EALREADY)
                pho_error(rc,
                          "Device '%s' (S/N '%s') is owned by host %s but "
                          "contains medium (family '%s', name '%s', library "
                          "'%s') which is locked by an other hostname %s",
                          dev->ld_dev_path, devi->rsc.id.name, devi->host,
                          rsc_family2str(medium_id->family), medium_id->name,
                          medium_id->library, medium->lock.hostname);

            lrs_medium_release(medium);
            return rc;
        }
        dev->ld_dss_media_info = medium;

        /* get lock for loaded media */
        if (!dev->ld_dss_media_info->lock.hostname) {
            rc = take_and_update_lock(&sched->sched_thread.dss, DSS_MEDIA,
                                      dev->ld_dss_media_info,
                                      &dev->ld_dss_media_info->lock);
            if (rc) {
                const struct pho_id *med_id = lrs_dev_med_id(dev);

                LOG_RETURN(rc,
                           "Unable to lock the media (family '%s', name '%s', "
                           "library '%s') loaded in an owned device '%s'",
                           rsc_family2str(med_id->family), med_id->name,
                           med_id->library, dev->ld_dev_path);
            }
        }

        /* See if the device is currently mounted */
        rc = get_fs_adapter(dev->ld_dss_media_info->fs.type, &fsa);
        if (rc)
            return rc;

        /* If device is loaded, check if it is mounted as a filesystem */
        rc = ldm_fs_mounted(fsa, dev->ld_dev_path, dev->ld_mnt_path,
                            sizeof(dev->ld_mnt_path));

        if (rc == 0) {
            pho_debug("Discovered mounted filesystem at '%s'",
                      dev->ld_mnt_path);
            dev->ld_op_status = PHO_DEV_OP_ST_MOUNTED;
        } else if (rc == -ENOENT) {
            /* not mounted, not an error */
            rc = 0;
        } else {
            LOG_RETURN(rc, "Cannot determine if device '%s' is mounted",
                       dev->ld_dev_path);
        }
    } else {
        dev->ld_op_status = PHO_DEV_OP_ST_EMPTY;
    }

    pho_debug("Device '%s' is '%s'", lrs_dev_name(dev),
              op_status2str(dev->ld_op_status));

    return rc;
}

/**
 * Load device states into memory.
 * Do nothing if device status is already loaded.
 */
static int sched_load_dev_state(struct lrs_sched *sched)
{
    bool clean_devices = false;
    int rc;
    int i;

    ENTRY;

    if (sched->devices.ldh_devices->len == 0) {
        pho_verb("No device of family '%s' to load",
                 rsc_family2str(sched->family));
        return -ENXIO;
    }

    for (i = 0 ; i < sched->devices.ldh_devices->len; i++) {
        struct lib_handle lib_hdl;
        struct lrs_dev *dev;


        dev = lrs_dev_hdl_get(&sched->devices, i);

        /* get a handle to the library to query it */
        rc = wrap_lib_open(sched->family, dev->ld_dss_dev_info->rsc.id.library,
                           &lib_hdl);
        if (rc) {
            pho_error(rc, "Error while loading devices when opening library");
            continue;
        }

        MUTEX_LOCK(&dev->ld_mutex);
        rc = sched_fill_dev_info(sched, &lib_hdl, dev);
        if (rc) {
            pho_error(rc,
                      "Fail to init device '%s', stopping corresponding device "
                      "thread", dev->ld_dev_path);
            thread_signal_stop_on_error(&dev->ld_device_thread, rc);
        } else {
            clean_devices = true;
        }

        /* close handle to the library */
        rc = ldm_lib_close(&lib_hdl);
        if (rc)
            pho_error(rc,
                      "Error while closing the library handle after loading "
                      "device state");

        MUTEX_UNLOCK(&dev->ld_mutex);
    }


    if (!clean_devices)
        LOG_RETURN(-ENXIO, "No functional device found");

    return 0;
}

/**
 * Unlocks all devices that were locked by a previous instance on this host and
 * that it doesn't own anymore.
 *
 * @param   sched       Scheduler handle.
 * @return              0 on success,
 *                      first encountered negative posix error on failure.
 */
static int sched_clean_device_locks(struct lrs_sched *sched)
{
    struct lock_handle *lock_handle = &sched->lock_handle;
    int rc;

    ENTRY;

    rc = dss_lock_device_clean(&sched->sched_thread.dss,
                               rsc_family_names[sched->family],
                               lock_handle->lock_hostname,
                               lock_handle->lock_owner);
    if (rc)
        pho_error(rc, "Failed to clean device locks");

    return rc;
}

/**
 * Unlocks all media that were locked by a previous instance on this host and
 * that are not loaded anymore in a device locked by this host.
 *
 * @param   sched       Scheduler handle.
 * @return              0 on success,
 *                      first encountered negative posix error on failure.
 */
static int sched_clean_medium_locks(struct lrs_sched *sched)
{
    struct lock_handle *lock_handle = &sched->lock_handle;
    struct media_info *media = NULL;
    int cnt = 0;
    int rc;
    int i;

    ENTRY;

    media = xmalloc(sched->devices.ldh_devices->len * sizeof(*media));

    for (i = 0; i < sched->devices.ldh_devices->len; i++) {
        struct media_info *mda;
        struct lrs_dev *dev;

        dev = lrs_dev_hdl_get(&sched->devices, i);
        if (thread_is_running(&dev->ld_device_thread)) {
            mda = dev->ld_dss_media_info;

            if (mda)
                media[cnt++] = *mda;
        }
    }

    rc = dss_lock_media_clean(&sched->sched_thread.dss, media, cnt,
                              lock_handle->lock_hostname,
                              lock_handle->lock_owner);
    if (rc)
        pho_error(rc, "Failed to clean media locks");

    free(media);
    return rc;
}

int sched_init(struct lrs_sched *sched, enum rsc_family family,
               struct tsqueue *resp_queue)
{
    int rc;

    sched->family = family;

    rc = lrs_cache_setup(sched->family);
    if (rc)
        LOG_RETURN(rc,
                   "failed to initialize media cache for family '%s'",
                   rsc_family2str(sched->family));

    rc = format_media_init(&sched->ongoing_format);
    if (rc)
        LOG_GOTO(err_clean_cache, rc,  "Failed to init sched format media");

    rc = lrs_dev_hdl_init(&sched->devices, family);
    if (rc)
        LOG_GOTO(err_format_media, rc, "Failed to initialize device handle");

    /* Connect to the DSS */
    rc = dss_init(&sched->sched_thread.dss);
    if (rc)
        LOG_GOTO(err_hdl_fini, rc, "Failed to init sched dss handle");

    rc = lock_handle_init(&sched->lock_handle, &sched->sched_thread.dss);
    if (rc)
        LOG_GOTO(err_dss_fini, rc, "Failed to get hostname and PID");

    rc = tsqueue_init(&sched->incoming);
    if (rc)
        LOG_GOTO(err_dss_fini, rc, "Failed to init sched incoming");

    rc = tsqueue_init(&sched->retry_queue);
    if (rc)
        LOG_GOTO(err_incoming_fini, rc, "Failed to init sched retry_queue");

    rc = io_sched_handle_load_from_config(&sched->io_sched_hdl, family);
    if (rc)
        LOG_GOTO(err_retry_queue_fini, rc,
                 "Failed to load I/O schedulers from config");

    sched->response_queue = resp_queue;
    sched->io_sched_hdl.lock_handle = &sched->lock_handle;
    sched->io_sched_hdl.response_queue = sched->response_queue;
    sched->io_sched_hdl.global_device_list = sched->devices.ldh_devices;

    /* Load devices from DSS -- not critical if no device is found */
    lrs_dev_hdl_load(sched, &sched->devices);

    /* Load the device state -- not critical if no device is found */
    sched_load_dev_state(sched);

    rc = sched_clean_device_locks(sched);
    if (rc)
        goto err_sched_fini;

    rc = sched_clean_medium_locks(sched);
    if (rc)
        goto err_sched_fini;

    rc = thread_init(&sched->sched_thread, lrs_sched_thread, sched);
    if (rc)
        LOG_GOTO(err_sched_fini, rc,
                 "Could not create sched thread for family '%d'",
                 sched->family);

    return 0;

err_sched_fini:
    sched_fini(sched);
    return rc;

err_retry_queue_fini:
    tsqueue_destroy(&sched->retry_queue, sched_req_free);
err_incoming_fini:
    tsqueue_destroy(&sched->incoming, sched_req_free);
err_dss_fini:
    dss_fini(&sched->sched_thread.dss);
err_hdl_fini:
    lrs_dev_hdl_fini(&sched->devices);
err_format_media:
    format_media_clean(&sched->ongoing_format);
err_clean_cache:
    lrs_cache_cleanup(family);
    return rc;
}

void prepare_error(struct resp_container *resp_cont, int req_rc,
                   const struct req_container *req_cont)
{
    resp_cont->socket_id = req_cont->socket_id;
    pho_srl_response_error_alloc(resp_cont->resp);

    resp_cont->resp->error->rc = req_rc;

    resp_cont->resp->req_id = req_cont->req->id;
    if (pho_request_is_write(req_cont->req))
        resp_cont->resp->error->req_kind = PHO_REQUEST_KIND__RQ_WRITE;
    else if (pho_request_is_read(req_cont->req))
        resp_cont->resp->error->req_kind = PHO_REQUEST_KIND__RQ_READ;
    else if (pho_request_is_release(req_cont->req))
        resp_cont->resp->error->req_kind = req_cont->req->release->kind;
    else if (pho_request_is_format(req_cont->req))
        resp_cont->resp->error->req_kind = PHO_REQUEST_KIND__RQ_FORMAT;
    else if (pho_request_is_notify(req_cont->req))
        resp_cont->resp->error->req_kind = PHO_REQUEST_KIND__RQ_NOTIFY;
    else if (pho_request_is_monitor(req_cont->req))
        resp_cont->resp->error->req_kind = PHO_REQUEST_KIND__RQ_MONITOR;
    else if (pho_request_is_configure(req_cont->req))
        resp_cont->resp->error->req_kind = PHO_REQUEST_KIND__RQ_CONFIGURE;
}

void queue_error_response(struct tsqueue *response_queue, int req_rc,
                          struct req_container *reqc)
{
    struct resp_container *resp_cont;

    resp_cont = xmalloc(sizeof(*resp_cont));
    resp_cont->resp = xmalloc(sizeof(*resp_cont->resp));

    prepare_error(resp_cont, req_rc, reqc);

    tsqueue_push(response_queue, resp_cont);
}

void sched_resp_free(void *_respc)
{
    struct resp_container *respc = (struct resp_container *)_respc;

    if (!respc)
        return;

    /* XXX: remove condition by correctly initializing response container
     * regardless of their types
     */
    if (pho_response_is_write(respc->resp) ||
        pho_response_is_read(respc->resp))
        free(respc->devices);

    pho_srl_response_free(respc->resp, false);
    free(respc->resp);
}

void sched_resp_free_with_cont(void *_respc)
{
    struct resp_container *respc = (struct resp_container *)_respc;

    sched_resp_free(_respc);
    free(respc);
}

static void sub_request_free_cb(void *sub_request)
{
    sub_request_free(sub_request);
}

void sched_fini(struct lrs_sched *sched)
{
    if (sched == NULL)
        return;

    lrs_dev_hdl_clear(&sched->devices, sched);
    io_sched_fini(&sched->io_sched_hdl);
    lrs_dev_hdl_fini(&sched->devices);
    dss_fini(&sched->sched_thread.dss);
    tsqueue_destroy(&sched->incoming, sched_req_free);
    tsqueue_destroy(&sched->retry_queue, sub_request_free_cb);
    format_media_clean(&sched->ongoing_format);
    lrs_cache_cleanup(sched->family);
}

bool sched_has_running_devices(struct lrs_sched *sched)
{
    int i;

    for (i = 0; i < sched->devices.ldh_devices->len; i++) {
        struct lrs_dev *dev;

        dev = lrs_dev_hdl_get(&sched->devices, i);
        MUTEX_LOCK(&dev->ld_mutex);
        if (dev->ld_ongoing_io || dev->ld_needs_sync || dev->ld_sub_request ||
            dev->ld_sync_params.tosync_array->len ||
            dev->ld_ongoing_scheduled) {
            MUTEX_UNLOCK(&dev->ld_mutex);
            return true;
        }

        MUTEX_UNLOCK(&dev->ld_mutex);
    }

    return false;
}

/**
 * Build a filter string fragment to filter on a given tag array. The returned
 * string is allocated with malloc. NULL is returned when ENOMEM is encountered.
 *
 * The returned string looks like the following:
 * {"$AND": [{"$XJSON": {"DSS:MDA::tags": "tag1"}}]}
 */
static char *build_tag_filter(const struct string_array *tags)
{
    json_t *and_filter = NULL;
    json_t *tag_filters = NULL;
    char   *tag_filter_json = NULL;
    size_t  i;

    /* Build a json array to properly format tag related DSS filter */
    tag_filters = json_array();
    if (!tag_filters)
        return NULL;

    /* Build and append one filter per tag */
    for (i = 0; i < tags->count; i++) {
        json_t *tag_flt;
        json_t *xjson;

        tag_flt = json_object();
        if (!tag_flt)
            GOTO(out, -ENOMEM);

        xjson = json_object();
        if (!xjson) {
            json_decref(tag_flt);
            GOTO(out, -ENOMEM);
        }

        if (json_object_set_new(tag_flt, "DSS::MDA::tags",
                                json_string(tags->strings[i]))) {
            json_decref(tag_flt);
            json_decref(xjson);
            GOTO(out, -ENOMEM);
        }

        if (json_object_set_new(xjson, "$XJSON", tag_flt)) {
            json_decref(xjson);
            GOTO(out, -ENOMEM);
        }

        if (json_array_append_new(tag_filters, xjson))
            GOTO(out, -ENOMEM);
    }

    and_filter = json_object();
    if (!and_filter)
        GOTO(out, -ENOMEM);

    /* Do not use the _new function and decref inconditionnaly later */
    if (json_object_set(and_filter, "$AND", tag_filters))
        GOTO(out, -ENOMEM);

    /* Convert to string for formatting */
    tag_filter_json = json_dumps(and_filter, 0);

out:
    json_decref(tag_filters);

    /* json_decref(NULL) is safe but not documented */
    if (and_filter)
        json_decref(and_filter);

    return tag_filter_json;
}

/**
 * Check if medium is already selected in request
 *
 * @param[in] medium            Medium to check
 * @param[in] reqc              Request to check
 * @param[in] n_med             Number of medium already allocated in this
 *                              request
 * @param[in] not_alloc         Index of one medium which is not to take into
 *                              account into already allocated media
 *                              (ie: index with an error which is currently
 *                              in "retry")
 * @param[out] already_alloc    Set to true if medium is already allocated
 *
 * @return  0 if no error, -EINVAL if a previous medium is not set (neither in
 *          request, nor in alloc device)
 */
static int medium_in_devices(const struct media_info *medium,
                             struct req_container *reqc, size_t n_med,
                             size_t not_alloc, bool *already_alloc)
{
    struct rwalloc_medium *media = reqc->params.rwalloc.media;
    struct lrs_dev **devices = reqc->params.rwalloc.respc->devices;
    size_t i;

    for (i = 0; i < n_med; i++) {
        struct media_info *prev_medium = media[i].alloc_medium;

        if (i == not_alloc)
            continue;

        if (!prev_medium)
            prev_medium = atomic_dev_medium_get(devices[i]);
        else
            /* this reference is not necessary as there is already one in the
             * req_container. This is done to be consistent with the device
             * reference.
             */
            prev_medium = lrs_medium_acquire(&prev_medium->rsc.id);

        /*
         * An allocated medium must be set in the request or already set
         * in the device. If not, we consider that we face an incoherence and
         * we return EINVAL. This incoherence could be temporary, due for
         * example to a device that was concurrently shifting the medium from
         * its subrequest to its inner state.
         */
        if (!prev_medium)
            return -EINVAL;

        if (pho_id_equal(&medium->rsc.id, &prev_medium->rsc.id)) {
            lrs_medium_release(prev_medium);
            *already_alloc = true;
            return 0;
        }
        lrs_medium_release(prev_medium);
    }

    *already_alloc = false;
    return 0;
}

/**
 * Get a suitable medium for a write operation.
 *
 * @param[in]  sched         Current scheduler
 * @param[out] p_media       Selected medium
 * @param[in]  required_size Size of the extent to be written.
 * @param[in]  family        Medium family from which getting the medium
 * @param[in]  tags          Tags used to filter candidate media, the
 *                           selected medium must have all the specified tags.
 * @param[in]  reqc          Current write alloc request container
 * @param[in]  n_med         Nb already allocated media
 * @param[in]  not_alloc     Index to ignore in \p reqc allocated media (can
 *                           be set to n_med or more if every already allocated
 *                           media should be taken into account)
 * @param[out] need_new_grouping Set to true if a grouping is asked and no
 *                               available medium can be found, else, set to
 *                               false
 */
mockable
int sched_select_medium(struct io_scheduler *io_sched,
                        struct media_info **p_media,
                        size_t required_size,
                        enum rsc_family family,
                        const char *library,
                        const char *grouping,
                        const struct string_array *tags,
                        struct req_container *reqc,
                        size_t n_med,
                        size_t not_alloc,
                        bool *need_new_grouping)
{
    struct lock_handle *lock_handle = io_sched->io_sched_hdl->lock_handle;
    bool with_tags = tags != NULL && tags->count > 0;
    struct media_info *split_media_best = NULL;
    struct media_info *whole_media_best = NULL;
    struct media_info *chosen_media = NULL;
    struct media_info *pmedia_res = NULL;
    char *tag_filter_json = NULL;
    struct dss_filter filter;
    size_t avail_size = 0;
    int mcnt = 0;
    int rc;
    int i;

    ENTRY;

    *need_new_grouping = false;

    if (with_tags) {
        tag_filter_json = build_tag_filter(tags);
        if (!tag_filter_json)
            LOG_GOTO(err_nores, rc = -ENOMEM, "while building tags dss filter");
    }

    if (reqc->req->walloc->media[0]->empty_medium)
        rc = dss_filter_build(&filter,
                              "{\"$AND\": ["
                              "  {\"DSS::MDA::family\": \"%s\"},"
                              "  {\"DSS::MDA::put\": \"t\"},"
                              "  {\"DSS::MDA::adm_status\": \"%s\"},"
                              "  {\"DSS::MDA::fs_status\": \"%s\"}"
                              "  %s%s%s"
                              "  %s%s%s"
                              "  %s%s"
                              "]}",
                              rsc_family2str(family),
                              rsc_adm_status2str(PHO_RSC_ADM_ST_UNLOCKED),
                              fs_status2str(PHO_FS_STATUS_EMPTY),
                              library ? ", {\"DSS::MDA::library\": \"" : "",
                              library ? library : "",
                              library ? "\"}" : "",
                              grouping ? ", {\"$XJSON\": "
                                         "{\"DSS::MDA::groupings\": \"" : "",
                              grouping ? grouping : "",
                              grouping ? "\"}}" : "",
                              with_tags ? ", " : "",
                              with_tags ? tag_filter_json : "");
    else
        rc = dss_filter_build(&filter,
                              "{\"$AND\": ["
                              /* Basic criteria */
                              "  {\"DSS::MDA::family\": \"%s\"},"
                              /* Check put media operation flags */
                              "  {\"DSS::MDA::put\": \"t\"},"
                              /* Exclude media locked by admin */
                              "  {\"DSS::MDA::adm_status\": \"%s\"},"
                              "  {\"$OR\": ["
                                   /* Include used media */
                              "    {\"DSS::MDA::fs_status\": \"%s\"},"
                                   /* Include empty media */
                              "    {\"DSS::MDA::fs_status\": \"%s\"}"
                              "  ]}"
                              "  %s%s%s"
                              "  %s%s%s"
                              "  %s%s"
                              "]}",
                              rsc_family2str(family),
                              rsc_adm_status2str(PHO_RSC_ADM_ST_UNLOCKED),
                              /**
                               * @TODO add criteria to limit the maximum number
                               * of data fragments:
                               * vol_free >= required_size/max_fragments with a
                               * configurable max_fragments of 4 for example)
                               */
                              fs_status2str(PHO_FS_STATUS_USED),
                              fs_status2str(PHO_FS_STATUS_EMPTY),
                              library ? ", {\"DSS::MDA::library\": \"" : "",
                              library ? library : "",
                              library ? "\"}" : "",
                              grouping ? ", {\"$XJSON\": "
                                         "{\"DSS::MDA::groupings\": \"" : "",
                              grouping ? grouping : "",
                              grouping ? "\"}}" : "",
                              with_tags ? ", " : "",
                              with_tags ? tag_filter_json : "");

    free(tag_filter_json);
    if (rc)
        return rc;

    rc = dss_media_get(lock_handle->dss, &filter, &pmedia_res,
                       &mcnt, NULL);
    if (mcnt == 0) {
        char *dump = json_dumps(filter.df_json, JSON_COMPACT);

        pho_warn("No medium found matching query: %s", dump);
        dss_filter_free(&filter);
        free(dump);
        if (grouping)
            *need_new_grouping = true;

        GOTO(free_res, rc = -ENOSPC);
    }

    dss_filter_free(&filter);
    if (rc)
        GOTO(err_nores, rc);

    /* get the best fit */
    for (i = 0; i < mcnt; i++) {
        struct media_info *curr = &pmedia_res[i];
        struct lrs_dev *dev = NULL;
        bool already_alloc;
        bool sched_ready;

        /* exclude medium already booked for this allocation */
        rc = medium_in_devices(curr, reqc, n_med, not_alloc, &already_alloc);
        if (rc)
            LOG_GOTO(free_res, rc = -EAGAIN,
                     "Unable to test if medium is already alloc");

        if (already_alloc)
            continue;

        /* exclude medium too small to do a no-split */
        if (reqc->req->walloc->no_split &&
            curr->stats.phys_spc_free <= required_size)
            continue;

        avail_size += curr->stats.phys_spc_free;

        /* already locked */
        if (curr->lock.hostname != NULL)
            if (check_renew_lock(lock_handle, DSS_MEDIA, curr,
                                 &curr->lock))
                /* not locked by myself */
                continue;

        rc = dss_medium_health(lock_handle->dss, &curr->rsc.id, max_health(),
                               &curr->health);
        if (rc)
            continue;

        /* already loaded and in use ? */
        dev = search_in_use_medium(io_sched->io_sched_hdl->global_device_list,
                                   curr->rsc.id.name, curr->rsc.id.library,
                                   &sched_ready);
        if (dev && (!sched_ready ||
                    /* we cannot use a medium that doesn't belong to the write
                     * I/O scheduler.
                     */
                    !(dev->ld_io_request_type & io_sched->type))) {
            pho_debug("Skipping device '%s', already in use",
                      dev->ld_dss_dev_info->rsc.id.name);
            continue;
        }

        if (!reqc->req->walloc->no_split &&
            (split_media_best == NULL ||
             curr->stats.phys_spc_free > split_media_best->stats.phys_spc_free))
                split_media_best = curr;

        if (curr->stats.phys_spc_free < required_size)
            continue;

        if (whole_media_best == NULL ||
            curr->stats.phys_spc_free < whole_media_best->stats.phys_spc_free)
            whole_media_best = curr;
    }

    if (avail_size < required_size) {
        pho_warn("Available space on all media: %zd, required size : %zd",
                 avail_size, required_size);
        if (grouping)
            *need_new_grouping = true;

        GOTO(free_res, rc = -ENOSPC);
    }

    if (whole_media_best != NULL) {
        chosen_media = whole_media_best;
    } else if (split_media_best != NULL) {
        chosen_media = split_media_best;
        pho_info("Split %zd required_size on %zd avail size on medium (family "
                 "'%s', name '%s', library '%s')",
                 required_size, chosen_media->stats.phys_spc_free,
                 rsc_family2str(chosen_media->rsc.id.family),
                 chosen_media->rsc.id.name, chosen_media->rsc.id.library);
    } else {
        pho_debug("No medium available, wait for one");
        if (grouping)
            *need_new_grouping = true;

        GOTO(free_res, rc = -EAGAIN);
    }

    pho_verb("Selected medium (family '%s', name '%s', library '%s'): %zd "
             "bytes free", rsc_family2str(family), chosen_media->rsc.id.name,
             chosen_media->rsc.id.library, chosen_media->stats.phys_spc_free);

    /* Don't rely on existing lock for future use */
    pho_lock_clean(&chosen_media->lock);

    *p_media = lrs_medium_insert(chosen_media);
    if (!*p_media)
        rc = -errno;
    else
        rc = 0;

free_res:
    dss_res_free(pmedia_res, mcnt);

err_nores:
    return rc;
}

/*
 * The intent is to write: exclude media that are administratively
 * locked, full, do not have the put operation flag, do not have the
 * requested tags and do not have the requested grouping.
 */
static bool medium_is_write_compatible(struct media_info *medium,
                                       const char *grouping,
                                       const struct string_array *required_tags,
                                       bool empty_medium)
{
    if (medium->rsc.adm_status != PHO_RSC_ADM_ST_UNLOCKED) {
        pho_debug("Media (family '%s', name '%s', library '%s') is not "
                  "unlocked but '%s'",
                  rsc_family2str(medium->rsc.id.family), medium->rsc.id.name,
                  medium->rsc.id.library,
                  rsc_adm_status2str(medium->rsc.adm_status));
        return false;
    }

    if (empty_medium && medium->fs.status != PHO_FS_STATUS_EMPTY) {
        pho_debug("Media (family '%s', name '%s', library '%s') is not empty",
                  rsc_family2str(medium->rsc.id.family), medium->rsc.id.name,
                  medium->rsc.id.library);
        return false;
    }

    if (medium->fs.status == PHO_FS_STATUS_FULL) {
        pho_debug("Media (family '%s', name '%s', library '%s') is full",
                  rsc_family2str(medium->rsc.id.family), medium->rsc.id.name,
                  medium->rsc.id.library);
        return false;
    }

    if (!medium->flags.put) {
        pho_debug("Media (family '%s', name '%s', library '%s') has a false "
                  "put operation flag",
                  rsc_family2str(medium->rsc.id.family), medium->rsc.id.name,
                  medium->rsc.id.library);
        return false;
    }

    if (required_tags->count > 0 &&
        !string_array_in(&medium->tags, required_tags)) {
        pho_debug("Media (family '%s', name '%s', library '%s') does not match "
                  "required tags",
                  rsc_family2str(medium->rsc.id.family), medium->rsc.id.name,
                  medium->rsc.id.library);
        return false;
    }

    if (grouping && !string_exists(&medium->groupings, grouping)) {
        pho_debug("Media (family '%s', name '%s', library '%s') does not match "
                  "the required grouping %s",
                 rsc_family2str(medium->rsc.id.family), medium->rsc.id.name,
                 medium->rsc.id.library, grouping);
        return false;
    }

    return true;
}

/**
 * Device selection policy prototype.
 * @param[in]     required_size required space to perform the write operation.
 * @param[in]     dev_curr      the current device to consider.
 * @param[in,out] dev_selected  the currently selected device.
 * @retval <0 on error
 * @retval 0 to stop searching for a device
 * @retval >0 to check next devices.
 */
typedef int (*device_select_func_t)(size_t required_size,
                                    struct lrs_dev *dev_curr,
                                    struct lrs_dev **dev_selected);

/**
 * Select a device according to a given status and policy function.
 * Returns a device by setting its ld_ongoing_scheduled flag to true.
 *
 * @param dss     DSS handle.
 * @param op_st   Filter devices by the given operational status.
 *                No filtering is op_st is PHO_DEV_OP_ST_UNSPEC.
 * @param library        If set, selected device must be from library
 * @param select_func    Drive selection function.
 * @param required_size  Required size for the operation.
 * @param media_tags     Mandatory tags for the contained media (for write
 *                       requests only).
 * @param pmedia         Media that should be used by the drive to check
 *                       compatibility (ignored if NULL)
 * @param[in] is_write   Set to true if we want a device to write
 * @param[in] empty_medium   Set to true if we want an empty medium
 * @param[out] one_drive_available  Return true if there is at least one drive
 *                                  that is available to perform an action
 *                                  (ignored if NULL)
 */
struct lrs_dev *dev_picker(GPtrArray *devices,
                           enum dev_op_status op_st,
                           const char *library,
                           const char *grouping,
                           device_select_func_t select_func,
                           size_t required_size,
                           const struct string_array *media_tags,
                           struct media_info *pmedia,
                           bool is_write, bool empty_medium,
                           bool *one_drive_available)
{
    struct lrs_dev *selected = NULL;
    int selected_i = -1;
    int rc;
    int i;

    ENTRY;

    if (one_drive_available)
        *one_drive_available = false;

    for (i = 0; i < devices->len; i++) {
        struct lrs_dev *itr = g_ptr_array_index(devices, i);
        struct lrs_dev *prev = selected;

        MUTEX_LOCK(&itr->ld_mutex);
        if (itr->ld_ongoing_io || itr->ld_needs_sync || itr->ld_sub_request ||
            itr->ld_ongoing_scheduled) {
            pho_debug("Skipping busy device '%s'", itr->ld_dev_path);
            goto unlock_continue;
        }

        if (dev_is_failed(itr)) {
            pho_debug("Skipping device '%s' with status %s", itr->ld_dev_path,
                      op_status2str(itr->ld_op_status));
            goto unlock_continue;
        }

        if (!thread_is_running(&itr->ld_device_thread)) {
            pho_debug("Skipping device '%s' with thread '%s'",
                      itr->ld_dev_path,
                      thread_state2str(&itr->ld_device_thread));
            goto unlock_continue;
        }

        if (one_drive_available)
            *one_drive_available = true;

        if (op_st != PHO_DEV_OP_ST_UNSPEC && itr->ld_op_status != op_st) {
            pho_debug("Skipping device '%s' with incompatible status %s "
                      "instead of %s", itr->ld_dev_path,
                      op_status2str(itr->ld_op_status), op_status2str(op_st));
            goto unlock_continue;
        }

        if (library && strcmp(library, itr->ld_dss_dev_info->rsc.id.library)) {
            pho_debug("Skipping device '%s' with incompatible library '%s' "
                      "instead of '%s'", itr->ld_dev_path,
                      itr->ld_dss_dev_info->rsc.id.library, library);
            goto unlock_continue;
        }

        /* if pmedia is set, we don't want to use the medium currently loaded */
        if (is_write && !pmedia && itr->ld_dss_media_info &&
            !medium_is_write_compatible(itr->ld_dss_media_info, grouping,
                                        media_tags, empty_medium)
            )
            goto unlock_continue;

        /* check tape / drive compat */
        if (pmedia) {
            bool compatible;
            int rc;

            if (is_write &&
                !medium_is_write_compatible(pmedia, grouping, media_tags,
                                            empty_medium))
                goto unlock_continue;

            rc = tape_drive_compat(pmedia, itr, &compatible);
            if (rc) {
                selected = NULL;
                MUTEX_UNLOCK(&itr->ld_mutex);
                break;
            }

            if (!compatible) {
                pho_debug("Skipping incompatible device '%s'",
                          itr->ld_dev_path);
                goto unlock_continue;
            }
        }

        rc = select_func(required_size, itr, &selected);
        if (prev != selected)
            selected_i = i;

        if (rc < 0) {
            pho_debug("Device selection function failed");
            selected = NULL;
            MUTEX_UNLOCK(&itr->ld_mutex);
            break;
        } else if (rc == 0) { /* stop searching */
            MUTEX_UNLOCK(&itr->ld_mutex);
            break;
        }

unlock_continue:
        MUTEX_UNLOCK(&itr->ld_mutex);
    }

    if (selected != NULL)
        pho_debug("Picked dev number %d (%s)",
                  selected_i,
                  selected->ld_dev_path);
    else
        pho_debug("Could not find a suitable %s device", op_status2str(op_st));

    return selected;
}

/**
 * Get the first device with enough space.
 * @retval 0 to stop searching for a device
 * @retval 1 to check next device.
 */
int select_first_fit(size_t required_size,
                     struct lrs_dev *dev_curr,
                     struct lrs_dev **dev_selected)
{
    ENTRY;

    if (dev_curr->ld_dss_media_info == NULL)
        return 1;

    if (dev_curr->ld_dss_media_info->stats.phys_spc_free >= required_size) {
        *dev_selected = dev_curr;
        return 0;
    }
    return 1;
}

/**
 *  Get the device with the lower space to match required_size.
 * @return 1 to check next devices, unless an exact match is found (return 0).
 */
static int select_best_fit(size_t required_size,
                           struct lrs_dev *dev_curr,
                           struct lrs_dev **dev_selected)
{
    ENTRY;

    if (dev_curr->ld_dss_media_info == NULL)
        return 1;

    /* does it fit? */
    if (dev_curr->ld_dss_media_info->stats.phys_spc_free < required_size)
        return 1;

    /* no previous fit, or better fit */
    if (*dev_selected == NULL ||
        (dev_curr->ld_dss_media_info->stats.phys_spc_free <
         (*dev_selected)->ld_dss_media_info->stats.phys_spc_free)) {
        *dev_selected = dev_curr;

        if (required_size == dev_curr->ld_dss_media_info->stats.phys_spc_free)
            /* exact match, stop searching */
            return 0;
    }
    return 1;
}

/**
 * Select empty device first, then loaded, lastly mounted.
 *
 * @return 0 on first empty device found, 1 otherwise (to continue searching).
 */
int select_empty_loaded_mount(size_t required_size,
                              struct lrs_dev *dev_curr,
                              struct lrs_dev **dev_selected)
{
    if (dev_curr->ld_op_status == PHO_DEV_OP_ST_EMPTY) {
        *dev_selected = dev_curr;
        return 0;
    }

    if (*dev_selected == NULL)
        *dev_selected = dev_curr;
    else if ((*dev_selected)->ld_op_status == PHO_DEV_OP_ST_MOUNTED &&
             dev_curr->ld_op_status == PHO_DEV_OP_ST_LOADED)
        *dev_selected = dev_curr;

    return 1;
}

/** return the device policy function depending on configuration */
device_select_func_t get_dev_policy(void)
{
    const char *policy_str;

    ENTRY;

    policy_str = PHO_CFG_GET(cfg_lrs, PHO_CFG_LRS, policy);
    if (policy_str == NULL)
        return NULL;

    if (!strcmp(policy_str, "best_fit"))
        return select_best_fit;

    if (!strcmp(policy_str, "first_fit"))
        return select_first_fit;

    pho_error(-EINVAL,
              "Invalid LRS policy name '%s' "
              "(expected: 'best_fit' or 'first_fit')",
              policy_str);

    return NULL;
}

/**
 * Return true if at least one compatible drive is found.
 *
 * The found compatible drive should be not failed, not locked by
 * administrator and not locked for the current operation.
 *
 * @param[in] sched           Current scheduler
 * @param[in] pmedia          Media that should be used by the drive to check
 *                            compatibility (ignored if NULL, any not failed and
 *                            not administrator locked drive will fit.
 * @param[in] selected_devs   Devices already selected for this operation.
 * @param[in] n_selected_devs Number of devices already selected.
 * @param[in] not_selected    Index to ignore from selected_devs (can be set to
 *                            n_selected_devs or more if all selected_devs
 *                            should be taken into account)
 * @param[in] prevent_duplicate Check the allocation requester of the medium.
 * @param[in] allocation_seeker Allocation requester.
 * @return                    True if one compatible drive is found, else false.
 */
static bool compatible_drive_exists(struct lrs_sched *sched,
                                    struct media_info *pmedia,
                                    struct lrs_dev **selected_devs,
                                    size_t n_selected_devs,
                                    size_t not_selected,
                                    bool prevent_duplicate,
                                    int allocation_requester)
{
    int i, j;

    for (i = 0; i < sched->devices.ldh_devices->len; i++) {
        struct lrs_dev *dev = lrs_dev_hdl_get(&sched->devices, i);
        bool is_already_selected = false;

        if (dev_is_failed(dev) ||
            !thread_is_running(&dev->ld_device_thread))
            continue;

        if (prevent_duplicate && dev->ld_sub_request != NULL &&
            dev->ld_sub_request->reqc->socket_id == allocation_requester)
            continue;

        /* check the device is not already selected */
        for (j = 0; j < n_selected_devs; ++j) {
            if (j == not_selected)
                continue;

            if (pho_id_equal(&dev->ld_dss_dev_info->rsc.id,
                             &selected_devs[j]->ld_dss_dev_info->rsc.id)) {
                is_already_selected = true;
                break;
            }
        }

        if (is_already_selected)
            continue;

        if (sched->family == PHO_RSC_TAPE && pmedia) {
            bool is_compat;

            /* DIR or RADOS resource */
            if (pmedia->rsc.id.family == PHO_RSC_DIR ||
                pmedia->rsc.id.family == PHO_RSC_RADOS_POOL) {
                if (pho_id_equal(&dev->ld_dss_dev_info->rsc.id,
                                 &pmedia->rsc.id))
                    continue;

                return true;
            }

            /* last existing resource type : tape */
            if (tape_drive_compat(pmedia, dev, &is_compat))
                continue;

            if (is_compat)
                return true;
        } else {
            return true;
        }
    }

    return false;
}

/******************************************************************************/
/* Request/response manipulation **********************************************/
/******************************************************************************/

static int sched_device_add(struct lrs_sched *sched, enum rsc_family family,
                            const char *name, const char *library)
{
    struct lrs_dev *device = NULL;
    struct lib_handle lib_hdl;
    int rc = 0;

    rc = lrs_dev_hdl_add(sched, &sched->devices, name, library);
    if (rc)
        return rc;

    device = lrs_dev_hdl_get(&sched->devices,
                             sched->devices.ldh_devices->len - 1);

    /* get a handle to the library to query it */
    rc = wrap_lib_open(family, library, &lib_hdl);
    if (rc)
        goto dev_del;

    MUTEX_LOCK(&device->ld_mutex);
    rc = sched_fill_dev_info(sched, &lib_hdl, device);
    MUTEX_UNLOCK(&device->ld_mutex);
    ldm_lib_close(&lib_hdl);
    if (rc)
        goto dev_del;

    return 0;

dev_del:
    lrs_dev_hdl_del(&sched->devices, sched->devices.ldh_devices->len - 1, rc,
                    sched);

    return rc;
}

/**
 * Retry to remove the locked device from the local device array.
 *
 * If the device cannont be removed, will try again later by returning -EAGAIN.
 */
static int sched_device_retry_lock(struct lrs_sched *sched, const char *name,
                                   const char *library, struct lrs_dev *dev_ptr)
{
    int rc;

    rc = lrs_dev_hdl_retrydel(&sched->devices, dev_ptr);
    if (rc)
        return rc;

    io_sched_remove_device(&sched->io_sched_hdl, dev_ptr);

    pho_verb("Removed locked device (name '%s', library '%s') from the local "
             "memory", name, library);

    return 0;
}

/**
 * Try to remove the locked device from the local device array.
 * It will be inserted back once the device status is changed to 'unlocked'.
 *
 * If the device cannot be removed, because operations are still ongoing,
 * will try again later, by returning -EAGAIN.
 */
static int sched_device_lock(struct lrs_sched *sched, const char *name,
                             const char *library, struct lrs_dev **dev_ptr)
{
    struct lrs_dev *dev;
    int rc;
    int i;

    for (i = 0; i < sched->devices.ldh_devices->len; ++i) {
        dev = lrs_dev_hdl_get(&sched->devices, i);

        if (!strcmp(name, dev->ld_dss_dev_info->rsc.id.name) &&
            !strcmp(library, dev->ld_dss_dev_info->rsc.id.library)) {
            rc = lrs_dev_hdl_trydel(&sched->devices, i);
            if (rc == -EAGAIN) {
                *dev_ptr = dev;
                return rc;
            }
            io_sched_remove_device(&sched->io_sched_hdl, dev);

            if (!rc)
                pho_verb("Removed locked device (name '%s', library '%s') from "
                         "the local memory", name, library);

            return rc;
        }
    }

    pho_verb("Cannot find local device info for '%s', not critical, "
             "will continue", name);

    return 0;
}

/**
 * Update local admin status of device to 'unlocked',
 * or fetch it from the database if unknown
 */
static int sched_device_unlock(struct lrs_sched *sched, const char *name,
                               const char *library)
{
    struct lrs_dev *dev;
    int i;

    for (i = 0; i < sched->devices.ldh_devices->len; ++i) {
        dev = lrs_dev_hdl_get(&sched->devices, i);

        if (!strcmp(name, dev->ld_dss_dev_info->rsc.id.name) &&
            !strcmp(library, dev->ld_dss_dev_info->rsc.id.library)) {
            pho_verb("Updating device '%s' state to unlocked", name);
            dev->ld_dss_dev_info->rsc.adm_status = PHO_RSC_ADM_ST_UNLOCKED;
            return 0;
        }
    }

    pho_verb("Cannot find local device info for (name '%s', library '%s', will "
             "fetch it from the database", name, library);

    return sched_device_add(sched, sched->family, name, library);
}

/** remove written_size from phys_spc_free in media_info and DSS */
static void push_sub_request_to_device(struct req_container *reqc)
{
    struct lrs_dev **devices = reqc->params.rwalloc.respc->devices;
    size_t devices_len = reqc->params.rwalloc.respc->devices_len;
    struct sub_request **sub_requests;
    size_t i;

    sub_requests = xmalloc(sizeof(*sub_requests) * devices_len);

    for (i = 0; i < devices_len; i++) {
        sub_requests[i] = xmalloc(sizeof(*sub_requests[i]));

        sub_requests[i]->medium_index = i;
        sub_requests[i]->reqc = reqc;
        sub_requests[i]->failure_on_medium = false;
    }

    for (i = 0; i < devices_len; i++) {
        devices[i]->ld_sub_request = sub_requests[i];
        devices[i]->ld_ongoing_scheduled = false;

        thread_signal(&devices[i]->ld_device_thread);
    }

    free(sub_requests);
}

static int publish_or_cancel(struct lrs_sched *sched,
                             struct req_container *reqc, int reqc_rc,
                             size_t n_selected)
{
    int rc = 0;
    size_t i;

    if (pho_request_is_read(reqc->req)) {
        if (reqc_rc == -EAGAIN)
            rml_requeue(&reqc->params.rwalloc.media_list);
        else if (reqc_rc)
            rml_reset(&reqc->params.rwalloc.media_list);
    }

    if (reqc_rc == -EAGAIN && !running)
        return reqc_rc;

    if (reqc_rc != -EAGAIN) {
        rc = io_sched_remove_request(&sched->io_sched_hdl, reqc);
        if (rc)
            pho_error(rc, "Failed to remove request '%p' (%s)", reqc,
                      pho_srl_request_kind_str(reqc->req));
    }

    if (!reqc_rc && !rc)
        push_sub_request_to_device(reqc);

    if (reqc_rc || rc) {
        for (i = 0; i < n_selected; i++)
            reqc->params.rwalloc.respc->devices[i]->ld_ongoing_scheduled =
                                                                        false;

        if (reqc_rc != -EAGAIN || rc) {
            queue_error_response(sched->response_queue,
                                 reqc_rc != -EAGAIN ? reqc_rc : rc,
                                 reqc);
            sched_req_free(reqc);
        }
    }

    return reqc_rc == -EAGAIN ? reqc_rc : rc;
}

/**
 * In the scheduler's context, the device thread may unload the medium while we
 * are checking if it is loaded. To avoid race conditions, we need to have a
 * local copy and take a reference to it to make sure that the medium is not
 * freed concurrently. The reference must be taken with a lock held on the
 * device.
 */
static bool medium_is_loaded(struct lrs_dev *dev, struct media_info *medium)
{
    struct media_info *loaded_medium = NULL;
    bool loaded;

    if (!dev->ld_dss_media_info)
        return false;

    MUTEX_LOCK(&dev->ld_mutex);
    if (dev->ld_dss_media_info)
        loaded_medium = lrs_medium_acquire(&dev->ld_dss_media_info->rsc.id);
    MUTEX_UNLOCK(&dev->ld_mutex);

    loaded = loaded_medium && (dev_is_loaded(dev) || dev_is_mounted(dev)) &&
             pho_id_equal(&loaded_medium->rsc.id, &medium->rsc.id);

    if (loaded_medium)
        lrs_medium_release(loaded_medium);

    return loaded;
}

static int sched_write_alloc_one_medium(struct lrs_sched *sched,
                                        struct allocation *alloc,
                                        size_t index_to_alloc,
                                        bool handle_error)
{
    struct req_container *reqc = alloc->is_sub_request ?
        alloc->u.sub_req->reqc :
        alloc->u.req.reqc;
    struct media_info **alloc_medium =
        &reqc->params.rwalloc.media[index_to_alloc].alloc_medium;
    pho_req_write_t *wreq = reqc->req->walloc;
    struct lrs_dev *dev;
    int rc;

lock_race_retry:
    if (!alloc->is_sub_request)
        rc = io_sched_get_device_medium_pair(&sched->io_sched_hdl, reqc, &dev,
                                             &index_to_alloc);
    else
        rc = io_sched_retry(&sched->io_sched_hdl, alloc->u.sub_req, &dev);

    if (rc)
        GOTO(notify_error, rc);

    /* the write scheduler either chooses a device with a medium or chooses a
     * medium then a device. If the device is not NULL, it is ready for
     * allocation with the current medium.
     */
    if (dev && !*alloc_medium) {
        *alloc_medium = dev->ld_dss_media_info;
        lrs_medium_acquire(&(*alloc_medium)->rsc.id);
        /* a device containing a medium with enough space was found */
        goto select_device;
    } else if (dev && medium_is_loaded(dev, *alloc_medium)) {
        if (dev_is_sched_ready(dev)) {
            goto select_device;
        } else {
            pho_debug("Selected medium for write is already loaded in a busy "
                      "drive");
            rc = -EAGAIN;
            goto notify_error;
        }
    } else if (dev) {
        /* a new medium needs to be loaded in \p dev : lock it */
        rc = ensure_medium_lock(&sched->lock_handle, *alloc_medium);

        if (rc) {
            pho_debug("failed to lock media (family '%s', name '%s', library "
                      "'%s') for write, looking for another one",
                      rsc_family2str((*alloc_medium)->rsc.id.family),
                      (*alloc_medium)->rsc.id.name,
                      (*alloc_medium)->rsc.id.library);
            *alloc_medium = NULL;
            dev = NULL;
            goto lock_race_retry;
        }

        goto select_device;
    }

    if (compatible_drive_exists(sched, *alloc_medium,
                                reqc->params.rwalloc.respc->devices,
                                handle_error ? wreq->n_media : index_to_alloc,
                                index_to_alloc,
                                reqc->req->walloc->prevent_duplicate,
                                reqc->socket_id))
        rc = -EAGAIN;
    else
        pho_error(rc = -ENODEV, "No compatible device found for write alloc");

notify_error:
    lrs_medium_release(*alloc_medium);
    *alloc_medium = NULL;

    return rc;

select_device:
    dev->ld_ongoing_scheduled = true;
    reqc->params.rwalloc.respc->devices[index_to_alloc] = dev;

    return 0;
}

static void sched_write_alloc_add_threshold(struct lrs_sched *sched,
                                            struct resp_container *respc)
{
    pho_resp_write_t *wresp = respc->resp->walloc;

    wresp->threshold = xmalloc(sizeof(*wresp->threshold));
    pho_sync_threshold__init(wresp->threshold);

    wresp->threshold->sync_nb_req    = sched->devices.sync_nb_req;
    wresp->threshold->sync_wsize_kb  = sched->devices.sync_wsize_kb;
    wresp->threshold->sync_time_sec  = sched->devices.sync_time_ms.tv_sec;
    wresp->threshold->sync_time_nsec = sched->devices.sync_time_ms.tv_nsec;
}

/**
 * Handle a write allocation request by finding appropriate medium/device
 * couples to write.
 *
 * The request is pushed to the selected device threads.
 *
 * @return  0 on success, -EAGAIN if the request should be rescheduled later,
 *          a negative error code if a failure occurs in scheduler thread.
 */
static int sched_handle_write_alloc(struct lrs_sched *sched,
                                    struct req_container *reqc)
{
    pho_req_write_t *wreq = reqc->req->walloc;
    size_t next_medium_index = 0;
    int rc = 0;

    pho_debug("write: allocation request (%ld medias)", wreq->n_media);

    for (next_medium_index = 0; next_medium_index < wreq->n_media;
         next_medium_index++) {
        struct allocation alloc = {
            .is_sub_request = false,
            .u = {
                .req = {
                    .reqc = reqc,
                    .index = next_medium_index,
                },
            },
        };
        rc = sched_write_alloc_one_medium(sched, &alloc, next_medium_index,
                                          false);
        if (rc)
            break;
    }

    sched_write_alloc_add_threshold(sched, reqc->params.rwalloc.respc);
    return publish_or_cancel(sched, reqc, rc, next_medium_index);
}

static int skip_read_alloc_medium(int rc, struct req_container *reqc,
                                  size_t index_to_alloc)
{
    struct read_media_list *list;
    size_t n_required;
    size_t available;

    n_required = reqc->req->ralloc->n_required;
    list = &reqc->params.rwalloc.media_list;

    available = rml_medium_update(list, index_to_alloc, rml_errno2status(rc));
    if (available < n_required) {
        if (list->rml_size - list->rml_errors >= n_required)
            /* We can retry later with the media unavailable now */
            return -EAGAIN;
        else
            /* Not enough media remaining to allocate the request later  */
            return rc;
    }

    return 0;
}

static int _check_medium_status(struct req_container *reqc,
                                struct media_info *medium)
{
    if (medium->fs.status == PHO_FS_STATUS_BLANK)
        LOG_RETURN(-EINVAL,
                   "Cannot do I/O on unformatted medium (family '%s', name "
                   "'%s', library '%s')",
                   rsc_family2str(medium->rsc.id.family),
                   medium->rsc.id.name, medium->rsc.id.library);

    if (medium->rsc.adm_status != PHO_RSC_ADM_ST_UNLOCKED)
        LOG_RETURN(-EPERM,
                   "Cannot read on medium (family '%s', name '%s', library "
                   "'%s') with adm_status '%s'",
                   rsc_family2str(medium->rsc.id.family),
                   medium->rsc.id.name, medium->rsc.id.library,
                   rsc_adm_status2str(medium->rsc.adm_status));

    return 0;
}

static int _check_medium_on_read_alloc(struct req_container *reqc,
                                       struct media_info *medium)
{
    if (!medium->flags.get)
        LOG_RETURN(-EPERM, "medium (family '%s', name '%s', library '%s') "
                   "get flag is false",
                   rsc_family2str(medium->rsc.id.family),
                   medium->rsc.id.name, medium->rsc.id.library);

    return 0;
}

static int _check_medium_on_delete_alloc(struct req_container *reqc,
                                         struct media_info *medium)
{
    if (!medium->flags.delete)
        LOG_RETURN(-EPERM, "medium (family '%s', name '%s', library '%s') "
                   "delete flag is false",
                   rsc_family2str(medium->rsc.id.family),
                   medium->rsc.id.name, medium->rsc.id.library);

    return 0;
}

static int check_medium_permission_and_status(struct req_container *reqc,
                                              struct media_info *medium)
{

    if (medium->fs.status == PHO_FS_STATUS_IMPORTING &&
        !pho_request_is_read(reqc->req))
        LOG_RETURN(-EINVAL,
                   "Medium (family '%s', name '%s', library '%s') is being "
                   "imported. Can only read from it.",
                   rsc_family2str(medium->rsc.id.family),
                   medium->rsc.id.name, medium->rsc.id.library);

    if (medium->fs.status != PHO_FS_STATUS_IMPORTING &&
        pho_request_is_read(reqc->req)) {
        int rc;

        if ((int)reqc->req->ralloc->operation ==
            PHO_READ_TARGET_ALLOC_OP_READ) {
            rc = _check_medium_on_read_alloc(reqc, medium);
            if (rc)
                return rc;
        } else if ((int)reqc->req->ralloc->operation ==
            PHO_READ_TARGET_ALLOC_OP_DELETE) {
            rc = _check_medium_on_delete_alloc(reqc, medium);
            if (rc)
                return rc;
        }

        rc = _check_medium_status(reqc, medium);
        if (rc)
            return rc;
    } else if (pho_request_is_format(reqc->req) &&
               (medium->rsc.id.family != PHO_RSC_TAPE ||
                !reqc->req->format->force)) {
        if (medium->fs.status != PHO_FS_STATUS_BLANK)
            LOG_RETURN(-EINVAL,
                       "Medium (family '%s', name '%s', library '%s') has a "
                       "fs.status '%s', expected PHO_FS_STATUS_BLANK to be "
                       "formatted.",
                       rsc_family2str(medium->rsc.id.family),
                       medium->rsc.id.name, medium->rsc.id.library,
                       fs_status2str(medium->fs.status));
    }

    return 0;
}

mockable
int fetch_and_check_medium_info(struct lock_handle *lock_handle,
                                struct req_container *reqc,
                                struct pho_id *m_id,
                                size_t index,
                                struct media_info **target_medium)
{
    struct media_info *medium;
    struct pho_id sm_id;
    int rc;

    if (!m_id)
        m_id = &sm_id;

    reqc_pho_id_from_index(reqc, index, m_id);
    rc = sched_fill_media_info(lock_handle, &medium, m_id);
    if (rc)
        return rc;

    rc = check_medium_permission_and_status(reqc, medium);
    *target_medium = medium;
    if (rc)
        return rc;

    /* don't rely on existing lock for future use of this medium */
    pho_lock_clean(&(*target_medium)->lock);
    return 0;
}

enum allocation_status {
    AS_ALLOCATED,
    AS_RETRY,
};

/*
 * \return   < 0 an error occured for this allocation
 *           AS_ALLOCATED: the medium can be allocated to the device
 *           AS_RETRY:     the caller should try to allocate another medium
 *
 */
static int sched_try_alloc_medium(struct lrs_sched *sched,
                                  struct lrs_dev *dev,
                                  struct req_container *reqc,
                                  size_t n_already_allocated,
                                  size_t index_to_alloc,
                                  int alloc_rc)
{
    struct media_info **medium_to_alloc;
    int rc;

    if (alloc_rc)
        GOTO(skip_medium, rc = alloc_rc);

    medium_to_alloc = reqc_get_medium_to_alloc(reqc, index_to_alloc);
    if (!dev) {
        /* an I/O scheduler may not set *medium_to_alloc if it doesn't find a
         * suitable medium. Return EAGAIN in this case.
         */
        if (!*medium_to_alloc ||
            compatible_drive_exists(sched, *medium_to_alloc,
                                    reqc->params.rwalloc.respc->devices,
                                    n_already_allocated, index_to_alloc,
                                    false, reqc->socket_id))
            rc = -EAGAIN;
        else
            rc = -ENODEV;

        goto skip_medium;
    } else if (!dev_is_sched_ready(dev)) {
        rc = -EAGAIN;
        goto skip_medium;
    }

    /* lock medium */
    rc = ensure_medium_lock(&sched->lock_handle, *medium_to_alloc);
    if (rc)
        goto release_skip_medium;

    dev->ld_ongoing_scheduled = true;
    reqc->params.rwalloc.respc->devices[index_to_alloc] = dev;
    return AS_ALLOCATED;

release_skip_medium:
    lrs_medium_release(*medium_to_alloc);
    *medium_to_alloc = NULL;
skip_medium:
    if (dev)
        dev->ld_ongoing_scheduled = false;

    rc = skip_read_alloc_medium(rc, reqc, index_to_alloc);
    if (rc)
        return rc;

    return AS_RETRY;
}

static int sched_handle_read_alloc_error(struct lrs_sched *sched,
                                         struct sub_request *sreq)
{
    size_t index_of_failed = sreq->medium_index;
    struct read_media_list *list;
    struct lrs_dev *dev = NULL;
    size_t health_of_failed;
    int alloc_status;
    int rc;

    ENTRY;

    health_of_failed =
        (*reqc_get_medium_to_alloc(sreq->reqc, index_of_failed))->health;
    list = &sreq->reqc->params.rwalloc.media_list;
    rml_reset(list);

retry:
    rc = io_sched_retry(&sched->io_sched_hdl, sreq, &dev);
    pho_debug("I/O sched pair: rc=%d, index=%lu, dev=%s",
              rc, sreq->medium_index,
              dev ? dev->ld_dss_dev_info->rsc.id.name : "none");
    if (rc) {
        rml_medium_realloc_failed(list, sreq->medium_index, index_of_failed);
        sreq->medium_index = index_of_failed;
        GOTO(try_alloc, rc);
    }

    if (dev) {
        if (sreq->failure_on_medium && health_of_failed == 0)
            /* Move the failed medium (index_of_failed) in the error section.
             * Swap it with the medium selected by the I/O scheduler
             * (medium_index).
             */
            rml_medium_realloc_failed(list, sreq->medium_index,
                                      index_of_failed);
        else
            rml_medium_realloc(list, sreq->medium_index, index_of_failed);

        /* The newly allocated medium has been moved to index_of_failed to
         * replace the previous one. Update medium_index accordingly.
         */
        sreq->medium_index = index_of_failed;
    }

try_alloc:
    alloc_status = sched_try_alloc_medium(sched, dev, sreq->reqc,
                                          sreq->reqc->req->ralloc->n_required,
                                          sreq->medium_index,
                                          rc);
    switch (alloc_status) {
    case AS_ALLOCATED:
        return 0;
    case AS_RETRY:
        goto retry;
    default:
        assert(alloc_status < 0);
        return alloc_status;
    }
}

/**
 * Alloc one more medium to a device in a read alloc request
 */
static int sched_read_alloc_one_medium(struct lrs_sched *sched,
                                       struct req_container *reqc,
                                       size_t index_to_alloc,
                                       size_t num_allocated)
{
    struct read_media_list *list;
    struct lrs_dev *dev = NULL;
    int alloc_status;
    int rc = 0;

    ENTRY;

    list = &reqc->params.rwalloc.media_list;

find_read_device:
    rc = io_sched_get_device_medium_pair(&sched->io_sched_hdl, reqc, &dev,
                                         &index_to_alloc);
    pho_debug("I/O sched pair: rc=%d, index=%lu, dev=%s",
              rc, index_to_alloc,
              dev ? dev->ld_dss_dev_info->rsc.id.name : "none");
    if (rc)
        GOTO(try_alloc, rc);

    assert(index_to_alloc >= num_allocated);
    if (dev) {
        rml_medium_update(list, index_to_alloc, RMAS_OK);
        index_to_alloc = num_allocated;
    }

try_alloc:
    alloc_status = sched_try_alloc_medium(sched, dev, reqc, num_allocated,
                                          index_to_alloc, rc);
    switch (alloc_status) {
    case AS_ALLOCATED:
        return 0;
    case AS_RETRY:
        goto find_read_device;
    default:
        assert(alloc_status < 0);
        return alloc_status;
    }
    __builtin_unreachable();
}

/**
 * Handle a read allocation request by finding the specified media and choose
 * the right device to read them
 */
static int sched_handle_read_alloc(struct lrs_sched *sched,
                                   struct req_container *reqc)
{
    int rc = 0;
    size_t i;

    pho_debug("read: allocation request (%ld medias)",
              reqc->req->ralloc->n_med_ids);

    for (i = 0; i < reqc->req->ralloc->n_required; i++) {
        rc = sched_read_alloc_one_medium(sched, reqc, i, i);
        if (rc)
            break;
    }

    return publish_or_cancel(sched, reqc, rc, i);
}

/**
 * Count the number of available and compatible devices for a specific medium.
 *
 * @param[in]   sched       Scheduler handle
 * @param[in]   medium      Medium we want to check
 *
 * @return                  Number of available and compatible devices
 */
static int count_suitable_devices(struct lrs_sched *sched,
                                  struct media_info *medium)
{
    int count = 0;
    int i;

    for (i = 0; i < sched->devices.ldh_devices->len; i++) {
        struct lrs_dev *iter = lrs_dev_hdl_get(&sched->devices, i);
        bool is_compatible;

        if (dev_is_failed(iter))
            continue;

        if (!thread_is_running(&iter->ld_device_thread))
            continue;

        if (tape_drive_compat(medium, iter, &is_compatible))
            continue;

        if (is_compatible)
            count++;
    }

    return count;
}

/**
 * Handle a format request
 *
 * reqc is freed, except when -EAGAIN is returned.
 */
static int sched_handle_format(struct lrs_sched *sched,
                               struct req_container *reqc)
{
    pho_req_format_t *freq = reqc->req->format;
    struct sub_request *format_sub_request;
    struct lrs_dev *device = NULL;
    struct pho_id m;
    int rc = 0;

    rc = fetch_and_check_medium_info(&sched->lock_handle, reqc, &m, 0,
                                     reqc_get_medium_to_alloc(reqc, 0));
    if (rc == -EALREADY)
        LOG_GOTO(err_out, rc = -EBUSY,
                 "Medium to format (family '%s', name '%s', library '%s') is "
                 "already locked",
                 rsc_family2str(reqc->req->format->med_id->family),
                 reqc->req->format->med_id->name,
                 reqc->req->format->med_id->library);
    if (rc)
        goto err_out;

    rc = get_fs_adapter((enum fs_type)freq->fs, &reqc->params.format.fsa);
    if (rc)
        LOG_GOTO(err_out, rc, "Invalid fs_type (%d)", freq->fs);

    if (!format_medium_add(&sched->ongoing_format,
                           reqc->params.format.medium_to_format))
        LOG_GOTO(err_out, rc = -EINVAL,
                 "trying to format the medium (family '%s', name '%s', library "
                 "'%s', while it is already being formatted",
                 rsc_family2str(m.family), m.name, m.library);

    rc = io_sched_get_device_medium_pair(&sched->io_sched_hdl, reqc, &device,
                                         NULL);
    if (rc)
        GOTO(err_out, rc);

    if (!device) {
        int suitable_devices;

        suitable_devices = count_suitable_devices(sched,
              reqc->params.format.medium_to_format);
        if (!suitable_devices)
            LOG_GOTO(remove_format_err_out, rc = -ENODEV,
                     "No device can format medium (family '%s', name '%s', "
                     "library '%s'), will abort request",
                     rsc_family2str(m.family), m.name, m.library);

        pho_verb("No device available to format medium (family '%s', name "
                 "'%s', library '%s'), will try again later",
                 rsc_family2str(m.family), m.name, m.library);
        format_medium_remove(&sched->ongoing_format,
                             reqc->params.format.medium_to_format);

        return -EAGAIN;
    } else if (!dev_is_sched_ready(device)) {
        LOG_GOTO(remove_format_err_out, rc = -EINVAL,
                 "medium (family '%s', name '%s', library '%s') is already "
                 "loaded into a busy device (family '%s', name '%s', library "
                 "'%s'), unexpected state, will abort request",
                 rsc_family2str(m.family), m.name, m.library,
                 rsc_family2str(device->ld_dss_dev_info->rsc.id.family),
                 device->ld_dss_dev_info->rsc.id.name,
                 device->ld_dss_dev_info->rsc.id.library);
    }

    /* lock medium */
    rc = ensure_medium_lock(&sched->lock_handle,
                            reqc->params.format.medium_to_format);
    if (rc == -EEXIST || rc == -EALREADY)
        rc = -EBUSY;

    if (rc)
        LOG_GOTO(remove_format_err_out, rc,
                 "Unable to lock the medium (family '%s', name '%s', library "
                 "'%s') to format it",
                 rsc_family2str(m.family), m.name, m.library);
    /*
     * dev_picker set the ld_ongoing_scheduled flag to true when a device
     * is selected. This prevents selecting the same device again for an
     * other medium of a request that needs several media.
     * For a format request, only one device is needed. We can directly
     * try to push the subrequest to the selected device and clear this
     * flag.
     */
    device->ld_ongoing_scheduled = false;

    format_sub_request = xmalloc(sizeof(*format_sub_request));

    format_sub_request->medium_index = 0;
    format_sub_request->reqc = reqc;
    rc = io_sched_remove_request(&sched->io_sched_hdl, reqc);
    if (rc)
        LOG_GOTO(free_sub_request, rc,
                 "Failed to remove request from I/O scheduler");

    MUTEX_LOCK(&device->ld_mutex);
    device->ld_sub_request = format_sub_request;
    MUTEX_UNLOCK(&device->ld_mutex);
    thread_signal(&device->ld_device_thread);

    return 0;

free_sub_request:
    free(format_sub_request);
remove_format_err_out:
    format_medium_remove(&sched->ongoing_format,
                         reqc->params.format.medium_to_format);

err_out:
    if (rc != -EAGAIN) {
        pho_error(rc, "format: failed to schedule format for medium (family "
                  "'%s', name '%s', library '%s')",
                  rsc_family2str(m.family), m.name, m.library);
        queue_error_response(sched->response_queue, rc, reqc);

        rc = io_sched_remove_request(&sched->io_sched_hdl, reqc);
        sched_req_free(reqc);
    }

    return rc;
}

/**
 * Enqueue the notify response in the response queue.
 *
 * This queue will then be processed by the communication thread, which send
 * it back to the requester.
 *
 * @param[in]   sched       Scheduler handle
 * @param[in]   reqc        Request container
 *
 * @return                  0 if success,
 *                         -errno if failure
 */
static void queue_notify_response(struct lrs_sched *sched,
                                  struct req_container *reqc)
{
    pho_req_notify_t *nreq = reqc->req->notify;
    struct resp_container *respc;
    pho_resp_t *resp;

    respc = xmalloc(sizeof(*respc));

    respc->socket_id = reqc->socket_id;
    respc->resp = xmalloc(sizeof(*respc->resp));

    pho_srl_response_notify_alloc(respc->resp);

    resp = respc->resp;

    resp->req_id = reqc->req->id;
    resp->notify->rsrc_id->family = nreq->rsrc_id->family;
    resp->notify->rsrc_id->name = xstrdup(nreq->rsrc_id->name);
    resp->notify->rsrc_id->library = xstrdup(nreq->rsrc_id->library);

    tsqueue_push(sched->response_queue, respc);
}

static int sched_medium_update(struct lrs_sched *sched,
                               const char *name, const char *library)
{
    struct media_info *medium = NULL;
    struct media_info *old_medium;
    struct lrs_dev *device;
    struct pho_id id;
    int rc;

    id.family = sched->family;
    pho_id_name_set(&id, name, library);

    medium = lrs_medium_update(&id);
    if (!medium)
        return -errno;

    device = search_loaded_medium_keep_lock(sched->devices.ldh_devices, name,
                                            library);
    if (!device)
        GOTO(release, rc = 0);

    /* make this change atomic to avoid race conditions with other threads that
     * might want to update the pointer as well.
     */
    old_medium = atomic_exchange(&device->ld_dss_media_info, medium);

    MUTEX_UNLOCK(&device->ld_mutex);

    lrs_medium_release(old_medium);
    return 0;

release:
    lrs_medium_release(medium);

    return rc;
}

/* reqc is freed unless -EAGAIN is returned */
static int sched_handle_notify(struct lrs_sched *sched,
                               struct req_container *reqc)
{
    pho_req_notify_t *nreq = reqc->req->notify;
    struct lrs_dev *dev;
    int rc = 0;

    pho_debug("Notify: device (name '%s', library '%s')",
              nreq->rsrc_id->name, nreq->rsrc_id->library);

    switch (nreq->op) {
    case PHO_NTFY_OP_DEVICE_ADD:
        rc = sched_device_add(sched, (enum rsc_family)nreq->rsrc_id->family,
                              nreq->rsrc_id->name, nreq->rsrc_id->library);
        break;
    case PHO_NTFY_OP_DEVICE_LOCK:
        dev = reqc->params.notify.notified_device;

        if (dev == NULL) {
            rc = sched_device_lock(sched, nreq->rsrc_id->name,
                                   nreq->rsrc_id->library, &dev);
            if (rc == -EAGAIN)
                reqc->params.notify.notified_device = dev;
        } else {
            rc = sched_device_retry_lock(sched, nreq->rsrc_id->name,
                                         nreq->rsrc_id->library, dev);
        }
        break;
    case PHO_NTFY_OP_DEVICE_UNLOCK:
        rc = sched_device_unlock(sched, nreq->rsrc_id->name,
                                 nreq->rsrc_id->library);
        break;
    case PHO_NTFY_OP_MEDIUM_UPDATE:
        rc = sched_medium_update(sched, nreq->rsrc_id->name,
                                 nreq->rsrc_id->library);
        break;
    default:
        LOG_GOTO(err, rc = -EINVAL, "The requested operation is not "
                 "recognized");
    }

    if (!nreq->wait) {
        if (rc != 0 && rc != -EAGAIN) {
            pho_error(rc, "Notify failed for (name '%s', library '%s')",
                      nreq->rsrc_id->name, nreq->rsrc_id->library);
            rc = 0;
        }

        return rc;
    }

    if (rc)
        goto err;

    queue_notify_response(sched, reqc);

    sched_req_free(reqc);
    return 0;

err:
    if (rc == -EAGAIN)
        return -EAGAIN;

    queue_error_response(sched->response_queue, rc, reqc);
    sched_req_free(reqc);

    return 0;
}

void rwalloc_cancel_DONE_devices(struct req_container *reqc)
{
    bool is_write = pho_request_is_write(reqc->req);
    size_t i;

    for (i = 0; i < reqc->params.rwalloc.n_media; i++) {
        if (reqc->params.rwalloc.media[i].status == SUB_REQUEST_DONE) {
            struct resp_container *respc = reqc->params.rwalloc.respc;
            pho_resp_t *resp = respc->resp;

            MUTEX_LOCK(&respc->devices[i]->ld_mutex);
            reqc->params.rwalloc.media[i].status = SUB_REQUEST_CANCEL;
            respc->devices[i]->ld_ongoing_io = false;
            if (respc->devices[i]->ld_ongoing_grouping.grouping) {
                free(respc->devices[i]->ld_ongoing_grouping.grouping);
                respc->devices[i]->ld_ongoing_grouping.grouping = NULL;
            }

            MUTEX_UNLOCK(&respc->devices[i]->ld_mutex);
            respc->devices[i] = NULL;
            if (is_write) {
                pho_resp_write_elt_t *wresp = resp->walloc->media[i];

                free(wresp->root_path);
                wresp->root_path = NULL;
                free(wresp->med_id->name);
                wresp->med_id->name = NULL;
                free(wresp->med_id->library);
                wresp->med_id->library = NULL;
            } else {
                pho_resp_read_elt_t *rresp = resp->ralloc->media[i];

                free(rresp->root_path);
                rresp->root_path = NULL;
                free(rresp->med_id->name);
                rresp->med_id->name = NULL;
                free(rresp->med_id->library);
                rresp->med_id->library = NULL;
            }
        }
    }
}

/**
 * Called with a lock on sreq->reqc
 */
static void sched_handle_read_or_write_error(struct lrs_sched *sched,
                                             struct sub_request *sreq,
                                             bool *sreq_pushed_or_requeued,
                                             bool *req_ended)
{
    struct rwalloc_params *rwalloc = &sreq->reqc->params.rwalloc;
    int rc = 0;

    *sreq_pushed_or_requeued = false;
    *req_ended = false;
    if (pho_request_is_read(sreq->reqc->req)) {
        rc = sched_handle_read_alloc_error(sched, sreq);
    } else {
        struct allocation alloc = {
            .is_sub_request = true,
            .u = {
                .sub_req = sreq,
            },
        };

        rc = sched_write_alloc_one_medium(sched, &alloc,
                                          sreq->medium_index,
                                          true);
    }

    if (!rc) {
        struct lrs_dev *selected_device =
            rwalloc->respc->devices[sreq->medium_index];

        MUTEX_LOCK(&selected_device->ld_mutex);
        selected_device->ld_sub_request = sreq;
        selected_device->ld_ongoing_scheduled = false;
        MUTEX_UNLOCK(&selected_device->ld_mutex);
        thread_signal(&selected_device->ld_device_thread);
        *sreq_pushed_or_requeued = true;
    } else {
        if (rc == -EAGAIN) {
            tsqueue_push(&sched->retry_queue, sreq);
            *sreq_pushed_or_requeued = true;
        } else {
            *sreq_pushed_or_requeued = false;
            rwalloc->rc = rc;
            rwalloc->media[sreq->medium_index].status = SUB_REQUEST_ERROR;
            queue_error_response(sched->response_queue, rc, sreq->reqc);
            rwalloc_cancel_DONE_devices(sreq->reqc);
            *req_ended = is_rwalloc_ended(sreq->reqc);
        }
    }
}

static void sched_handle_error(struct lrs_sched *sched,
                               struct sub_request *sreq)
{
    struct req_container *reqc = sreq->reqc;
    bool sreq_pushed_or_requeued = false;
    bool req_ended = false;

    ENTRY;

    MUTEX_LOCK(&reqc->mutex);
    if (locked_cancel_rwalloc_on_error(sreq, &req_ended))
        goto end_handle_error;

    if (!running) {
        struct rwalloc_medium *rwalloc_medium;

        rwalloc_medium = &reqc->params.rwalloc.media[sreq->medium_index];
        reqc->params.rwalloc.rc = -ESHUTDOWN;
        rwalloc_medium->status = SUB_REQUEST_ERROR;
        lrs_medium_release(rwalloc_medium->alloc_medium);
        rwalloc_medium->alloc_medium = NULL;
        queue_error_response(sched->response_queue, -ESHUTDOWN, reqc);
        rwalloc_cancel_DONE_devices(reqc);
        req_ended = is_rwalloc_ended(reqc);
        goto end_handle_error;
    }

    /**
     * At this time, only read and write use the error queue.
     * Format are still requeued through the request_requeue and must be
     * requeued through the retry_queue.
     */
    sched_handle_read_or_write_error(sched, sreq, &sreq_pushed_or_requeued,
                                     &req_ended);
end_handle_error:
    MUTEX_UNLOCK(&reqc->mutex);
    if (!sreq_pushed_or_requeued) {
        if (!req_ended)
            sreq->reqc = NULL;

        sub_request_free(sreq);
    }
}

int sched_handle_requests(struct lrs_sched *sched)
{
    struct req_container *reqc;
    struct sub_request *sreq;
    int rc = 0;

    /**
     * First try to re-run sub-request errors
     */
    while ((sreq = tsqueue_pop(&sched->retry_queue)) != NULL)
        sched_handle_error(sched, sreq);

    /**
     * push new request in the I/O scheduler
     */
    while ((reqc = tsqueue_pop(&sched->incoming)) != NULL) {
        pho_req_t *req = reqc->req;

        if (!running) {
            queue_error_response(sched->response_queue, -ESHUTDOWN, reqc);
            sched_req_free(reqc);
        } else if (pho_request_is_format(req) ||
                   pho_request_is_read(req) ||
                   pho_request_is_write(req)) {
            rc = io_sched_push_request(&sched->io_sched_hdl, reqc);
        } else if (pho_request_is_notify(req)) {
            pho_debug("lrs received notify request (%p)", req);
            rc = sched_handle_notify(sched, reqc);
        } else {
            /* Unexpected req->kind, very probably a programming error */
            pho_error(rc = -EPROTO,
                      "lrs received an invalid request "
                      "(no walloc, ralloc or release field)");
        }

        if (rc == 0)
            continue;

        if (rc != -EAGAIN)
            break;

        if (!pho_request_is_notify(req))
            continue;

        if (running) {
            /* Requeue last notify on -EAGAIN and running */
            tsqueue_push(&sched->incoming, reqc);
            rc = 0;
            break;
        }

        /* create an -ESHUTDOWN error on -EAGAIN and !running */
        if (!pho_request_is_notify(reqc->req) || reqc->req->notify->wait)
            queue_error_response(sched->response_queue, -ESHUTDOWN, reqc);

        sched_req_free(reqc);
    }

    return rc;
}

int lrs_schedule_work(struct lrs_sched *sched)
{
    struct req_container *reqc;
    int rc = 0;

    do {
        reqc = NULL;
        rc = io_sched_peek_request(&sched->io_sched_hdl, &reqc);
        if (rc)
            return rc;

        if (!reqc)
            /* no more requests to schedule for now */
            return 0;

        if (pho_request_is_format(reqc->req))
            rc = sched_handle_format(sched, reqc);
        else if (pho_request_is_read(reqc->req))
            rc = sched_handle_read_alloc(sched, reqc);
        else if (pho_request_is_write(reqc->req))
            rc = sched_handle_write_alloc(sched, reqc);
        else
            abort();

        if (rc == 0)
            continue;

        if (rc != -EAGAIN)
            break;

        if (running) {
            /* Requeue last request on -EAGAIN and running */
            rc = io_sched_requeue(&sched->io_sched_hdl, reqc) ? : rc;
            break;
        } else {
            int rc2;

            /* create an -ESHUTDOWN error on -EAGAIN and !running */
            queue_error_response(sched->response_queue, -ESHUTDOWN, reqc);

            rc2 = io_sched_remove_request(&sched->io_sched_hdl, reqc);
            /* overwrite EAGAIN, this failure is fatal */
            rc = rc2 ? : rc;
            sched_req_free(reqc);
            if (rc)
                break;
        }

    } while (rc != 0 && thread_is_running(&sched->sched_thread));

    return rc == -EAGAIN ? 0 : rc;
}

static void _json_object_set_str(struct json_t *object,
                                 const char *key,
                                 const char *value)
{
    json_t *str;

    if (!value)
        return;

    str = json_string(value);
    if (!str)
        return;

    json_object_set(object, key, str);
    json_decref(str);
}

static const char *device_request_type2str(struct lrs_dev *device,
                                           char buf[4])
{
    char *iter = buf;

    if (device->ld_io_request_type & IO_REQ_READ)
        *iter++ = 'R';
    if (device->ld_io_request_type & IO_REQ_WRITE)
        *iter++ = 'W';
    if (device->ld_io_request_type & IO_REQ_FORMAT)
        *iter++ = 'F';

    return buf;
}

static void sched_fetch_device_status(struct lrs_dev *device,
                                      json_t *device_status)
{
    struct media_info *medium = NULL;
    char request_type[4];
    json_t *ongoing_io;
    json_t *integer;

    memset(request_type, 0, sizeof(request_type));

    _json_object_set_str(device_status, "name", device->ld_dss_dev_info->path);
    _json_object_set_str(device_status, "library",
                         device->ld_dss_dev_info->rsc.id.library);
    _json_object_set_str(device_status, "device", device->ld_dev_path);
    _json_object_set_str(device_status, "serial",
                         device->ld_sys_dev_state.lds_serial);
    _json_object_set_str(device_status, "currently_dedicated_to",
                         device_request_type2str(device, request_type));

    integer = json_integer(device->ld_lib_dev_info.ldi_addr.lia_addr -
                           device->ld_lib_dev_info.ldi_first_addr);
    if (integer) {
        json_object_set(device_status, "address", integer);
        json_decref(integer);
    }

    ongoing_io = json_boolean(device->ld_ongoing_io);
    if (ongoing_io) {
        json_object_set(device_status, "ongoing_io", ongoing_io);
        json_decref(ongoing_io);
    }

    /* The device thread could remove the device while we are reading it here.
     * Take a reference with the mutex lock held to make sure that our local
     * pointer `medium' will remain valid.
     */
    MUTEX_LOCK(&device->ld_mutex);
    if (device->ld_dss_media_info)
        medium = lrs_medium_acquire(&device->ld_dss_media_info->rsc.id);
    MUTEX_UNLOCK(&device->ld_mutex);
    if (!medium)
        return;

    _json_object_set_str(device_status, "mount_path", device->ld_mnt_path);
    _json_object_set_str(device_status, "media", medium->rsc.id.name);

    lrs_medium_release(medium); /* release local reference */
}

int sched_handle_monitor(struct lrs_sched *sched, json_t *status)
{
    json_t *device_status;
    int rc = 0;
    int i;

    for (i = 0; i < sched->devices.ldh_devices->len; i++) {
        struct lrs_dev *device;

        device_status = json_object();
        if (!device_status)
            LOG_RETURN(-ENOMEM, "Failed to allocate device_status");

        device = lrs_dev_hdl_get(&sched->devices, i);

        sched_fetch_device_status(device, device_status);

        rc = json_array_append_new(status, device_status);
        if (rc == -1)
            LOG_GOTO(free_device_status,
                     rc = -ENOMEM,
                     "Failed to append device status to array");

        continue;

free_device_status:
        json_decref(device_status);
    }

    return rc;
}

static int compute_wakeup_time(const struct timespec *timeout,
                               struct timespec *date)
{
    struct timespec now;
    int rc;

    rc = clock_gettime(CLOCK_REALTIME, &now);
    if (rc)
        LOG_RETURN(-errno, "clock_gettime: unable to get CLOCK_REALTIME");

    *date = add_timespec(&now, timeout);

    return rc;
}

static void *lrs_sched_thread(void *sdata)
{
    struct timespec timeout = { .tv_sec = 0, .tv_nsec = 100000000 }; /* 100ms */
    struct lrs_sched *sched = (struct lrs_sched *) sdata;
    struct thread_info *thread = &sched->sched_thread;
    int rc;

    while (thread_is_running(thread)) {
        struct timespec wakeup_date;

        rc = sched_handle_requests(sched);
        if (rc)
            LOG_GOTO(end_thread, thread->status = rc,
                     "'%s' scheduler: error while handling requests",
                     rsc_family2str(sched->family));

        rc = io_sched_dispatch_devices(&sched->io_sched_hdl,
                                       sched->devices.ldh_devices);
        if (rc)
            LOG_GOTO(end_thread, thread->status = rc,
                     "'%s' scheduler: failed to dispatch devices to I/O "
                     "schedulers",
                     rsc_family2str(sched->family));

        rc = lrs_schedule_work(sched);
        if (rc)
            LOG_GOTO(end_thread, thread->status = rc,
                     "'%s' scheduler: error while scheduling requests",
                     rsc_family2str(sched->family));

        rc = compute_wakeup_time(&timeout, &wakeup_date);
        if (rc)
            GOTO(end_thread, thread->status = rc);

        rc = thread_signal_timed_wait(thread, &wakeup_date);
        if (rc < 0)
            LOG_GOTO(end_thread, thread->status = rc,
                     "sched thread '%d': fatal error", sched->family);
    }

end_thread:
    thread->state = THREAD_STOPPED;
    pthread_exit(&thread->status);
}
