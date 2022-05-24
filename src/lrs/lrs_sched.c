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
#include "lrs_sched.h"
#include "pho_common.h"
#include "pho_dss.h"
#include "pho_io.h"
#include "pho_ldm.h"
#include "pho_srl_common.h"
#include "pho_type_utils.h"
#include "lrs_device.h"

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

#define TAPE_TYPE_SECTION_CFG "tape_type \"%s\""
#define MODELS_CFG_PARAM "models"
#define DRIVE_RW_CFG_PARAM "drive_rw"
#define DRIVE_TYPE_SECTION_CFG "drive_type \"%s\""

enum sched_operation {
    LRS_OP_NONE = 0,
    LRS_OP_READ,
    LRS_OP_WRITE,
    LRS_OP_FORMAT,
};

static void *lrs_sched_thread(void *sdata);

static int format_media_init(struct format_media *format_media)
{
    int rc;

    rc = pthread_mutex_init(&format_media->mutex, NULL);
    if (rc)
        LOG_RETURN(rc, "Error on initializing format_media mutex");

    format_media->media_name = g_hash_table_new(g_str_hash, g_str_equal);
    return 0;
}

static void format_media_clean(struct format_media *format_media)
{
    int rc;

    rc = pthread_mutex_destroy(&format_media->mutex);
    if (rc)
        pho_error(rc, "Error on destroying format_media mutex");

    if (format_media->media_name) {
        g_hash_table_unref(format_media->media_name);
        format_media->media_name = NULL;
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
    if (g_hash_table_contains(format_media->media_name, medium->rsc.id.name)) {
        pthread_mutex_unlock(&format_media->mutex);
        return false;
    }

    g_hash_table_add(format_media->media_name, medium->rsc.id.name);
    pthread_mutex_unlock(&format_media->mutex);
    return true;
}

void format_medium_remove(struct format_media *format_media,
                          struct media_info *medium)
{
    pthread_mutex_lock(&format_media->mutex);
    g_hash_table_remove(format_media->media_name, medium->rsc.id.name);
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
 * Unlock a resource device at DSS level and clean the corresponding lock
 *
 * @param[in]   sched   current scheduler
 * @param[in]   type    DSS type of the resource to release
 * @param[in]   item    Resource to release
 * @param[out]  lock    lock to clean
 */
static int sched_resource_release(struct lrs_sched *sched, enum dss_type type,
                                  void *item, struct pho_lock *lock)
{
    int rc;

    ENTRY;

    rc = dss_unlock(&sched->sched_thread.dss, type, item, 1, false);
    if (rc)
        LOG_RETURN(rc, "Cannot unlock a resource");

    pho_lock_clean(lock);
    return 0;
}

static int sched_medium_release(struct lrs_sched *sched,
                                struct media_info *medium)
{
    int rc;

    rc = sched_resource_release(sched, DSS_MEDIA, medium, &medium->lock);
    if (rc)
        pho_error(rc,
                  "Error when releasing medium '%s' with current lock "
                  "(hostname %s, owner %d)", medium->rsc.id.name,
                  medium->lock.hostname, medium->lock.owner);

    pho_debug("unlock: medium %s\n", medium->rsc.id.name);

    return rc;
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
    rc = dss_lock(dss, type, item, 1);
    if (rc)
        pho_error(rc, "Unable to get lock on item for refresh");

    pho_debug("lock: %s %s\n", dss_type2str(type),
              type == DSS_DEVICE ?
              ((struct dev_info *)item)->rsc.id.name :
              type == DSS_MEDIA ?
              ((struct media_info *)item)->rsc.id.name :
              "???");

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
 * If lock->owner is different from sched->lock_owner, renew the lock with
 * the current owner (PID).
 */
static int check_renew_owner(struct lrs_sched *sched, enum dss_type type,
                             void *item, struct pho_lock *lock)
{
    int rc;

    if (lock->owner != sched->lock_owner) {
        pho_warn("'%s' is already locked by owner %d, owner %d will "
                 "take ownership of this device",
                 dss_type_names[type], lock->owner, sched->lock_owner);

        /**
         * Unlocking here is dangerous if there is another process than the
         * LRS on the same node that also acquires locks. If it becomes the case
         * we have to warn and return an error and we must not take the
         * ownership of this resource again.
         */
        /* unlock previous owner */
        rc = dss_unlock(&sched->sched_thread.dss, type, item, 1, true);
        if (rc)
            LOG_RETURN(rc,
                       "Unable to clear previous lock (hostname: %s, owner "
                       " %d) on item",
                       lock->hostname, lock->owner);

        /* get the lock again */
        rc = take_and_update_lock(&sched->sched_thread.dss, type, item,
                                  lock);
        if (rc)
            LOG_RETURN(rc, "Unable to get and refresh lock");
    }

    return 0;
}

/**
 * First, check that lock->hostname is the same as sched->lock_hostname. If not,
 * -EALREADY is returned.
 *
 * Then, if lock->owner is different from sched->lock_owner, renew the lock with
 * the current owner (PID) by calling check_renew_owner.
 */
static int check_renew_lock(struct lrs_sched *sched, enum dss_type type,
                            void *item, struct pho_lock *lock)
{
    if (strcmp(lock->hostname, sched->lock_hostname)) {
        pho_warn("Resource already locked by host %s instead of %s",
                 lock->hostname, sched->lock_hostname);
        return -EALREADY;
    }

    return check_renew_owner(sched, type, item, lock);
}

int check_and_take_device_lock(struct lrs_sched *sched,
                               struct dev_info *dev)
{
    int rc;

    if (dev->lock.hostname) {
        rc = check_renew_lock(sched, DSS_DEVICE, dev, &dev->lock);
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
 * Retrieve media info from DSS for the given ID.
 * @param pmedia[out] returned pointer to a media_info structure
 *                    allocated by this function.
 * @param id[in]      ID of the media.
 */
static int sched_fill_media_info(struct lrs_sched *sched,
                                 struct media_info **pmedia,
                                 const struct pho_id *id)
{
    struct dss_handle   *dss = &sched->sched_thread.dss;
    struct media_info   *media_res = NULL;
    struct dss_filter    filter;
    int                  mcnt = 0;
    int                  rc;

    if (id == NULL || pmedia == NULL)
        return -EINVAL;

    pho_debug("Retrieving media info for %s '%s'",
              rsc_family2str(id->family), id->name);

    rc = dss_filter_build(&filter,
                          "{\"$AND\": ["
                          "  {\"DSS::MDA::family\": \"%s\"},"
                          "  {\"DSS::MDA::id\": \"%s\"}"
                          "]}", rsc_family2str(id->family), id->name);
    if (rc)
        return rc;

    /* get media info from DB */
    rc = dss_media_get(dss, &filter, &media_res, &mcnt);
    if (rc)
        GOTO(out_nores, rc);

    if (mcnt == 0) {
        pho_info("No media found matching %s '%s'",
                 rsc_family2str(id->family), id->name);
        GOTO(out_free, rc = -ENXIO);
    } else if (mcnt > 1) {
        LOG_GOTO(out_free, rc = -EINVAL,
                 "Too many media found matching id '%s'", id->name);
    }

    media_info_free(*pmedia);
    *pmedia = media_info_dup(media_res);
    if (!*pmedia)
        LOG_GOTO(out_free, rc = -ENOMEM, "Couldn't duplicate media info");

    if ((*pmedia)->lock.hostname != NULL) {
        rc = check_renew_lock(sched, DSS_MEDIA, *pmedia, &(*pmedia)->lock);
        if (rc == -EALREADY) {
            LOG_GOTO(out_free, rc,
                     "Media '%s' is locked by (hostname: %s, owner: %d)",
                     id->name, (*pmedia)->lock.hostname, (*pmedia)->lock.owner);
        } else if (rc) {
            LOG_GOTO(out_free, rc,
                     "Error while checking media '%s' locked with hostname "
                     "'%s' and owner '%d'",
                     id->name, (*pmedia)->lock.hostname, (*pmedia)->lock.owner);
        }
    }

    pho_debug("%s: spc_free=%zd",
              (*pmedia)->rsc.id.name, (*pmedia)->stats.phys_spc_free);

    rc = 0;

out_free:
    dss_res_free(media_res, mcnt);
out_nores:
    dss_filter_free(&filter);
    return rc;
}

/**
 * Retrieve device information from system and complementary info from DB.
 * - check DB device info is consistent with mtx output.
 * - get operationnal status from system (loaded or not).
 * - for loaded drives, the mounted volume + LTFS mount point, if mounted.
 * - get media information from DB for loaded drives.
 *
 * @param[in]  dss  handle to dss connection.
 * @param[in]  lib  library handler for tape devices.
 * @param[out] dev  lrs_dev structure filled with all needed information.
 */
static int sched_fill_dev_info(struct lrs_sched *sched, struct lib_adapter *lib,
                               struct lrs_dev *dev)
{
    struct dev_adapter deva;
    struct dev_info   *devi;
    int                rc;

    ENTRY;

    if (dev == NULL)
        return -EINVAL;

    devi = dev->ld_dss_dev_info;

    media_info_free(dev->ld_dss_media_info);
    dev->ld_dss_media_info = NULL;
    dev->ld_op_status = PHO_DEV_OP_ST_EMPTY;

    rc = get_dev_adapter(devi->rsc.id.family, &deva);
    if (rc)
        return rc;

    /* get path for the given serial */
    rc = ldm_dev_lookup(&deva, devi->rsc.id.name, dev->ld_dev_path,
                        sizeof(dev->ld_dev_path));
    if (rc) {
        pho_debug("Device lookup failed: serial '%s'", devi->rsc.id.name);
        return rc;
    }

    /* now query device by path */
    ldm_dev_state_fini(&dev->ld_sys_dev_state);
    rc = ldm_dev_query(&deva, dev->ld_dev_path, &dev->ld_sys_dev_state);
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
    rc = ldm_lib_drive_lookup(lib, devi->rsc.id.name, &dev->ld_lib_dev_info);
    if (rc) {
        pho_debug("Failed to query the library about device '%s'",
                  devi->rsc.id.name);
        return rc;
    }

    if (dev->ld_lib_dev_info.ldi_full) {
        struct pho_id *medium_id;
        struct fs_adapter fsa;

        dev->ld_op_status = PHO_DEV_OP_ST_LOADED;
        medium_id = &dev->ld_lib_dev_info.ldi_medium_id;

        pho_debug("Device '%s' (S/N '%s') contains medium '%s'",
                  dev->ld_dev_path, devi->rsc.id.name, medium_id->name);

        /* get media info for loaded drives */
        rc = sched_fill_media_info(sched, &dev->ld_dss_media_info, medium_id);

        if (rc) {
            if (rc == -ENXIO)
                pho_error(rc,
                          "Device '%s' (S/N '%s') contains medium '%s', but "
                          "this medium cannot be found", dev->ld_dev_path,
                          devi->rsc.id.name, medium_id->name);

            if (rc == -EALREADY)
                pho_error(rc,
                          "Device '%s' (S/N '%s') is owned by host %s but "
                          "contains medium '%s' which is locked by an other "
                          "hostname %s", dev->ld_dev_path, devi->rsc.id.name,
                           devi->host, medium_id->name,
                           dev->ld_dss_media_info->lock.hostname);

            return rc;
        }

        /* get lock for loaded media */
        if (!dev->ld_dss_media_info->lock.hostname) {
            rc = take_and_update_lock(&sched->sched_thread.dss, DSS_MEDIA,
                                      dev->ld_dss_media_info,
                                      &dev->ld_dss_media_info->lock);
            if (rc)
                LOG_RETURN(rc,
                           "Unable to lock the media '%s' loaded in an owned "
                           "device '%s'", dev->ld_dss_media_info->rsc.id.name,
                           dev->ld_dev_path);
        }

        /* See if the device is currently mounted */
        rc = get_fs_adapter(dev->ld_dss_media_info->fs.type, &fsa);
        if (rc)
            return rc;

        /* If device is loaded, check if it is mounted as a filesystem */
        rc = ldm_fs_mounted(&fsa, dev->ld_dev_path, dev->ld_mnt_path,
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

    pho_debug("Drive '%s' is '%s'", dev->ld_dev_path,
              op_status2str(dev->ld_op_status));

    return rc;
}

/**
 * Load device states into memory.
 * Do nothing if device status is already loaded.
 */
static int sched_load_dev_state(struct lrs_sched *sched)
{
    bool                clean_devices = false;
    struct lib_adapter  lib;
    int                 rc;
    int                 i;

    ENTRY;

    if (sched->devices.ldh_devices->len == 0) {
        pho_verb("Try to load state of an empty list of devices");
        return -ENXIO;
    }

    /* get a handle to the library to query it */
    rc = wrap_lib_open(sched->family, &lib);
    if (rc)
        LOG_RETURN(rc, "Error while loading devices when opening library");

    for (i = 0 ; i < sched->devices.ldh_devices->len; i++) {
        struct lrs_dev *dev;

        dev = lrs_dev_hdl_get(&sched->devices, i);

        MUTEX_LOCK(&dev->ld_mutex);
        rc = sched_fill_dev_info(sched, &lib, dev);
        if (rc) {
            pho_error(rc,
                      "Fail to init device '%s', stopping corresponding device "
                      "thread", dev->ld_dev_path);
            thread_signal_stop_on_error(&dev->ld_device_thread, rc);
        } else {
            clean_devices = true;
        }
        MUTEX_UNLOCK(&dev->ld_mutex);
    }

    /* close handle to the library */
    rc = ldm_lib_close(&lib);
    if (rc)
        LOG_RETURN(rc,
                   "Error while closing the library handle after loading "
                   "device state");

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
    int rc;

    ENTRY;

    rc = dss_lock_device_clean(&sched->sched_thread.dss,
                               rsc_family_names[sched->family],
                               sched->lock_hostname, sched->lock_owner);
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
    struct media_info *media = NULL;
    int cnt = 0;
    int rc;
    int i;

    ENTRY;

    media = malloc(sched->devices.ldh_devices->len * sizeof(*media));
    if (!media)
        LOG_RETURN(-errno, "Failed to allocate media list");

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
                              sched->lock_hostname, sched->lock_owner);
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

    rc = format_media_init(&sched->ongoing_format);
    if (rc)
        LOG_RETURN(rc, "Failed to init sched format media");

    rc = lrs_dev_hdl_init(&sched->devices, family);
    if (rc)
        LOG_GOTO(err_format_media, rc, "Failed to initialize device handle");

    rc = fill_host_owner(&sched->lock_hostname, &sched->lock_owner);
    if (rc)
        LOG_GOTO(err_hdl_fini, rc, "Failed to get hostname and PID");

    /* Connect to the DSS */
    rc = dss_init(&sched->sched_thread.dss);
    if (rc)
        LOG_GOTO(err_hdl_fini, rc, "Failed to init sched dss handle");

    rc = tsqueue_init(&sched->req_queue);
    if (rc)
        LOG_GOTO(err_dss_fini, rc, "Failed to init sched req_queue");

    rc = tsqueue_init(&sched->retry_queue);
    if (rc)
        LOG_GOTO(err_req_queue_fini, rc, "Failed to init sched req_queue");

    sched->response_queue = resp_queue;

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

err_req_queue_fini:
    tsqueue_destroy(&sched->req_queue, sched_req_free);
err_dss_fini:
    dss_fini(&sched->sched_thread.dss);
err_hdl_fini:
    lrs_dev_hdl_fini(&sched->devices);
err_format_media:
    format_media_clean(&sched->ongoing_format);
    return rc;
}

int prepare_error(struct resp_container *resp_cont, int req_rc,
                  const struct req_container *req_cont)
{
    int rc;

    resp_cont->socket_id = req_cont->socket_id;
    rc = pho_srl_response_error_alloc(resp_cont->resp);
    if (rc)
        LOG_RETURN(rc, "Failed to allocate response");

    resp_cont->resp->error->rc = req_rc;

    resp_cont->resp->req_id = req_cont->req->id;
    if (pho_request_is_write(req_cont->req))
        resp_cont->resp->error->req_kind = PHO_REQUEST_KIND__RQ_WRITE;
    else if (pho_request_is_read(req_cont->req))
        resp_cont->resp->error->req_kind = PHO_REQUEST_KIND__RQ_READ;
    else if (pho_request_is_release(req_cont->req))
        resp_cont->resp->error->req_kind = PHO_REQUEST_KIND__RQ_RELEASE;
    else if (pho_request_is_format(req_cont->req))
        resp_cont->resp->error->req_kind = PHO_REQUEST_KIND__RQ_FORMAT;
    else if (pho_request_is_notify(req_cont->req))
        resp_cont->resp->error->req_kind = PHO_REQUEST_KIND__RQ_NOTIFY;

    return 0;
}

int queue_error_response(struct tsqueue *response_queue, int req_rc,
                         struct req_container *reqc)
{
    struct resp_container *resp_cont;
    int rc;

    resp_cont = malloc(sizeof(*resp_cont));
    if (!resp_cont)
        LOG_RETURN(-ENOMEM, "Unable to allocate resp_cont");

    resp_cont->resp = malloc(sizeof(*resp_cont->resp));
    if (!resp_cont->resp)
        LOG_GOTO(clean_resp_cont, rc = -ENOMEM,
                 "Unable to allocate resp_cont->resp");

    rc = prepare_error(resp_cont, req_rc, reqc);
    if (rc)
        goto clean;

    tsqueue_push(response_queue, resp_cont);

    return 0;

clean:
    free(resp_cont->resp);
clean_resp_cont:
    free(resp_cont);
    return rc;
}

void sched_resp_free(void *_respc)
{
    struct resp_container *respc = (struct resp_container *)_respc;

    if (!respc)
        return;

    if (pho_response_is_write(respc->resp) ||
        pho_response_is_read(respc->resp))
        free(respc->devices);

    pho_srl_response_free(respc->resp, false);
    free(respc->resp);
}

void sched_fini(struct lrs_sched *sched)
{
    if (sched == NULL)
        return;

    lrs_dev_hdl_clear(&sched->devices);
    lrs_dev_hdl_fini(&sched->devices);
    dss_fini(&sched->sched_thread.dss);
    tsqueue_destroy(&sched->req_queue, sched_req_free);
    tsqueue_destroy(&sched->retry_queue, sched_req_free);
    format_media_clean(&sched->ongoing_format);
}

bool sched_has_running_devices(struct lrs_sched *sched)
{
    int i;

    for (i = 0; i < sched->devices.ldh_devices->len; i++) {
        struct lrs_dev *dev;

        dev = lrs_dev_hdl_get(&sched->devices, i);
        if (dev->ld_ongoing_io || dev->ld_needs_sync ||
            dev->ld_sync_params.tosync_array->len)
            return true;
    }

    return false;
}

/**
 * Build a filter string fragment to filter on a given tag set. The returned
 * string is allocated with malloc. NULL is returned when ENOMEM is encountered.
 *
 * The returned string looks like the following:
 * {"$AND": [{"DSS:MDA::tags": "tag1"}]}
 */
static char *build_tag_filter(const struct tags *tags)
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
    for (i = 0; i < tags->n_tags; i++) {
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
                                json_string(tags->tags[i]))) {
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
    tag_filter_json = json_dumps(tag_filters, 0);

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
            prev_medium = devices[i]->ld_dss_media_info;

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
            *already_alloc = true;
            return 0;
        }
    }

    *already_alloc = false;
    return 0;
}

static struct lrs_dev *search_loaded_media(struct lrs_sched *sched,
                                           const char *name)
{
    int i;

    ENTRY;

    for (i = 0; i < sched->devices.ldh_devices->len; i++) {
        const char *media_id;
        struct lrs_dev *dev;

        dev = lrs_dev_hdl_get(&sched->devices, i);

        if (dev->ld_op_status != PHO_DEV_OP_ST_MOUNTED &&
            dev->ld_op_status != PHO_DEV_OP_ST_LOADED)
            continue;

        /* The drive may contain a media unknown to phobos, skip it */
        if (dev->ld_dss_media_info == NULL)
            continue;

        media_id = dev->ld_dss_media_info->rsc.id.name;
        if (media_id == NULL) {
            pho_warn("Cannot retrieve media ID from device '%s'",
                     dev->ld_dev_path);
            continue;
        }

        if (!strcmp(name, media_id))
            return dev;
    }

    return NULL;
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
 */
static int sched_select_media(struct lrs_sched *sched,
                              struct media_info **p_media, size_t required_size,
                              enum rsc_family family, const struct tags *tags,
                              struct req_container *reqc, size_t n_med,
                              size_t not_alloc)
{
    struct media_info   *pmedia_res = NULL;
    struct media_info   *split_media_best;
    size_t               avail_size;
    struct media_info   *whole_media_best;
    struct media_info   *chosen_media;
    struct dss_filter    filter;
    char                *tag_filter_json = NULL;
    bool                 with_tags = tags != NULL && tags->n_tags > 0;
    int                  mcnt = 0;
    int                  rc;
    int                  i;

    ENTRY;

    if (with_tags) {
        tag_filter_json = build_tag_filter(tags);
        if (!tag_filter_json)
            LOG_GOTO(err_nores, rc = -ENOMEM, "while building tags dss filter");
    }

    rc = dss_filter_build(&filter,
                          "{\"$AND\": ["
                          /* Basic criteria */
                          "  {\"DSS::MDA::family\": \"%s\"},"
                          /* Check put media operation flags */
                          "  {\"DSS::MDA::put\": \"t\"},"
                          /* Exclude media locked by admin */
                          "  {\"DSS::MDA::adm_status\": \"%s\"},"
                          "  {\"$NOR\": ["
                               /* Exclude non-formatted media */
                          "    {\"DSS::MDA::fs_status\": \"%s\"},"
                               /* Exclude full media */
                          "    {\"DSS::MDA::fs_status\": \"%s\"}"
                          "  ]}"
                          "  %s%s"
                          "]}",
                          rsc_family2str(family),
                          rsc_adm_status2str(PHO_RSC_ADM_ST_UNLOCKED),
                          /**
                           * @TODO add criteria to limit the maximum number of
                           * data fragments:
                           * vol_free >= required_size/max_fragments
                           * with a configurable max_fragments of 4 for example)
                           */
                          fs_status2str(PHO_FS_STATUS_BLANK),
                          fs_status2str(PHO_FS_STATUS_FULL),
                          with_tags ? ", " : "",
                          with_tags ? tag_filter_json : "");

    free(tag_filter_json);
    if (rc)
        return rc;

    rc = dss_media_get(&sched->sched_thread.dss, &filter, &pmedia_res,
                       &mcnt);
    dss_filter_free(&filter);
    if (rc)
        GOTO(err_nores, rc);

lock_race_retry:
    chosen_media = NULL;
    whole_media_best = NULL;
    split_media_best = NULL;
    avail_size = 0;

    /* get the best fit */
    for (i = 0; i < mcnt; i++) {
        struct media_info *curr = &pmedia_res[i];
        struct lrs_dev *dev;
        bool already_alloc;

        /* exclude medium already booked for this allocation */
        rc = medium_in_devices(curr, reqc, n_med, not_alloc, &already_alloc);
        if (rc)
            LOG_GOTO(free_res, rc = -EAGAIN,
                     "Unable to test if medium is already alloc");

        if (already_alloc)
            continue;

        avail_size += curr->stats.phys_spc_free;

        /* already locked */
        if (curr->lock.hostname != NULL) {

            if (check_renew_lock(sched, DSS_MEDIA, curr, &curr->lock))
                /* not locked by myself */
                continue;
        }

        /* already loaded and in use ? */
        dev = search_loaded_media(sched, curr->rsc.id.name);
        if (dev && (dev->ld_ongoing_io || dev->ld_needs_sync ||
                    !thread_is_running(&dev->ld_device_thread)))
            continue;

        if (split_media_best == NULL ||
            curr->stats.phys_spc_free > split_media_best->stats.phys_spc_free)
            split_media_best = curr;

        if (curr->stats.phys_spc_free < required_size)
            continue;

        if (whole_media_best == NULL ||
            curr->stats.phys_spc_free < whole_media_best->stats.phys_spc_free)
            whole_media_best = curr;
    }

    if (avail_size < required_size) {
        pho_warn("Available space on media : %zd, required size : %zd",
                 avail_size, required_size);
        GOTO(free_res, rc = -ENOSPC);
    }

    if (whole_media_best != NULL) {
        chosen_media = whole_media_best;
    } else if (split_media_best != NULL) {
        chosen_media = split_media_best;
        pho_info("Split %zd required_size on %zd avail size on %s medium",
                 required_size, chosen_media->stats.phys_spc_free,
                 chosen_media->rsc.id.name);
    } else {
        pho_info("No medium available, wait for one");
        GOTO(free_res, rc = -EAGAIN);
    }

    if (!chosen_media->lock.hostname) {
        pho_debug("Acquiring selected media '%s'", chosen_media->rsc.id.name);
        rc = take_and_update_lock(&sched->sched_thread.dss, DSS_MEDIA,
                                  chosen_media, &chosen_media->lock);
        if (rc) {
            pho_debug("Failed to lock media '%s', looking for another one",
                      chosen_media->rsc.id.name);
            goto lock_race_retry;
        }
    }

    pho_verb("Selected %s '%s': %zd bytes free", rsc_family2str(family),
             chosen_media->rsc.id.name,
             chosen_media->stats.phys_spc_free);

    *p_media = media_info_dup(chosen_media);
    if (*p_media == NULL) {
        sched_medium_release(sched, chosen_media);
        LOG_GOTO(free_res, rc = -ENOMEM,
                 "Unable to duplicate chosen media '%s'",
                 chosen_media->rsc.id.name);
    }

    rc = 0;

free_res:
    dss_res_free(pmedia_res, mcnt);

err_nores:
    return rc;
}

/**
 * Get the value of the configuration parameter that contains
 * the list of drive models for a given drive type.
 * e.g. "LTO6_drive" -> "ULTRIUM-TD6,ULT3580-TD6,..."
 *
 * @return 0 on success, a negative POSIX error code on failure.
 */
static int drive_models_by_type(const char *drive_type, const char **list)
{
    char *section_name;
    int rc;

    /* build drive_type section name */
    rc = asprintf(&section_name, DRIVE_TYPE_SECTION_CFG,
                  drive_type);
    if (rc < 0)
        return -ENOMEM;

    /* get list of drive models */
    rc = pho_cfg_get_val(section_name, MODELS_CFG_PARAM, list);
    if (rc)
        pho_error(rc, "Unable to find parameter "MODELS_CFG_PARAM" in section "
                  "'%s' for drive type '%s'", section_name, drive_type);

    free(section_name);
    return rc;
}

/**
 * Get the value of the configuration parameter that contains
 * the list of write-compatible drives for a given tape model.
 * e.g. "LTO5" -> "LTO5_drive,LTO6_drive"
 *
 * @return 0 on success, a negative POSIX error code on failure.
 */
static int rw_drive_types_for_tape(const char *tape_model, const char **list)
{
    char *section_name;
    int rc;

    /* build tape_type section name */
    rc = asprintf(&section_name, TAPE_TYPE_SECTION_CFG, tape_model);
    if (rc < 0)
        return -ENOMEM;

    /* get list of drive_rw types */
    rc = pho_cfg_get_val(section_name, DRIVE_RW_CFG_PARAM, list);
    if (rc)
        pho_error(rc, "Unable to find parameter "DRIVE_RW_CFG_PARAM
                  " in section '%s' for tape model '%s'",
                  section_name, tape_model);

    free(section_name);
    return rc;
}

/**
 * Search a given item in a coma-separated list.
 *
 * @param[in]  list     Comma-separated list of items.
 * @param[in]  str      Item to find in the list.
 * @param[out] res      true of the string is found, false else.
 *
 * @return 0 on success. A negative POSIX error code on error.
 */
static int search_in_list(const char *list, const char *str, bool *res)
{
    char *parse_list;
    char *item;
    char *saveptr;

    *res = false;

    /* copy input list to parse it */
    parse_list = strdup(list);
    if (parse_list == NULL)
        return -errno;

    /* check if the string is in the list */
    for (item = strtok_r(parse_list, ",", &saveptr);
         item != NULL;
         item = strtok_r(NULL, ",", &saveptr)) {
        if (strcmp(item, str) == 0) {
            *res = true;
            goto out_free;
        }
    }

out_free:
    free(parse_list);
    return 0;
}

/**
 * This function determines if the input drive and tape are compatible.
 *
 * @param[in]  tape  tape to check compatibility
 * @param[in]  drive drive to check compatibility
 * @param[out] res   true if the tape and drive are compatible, else false
 *
 * @return 0 on success, negative error code on failure and res is false
 */
static int tape_drive_compat(const struct media_info *tape,
                             const struct lrs_dev *drive, bool *res)
{
    const char *rw_drives;
    char *parse_rw_drives;
    char *drive_type;
    char *saveptr;
    int rc;

    /* false by default */
    *res = false;

    /** XXX FIXME: this function is called for each drive for the same tape by
     *  the function dev_picker. Each time, we build/allocate same strings and
     *  we parse again the conf. This behaviour is heavy and not optimal.
     */
    rc = rw_drive_types_for_tape(tape->rsc.model, &rw_drives);
    if (rc)
        return rc;

    /* copy the rw_drives list to tokenize it */
    parse_rw_drives = strdup(rw_drives);
    if (parse_rw_drives == NULL)
        return -errno;

    /* For each compatible drive type, get list of associated drive models
     * and search the current drive model in it.
     */
    for (drive_type = strtok_r(parse_rw_drives, ",", &saveptr);
         drive_type != NULL;
         drive_type = strtok_r(NULL, ",", &saveptr)) {
        const char *drive_model_list;

        rc = drive_models_by_type(drive_type, &drive_model_list);
        if (rc)
            goto out_free;

        rc = search_in_list(drive_model_list, drive->ld_dss_dev_info->rsc.model,
                            res);
        if (rc)
            goto out_free;
        /* drive model found: media is compatible */
        if (*res)
            break;
    }

out_free:
    free(parse_rw_drives);
    return rc;
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
 * Returns a device by setting its ld_ongoing_io flag to true.
 *
 * @param dss     DSS handle.
 * @param op_st   Filter devices by the given operational status.
 *                No filtering is op_st is PHO_DEV_OP_ST_UNSPEC.
 * @param select_func    Drive selection function.
 * @param required_size  Required size for the operation.
 * @param media_tags     Mandatory tags for the contained media (for write
 *                       requests only).
 * @param pmedia         Media that should be used by the drive to check
 *                       compatibility (ignored if NULL)
 */
static struct lrs_dev *dev_picker(struct lrs_sched *sched,
                                  enum dev_op_status op_st,
                                  device_select_func_t select_func,
                                  size_t required_size,
                                  const struct tags *media_tags,
                                  struct media_info *pmedia, bool is_write)
{
    struct lrs_dev    *selected = NULL;
    int                  selected_i = -1;
    int                  i;
    int                  rc;

    ENTRY;

    for (i = 0; i < sched->devices.ldh_devices->len; i++) {
        struct lrs_dev *itr = lrs_dev_hdl_get(&sched->devices, i);
        struct lrs_dev *prev = selected;

        if (itr->ld_ongoing_io || itr->ld_needs_sync) {
            pho_debug("Skipping busy device '%s'", itr->ld_dev_path);
            continue;
        }

        if ((itr->ld_op_status == PHO_DEV_OP_ST_FAILED) ||
            (op_st != PHO_DEV_OP_ST_UNSPEC && itr->ld_op_status != op_st)) {
            pho_debug("Skipping device '%s' with incompatible status %s",
                      itr->ld_dev_path, op_status2str(itr->ld_op_status));
            continue;
        }

        if (!thread_is_running(&itr->ld_device_thread)) {
            pho_debug("Skipping ending or stopped device '%s'",
                      itr->ld_dev_path);
            continue;
        }

        /*
         * The intent is to write: exclude media that are administratively
         * locked, full, do not have the put operation flag and do not have the
         * requested tags
         */
        if (is_write && itr->ld_dss_media_info) {
            if (itr->ld_dss_media_info->rsc.adm_status !=
                    PHO_RSC_ADM_ST_UNLOCKED) {
                pho_debug("Media '%s' is not unlocked",
                          itr->ld_dss_media_info->rsc.id.name);
                continue;
            }

            if (itr->ld_dss_media_info->fs.status == PHO_FS_STATUS_FULL) {
                pho_debug("Media '%s' is full",
                          itr->ld_dss_media_info->rsc.id.name);
                continue;
            }

            if (!itr->ld_dss_media_info->flags.put) {
                pho_debug("Media '%s' has a false put operation flag",
                          itr->ld_dss_media_info->rsc.id.name);
                continue;
            }

            if (media_tags->n_tags > 0 &&
                !tags_in(&itr->ld_dss_media_info->tags, media_tags)) {
                pho_debug("Media '%s' does not match required tags",
                          itr->ld_dss_media_info->rsc.id.name);
                continue;
            }
        }

        /* check tape / drive compat */
        if (pmedia) {
            bool res;

            if (tape_drive_compat(pmedia, itr, &res)) {
                selected = NULL;
                break;
            }

            if (!res)
                continue;
        }

        rc = select_func(required_size, itr, &selected);
        if (prev != selected)
            selected_i = i;

        if (rc < 0) {
            pho_debug("Device selection function failed");
            selected = NULL;
            break;
        } else if (rc == 0) /* stop searching */
            break;
    }

    if (selected != NULL) {
        pho_debug("Picked dev number %d (%s)",
                  selected_i,
                  selected->ld_dev_path);
        selected->ld_ongoing_io = true;
    } else {
        pho_debug("Could not find a suitable %s device", op_status2str(op_st));
    }

    return selected;
}

/**
 * Get the first device with enough space.
 * @retval 0 to stop searching for a device
 * @retval 1 to check next device.
 */
static int select_first_fit(size_t required_size,
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
static int select_empty_loaded_mount(size_t required_size,
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
static device_select_func_t get_dev_policy(void)
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

    pho_error(EINVAL, "Invalid LRS policy name '%s' "
              "(expected: 'best_fit' or 'first_fit')", policy_str);

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
 * @return                    True if one compatible drive is found, else false.
 */
static bool compatible_drive_exists(struct lrs_sched *sched,
                                    struct media_info *pmedia,
                                    struct lrs_dev *selected_devs,
                                    size_t n_selected_devs,
                                    size_t not_selected)
{
    int i, j;

    for (i = 0; i < sched->devices.ldh_devices->len; i++) {
        struct lrs_dev *dev = lrs_dev_hdl_get(&sched->devices, i);
        bool is_already_selected = false;

        if (dev->ld_op_status == PHO_DEV_OP_ST_FAILED ||
            !thread_is_running(&dev->ld_device_thread))
            continue;

        /* check the device is not already selected */
        for (j = 0; j < n_selected_devs; ++j) {
            if (j == not_selected)
                continue;

            if (!strcmp(dev->ld_dss_dev_info->rsc.id.name,
                        selected_devs[j].ld_dss_dev_info->rsc.id.name)) {
                is_already_selected = true;
                break;
            }
        }

        if (is_already_selected)
            continue;

        if (pmedia) {
            bool is_compat;

            if (tape_drive_compat(pmedia, dev, &is_compat))
                continue;

            if (is_compat)
                return true;
        }
    }

    return false;
}

static int
check_read_medium_permission_and_status(const struct media_info *medium)
{
    if (!medium->flags.get)
        LOG_RETURN(-EPERM, "'%s' get flag is false",
                   medium->rsc.id.name);
    if (medium->fs.status == PHO_FS_STATUS_BLANK)
        LOG_RETURN(-EINVAL, "Cannot do I/O on unformatted medium '%s'",
                   medium->rsc.id.name);
    if (medium->rsc.adm_status != PHO_RSC_ADM_ST_UNLOCKED)
        LOG_RETURN(-EPERM, "Cannot do I/O on an admin locked medium '%s'",
                   medium->rsc.id.name);

    return 0;
}

/******************************************************************************/
/* Request/response manipulation **********************************************/
/******************************************************************************/

static int sched_device_add(struct lrs_sched *sched, enum rsc_family family,
                            const char *name)
{
    struct lrs_dev *device = NULL;
    struct lib_adapter lib;
    int rc = 0;

    rc = lrs_dev_hdl_add(sched, &sched->devices, name);
    if (rc)
        return rc;

    device = lrs_dev_hdl_get(&sched->devices,
                             sched->devices.ldh_devices->len - 1);

    /* get a handle to the library to query it */
    rc = wrap_lib_open(family, &lib);
    if (rc)
        goto dev_del;

    MUTEX_LOCK(&device->ld_mutex);
    rc = sched_fill_dev_info(sched, &lib, device);
    MUTEX_UNLOCK(&device->ld_mutex);
    ldm_lib_close(&lib);
    if (rc)
        goto dev_del;

    return 0;

dev_del:
    lrs_dev_hdl_del(&sched->devices, sched->devices.ldh_devices->len - 1, rc);

    return rc;
}

/**
 * Retry to remove the locked device from the local device array.
 *
 * If the device cannont be removed, will try again later by returning -EAGAIN.
 */
static int sched_device_retry_lock(struct lrs_sched *sched, const char *name,
                                   struct lrs_dev *dev_ptr)
{
    int rc;

    rc = lrs_dev_hdl_retrydel(&sched->devices, dev_ptr);
    if (rc)
        return rc;

    pho_verb("Removed locked device '%s' from the local memory", name);

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
                             struct lrs_dev **dev_ptr)
{
    struct lrs_dev *dev;
    int rc;
    int i;

    for (i = 0; i < sched->devices.ldh_devices->len; ++i) {
        dev = lrs_dev_hdl_get(&sched->devices, i);

        if (!strcmp(name, dev->ld_dss_dev_info->rsc.id.name)) {
            rc = lrs_dev_hdl_trydel(&sched->devices, i);
            if (rc == -EAGAIN) {
                *dev_ptr = dev;
                return rc;
            }

            if (!rc)
                pho_verb("Removed locked device '%s' from the local memory",
                         name);

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
static int sched_device_unlock(struct lrs_sched *sched, const char *name)
{
    struct lrs_dev *dev;
    int i;

    for (i = 0; i < sched->devices.ldh_devices->len; ++i) {
        dev = lrs_dev_hdl_get(&sched->devices, i);

        if (!strcmp(name, dev->ld_dss_dev_info->rsc.id.name)) {
            pho_verb("Updating device '%s' state to unlocked", name);
            dev->ld_dss_dev_info->rsc.adm_status = PHO_RSC_ADM_ST_UNLOCKED;
            return 0;
        }
    }

    pho_verb("Cannot find local device info for '%s', will fetch it "
             "from the database", name);

    return sched_device_add(sched, sched->family, name);
}

/** remove written_size from phys_spc_free in media_info and DSS */
static int update_phys_spc_free(struct dss_handle *dss,
                                struct media_info *dss_media_info,
                                size_t written_size)
{
    if (written_size > 0) {
        dss_media_info->stats.phys_spc_free -= written_size;
        return dss_media_set(dss, dss_media_info, 1, DSS_SET_UPDATE,
                             PHYS_SPC_FREE);
    }

    return 0;
}

int sched_release_enqueue(struct lrs_sched *sched, struct req_container *reqc)
{
    int rc = 0;
    size_t i;
    int rc2;

    /* for each nosync_medium */
    for (i = 0; i < reqc->params.release.n_nosync_media; i++) {
        struct lrs_dev *dev;

        /* find the corresponding device */
        dev = search_loaded_media(sched,
            reqc->params.release.nosync_media[i].medium.name);
        if (dev && thread_is_stopped(&dev->ld_device_thread)) {
            pho_error(-ENODEV, "device '%s' is not running but contains a "
                               "medium not sync'ed '%s'",
                      dev->ld_dss_dev_info->rsc.id.name,
                      reqc->params.release.nosync_media[i].medium.name);
            dev = NULL;
        }

        if (dev == NULL) {
            pho_error(-ENODEV, "Unable to find loaded device of the medium not "
                               "sync'ed '%s'",
                      reqc->params.release.nosync_media[i].medium.name);
            continue;
        }

        /* update media phys_spc_free stats in advance, before next sync */
        /**
         * TODO: we save media status into DSS but we can also use local cached
         * values.
         *
         * Many modifications are needed:
         * - remove sched_load_dev_state from format/read/write
         * - take into account current cached value for loaded media into
         *   sched_select_media
         */
        MUTEX_LOCK(&dev->ld_mutex);
        rc2 = update_phys_spc_free(&sched->sched_thread.dss,
            dev->ld_dss_media_info,
            reqc->params.release.nosync_media[i].written_size);
        MUTEX_UNLOCK(&dev->ld_mutex);
        if (rc2) {
            pho_error(rc2, "Unable to update phys_spc_free");
            /*
             * TODO: returning a fatal error seems to much here,
             * only the medium and the device should be failed
             */
            rc = rc ? : rc2;
        }

        /* Acknowledgement of the request */
        dev->ld_ongoing_io = false;
    }

    if (!reqc->params.release.n_tosync_media) {
        sched_req_free(reqc);
        return rc;
    }

    /* for each tosync_medium */
    for (i = 0; i < reqc->params.release.n_tosync_media; i++) {
        struct lrs_dev *dev;
        int result_rc;

        /* find the corresponding device */
        dev = search_loaded_media(sched,
            reqc->params.release.tosync_media[i].medium.name);

        if (dev && thread_is_stopped(&dev->ld_device_thread)) {
            pho_error(-ENODEV, "device '%s' is not running but contains the "
                               "medium not sync'ed '%s'",
                      dev->ld_dss_dev_info->rsc.id.name,
                      reqc->params.release.nosync_media[i].medium.name);
            dev = NULL;
        }

        if (dev != NULL) {
            /* update media phys_spc_free stats in advance, before next sync */
            /**
             * TODO: better to use a local cached value than always updating DSS
             *
             * Many modifications are needed:
             * - remove sched_load_dev_state from format/read/write
             * - take into account current cached value for loaded media into
             *   sched_select_media
             */
            MUTEX_LOCK(&dev->ld_mutex);
            rc2 = update_phys_spc_free(&sched->sched_thread.dss,
                dev->ld_dss_media_info,
                reqc->params.release.tosync_media[i].written_size);
            MUTEX_UNLOCK(&dev->ld_mutex);
            if (rc2) {
                pho_error(rc2, "Unable to update phys_spc_free");
                /*
                 * TODO: returning a fatal error seems to much here,
                 * only the medium and the device should be failed
                 */
                rc = rc ? : rc2;
            }

            /* Acknowledgement of the request */
            dev->ld_ongoing_io = false;

            /* Queue sync request */
            result_rc = push_new_sync_to_device(dev, reqc, i);

            if (result_rc)
                rc = rc ? : result_rc;
        } else {
            result_rc = -ENODEV;
        }

        MUTEX_LOCK(&reqc->mutex);

        reqc->params.release.rc = result_rc;
        if (!reqc->params.release.rc)
            goto unlock;

        /* manage error */
        reqc->params.release.tosync_media[i].status = SUB_REQUEST_ERROR;
        rc2 = queue_error_response(sched->response_queue,
                                   reqc->params.release.rc,
                                   reqc);
        rc = rc ? : rc2;

        if (i != 0) {
            size_t j;

            for (j = i + 1; j < reqc->params.release.n_tosync_media; j++)
                reqc->params.release.tosync_media[j].status =
                    SUB_REQUEST_CANCEL;
        }

unlock:
        MUTEX_UNLOCK(&reqc->mutex);

        if (reqc->params.release.rc) {
            if (i == 0) {
                /* never queued: we can free it */
                sched_req_free(reqc);
            }

            break;
        }
    }

    return rc;
}

static int push_sub_request_to_device(struct req_container *reqc)
{
    struct lrs_dev **devices = reqc->params.rwalloc.respc->devices;
    size_t devices_len = reqc->params.rwalloc.respc->devices_len;
    struct sub_request **sub_requests;
    size_t i, j;

    sub_requests = malloc(sizeof(*sub_requests) * devices_len);
    if (!sub_requests)
        LOG_RETURN(-ENOMEM, "Unable to allocate sub_requests array to publish");

    for (i = 0; i < devices_len; i++) {
        sub_requests[i] = malloc(sizeof(*sub_requests[i]));
        if (!sub_requests[i])
            LOG_GOTO(sub_request_alloc_error, -ENOMEM,
                     "Unable to allocate a sub request to publish");

        sub_requests[i]->medium_index = i;
        sub_requests[i]->reqc = reqc;
        sub_requests[i]->failure_on_medium = false;
    }

    for (i = 0; i < devices_len; i++)
        devices[i]->ld_sub_request = sub_requests[i];

    free(sub_requests);
    return 0;

sub_request_alloc_error:
    for (j = 0; j < i; j++)
        free(sub_requests[i]);

    free(sub_requests);
    return -ENOMEM;
}

static int publish_or_cancel(struct lrs_sched *sched,
                             struct req_container *reqc, int rc,
                             size_t n_selected)
{
    size_t i;

    if (!rc)
        rc = push_sub_request_to_device(reqc);

    if (rc) {
        for (i = 0; i < n_selected; i++)
            reqc->params.rwalloc.respc->devices[i]->ld_ongoing_io = false;

        if (rc != -EAGAIN) {
            rc = queue_error_response(sched->response_queue, rc, reqc);
            sched_req_free(reqc);
        }
    }

    return rc;
}

static int sched_write_alloc_one_medium(struct lrs_sched *sched,
                                        struct req_container *reqc,
                                        size_t index_to_alloc,
                                        device_select_func_t dev_select_policy,
                                        bool handle_error)
{
    pho_req_write_t *wreq = reqc->req->walloc;
    struct media_info **alloc_medium =
        &reqc->params.rwalloc.media[index_to_alloc].alloc_medium;
    struct lrs_dev *dev;
    struct tags tags;
    size_t size;
    int rc;

    /* Are we retrying to find a new device to an already chosen medium ? */
    if (*alloc_medium)
        goto find_write_device;

    tags.n_tags = wreq->media[index_to_alloc]->n_tags;
    tags.tags = wreq->media[index_to_alloc]->tags;
    size = wreq->media[index_to_alloc]->size;
    /* 1a) is there a mounted filesystem with enough room? */
    dev = dev_picker(sched, PHO_DEV_OP_ST_MOUNTED, dev_select_policy, size,
                     &tags, NULL, true);
    if (dev)
        goto select_device;

    /* 1b) is there a loaded media with enough room? */
    dev = dev_picker(sched, PHO_DEV_OP_ST_LOADED, dev_select_policy, size,
                     &tags, NULL, true);
    if (dev)
        goto select_device;

    /* 2) For the next steps, we need a media to write on.
     * It will be loaded into a free drive.
     * Note: sched_select_media locks the media.
     */
    pho_verb("Not enough space on loaded media: selecting another one");
    rc = sched_select_media(sched, alloc_medium, size, sched->family, &tags,
                            reqc, handle_error ? wreq->n_media : index_to_alloc,
                            index_to_alloc);
    if (rc)
        LOG_RETURN(rc, "Unable to select a new medium in write alloc");

    /**
     * Check if the media is already in a drive.
     *
     * We already look for loaded media with full available size.
     *
     * sched_select_media could find a "split" medium which is already
     * loaded if there is no medium with a enough available size.
     */
    dev = search_loaded_media(sched, (*alloc_medium)->rsc.id.name);
    if (dev) {
        media_info_free(*alloc_medium);
        *alloc_medium = NULL;
        if (!dev->ld_ongoing_io) {
            goto select_device;
        } else {
            pho_info("Selected medium for write is already loaded in a busy "
                     "drive");
            return -EAGAIN;
        }
    }

find_write_device:
    /* 3) choose a suitable drive */
    dev = dev_picker(sched, PHO_DEV_OP_ST_UNSPEC, select_empty_loaded_mount,
                     0, &NO_TAGS, *alloc_medium, false);
    if (dev)
        goto select_device;

    sched_medium_release(sched, *alloc_medium);
    if (compatible_drive_exists(sched, *alloc_medium,
                                *reqc->params.rwalloc.respc->devices,
                                handle_error ? wreq->n_media : index_to_alloc,
                                index_to_alloc)) {
        rc = -EAGAIN;
        pho_info("No device available for write alloc");
    } else {
        pho_error(rc = -ENODEV, "No compatible device found for write alloc");
    }

    media_info_free(*alloc_medium);
    *alloc_medium = NULL;
    return rc;

select_device:
    dev->ld_ongoing_io = true;
    reqc->params.rwalloc.respc->devices[index_to_alloc] = dev;
    return 0;
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
    device_select_func_t dev_select_policy;
    size_t next_medium_index = 0;
    int rc = 0;

    pho_debug("write: allocation request (%ld medias)", wreq->n_media);

    dev_select_policy = get_dev_policy();
    if (!dev_select_policy)
        LOG_GOTO(end, rc = -EINVAL,
                 "Unable to get device select policy during write alloc");

    for (next_medium_index = 0; next_medium_index < wreq->n_media;
         next_medium_index++) {
        rc = sched_write_alloc_one_medium(sched, reqc, next_medium_index,
                                          dev_select_policy, false);
        if (rc)
            break;
    }

end:
    return publish_or_cancel(sched, reqc, rc, next_medium_index);
}

static int skip_read_alloc_medium(int rc, struct req_container *reqc,
                                  size_t index_to_alloc,
                                  size_t next_to_select,
                                  size_t *nb_already_eagain)
{
    size_t *nb_failed = &reqc->params.rwalloc.nb_failed_media;
    pho_rsc_id_t **med_ids = reqc->req->ralloc->med_ids;
    size_t n_required = reqc->req->ralloc->n_required;
    size_t n_med_ids = reqc->req->ralloc->n_med_ids;

    if (rc == -EAGAIN) {
        (*nb_already_eagain)++;
    } else {
        (*nb_failed)++;
        /* Extend failed media by switching the last eagain with the failed */
        if (*nb_already_eagain > 0)
            med_ids_switch(med_ids, index_to_alloc, n_med_ids - *nb_failed);
    }

    /* Extend eagain and failed medium by switching current with last avail */
    if ((n_med_ids - *nb_failed - *nb_already_eagain) >= next_to_select)
        med_ids_switch(med_ids, index_to_alloc,
                       n_med_ids - *nb_failed - *nb_already_eagain);

    /* test if we still have enough candidate */
    if (n_required > (n_med_ids - *nb_already_eagain - *nb_failed)) {
        /* any future chance ? */
        if ((n_med_ids - *nb_failed) >= n_required)
            return -EAGAIN;
        else
            return rc;
    }

    return 0;
}

/**
 * Alloc one more medium to a device in a read alloc request
 */
static int sched_read_alloc_one_medium(struct lrs_sched *sched,
                                       struct req_container *reqc,
                                       size_t index_to_alloc,
                                       size_t next_to_select,
                                       size_t *nb_already_eagain)
{
    pho_req_read_t *rreq = reqc->req->ralloc;
    struct media_info **alloc_medium =
        &reqc->params.rwalloc.media[index_to_alloc].alloc_medium;
    struct pho_id medium_id;
    struct lrs_dev *dev;
    int rc = 0;

    if (*alloc_medium)
        goto find_read_device;

find_read_medium:
    medium_id.family = (enum rsc_family)rreq->med_ids[index_to_alloc]->family;
    rc = pho_id_name_set(&medium_id, rreq->med_ids[index_to_alloc]->name);
    if (rc)
        goto skip_medium;

    rc = sched_fill_media_info(sched, alloc_medium, &medium_id);
    if (rc)
        goto skip_medium;

    rc = check_read_medium_permission_and_status(*alloc_medium);
    if (rc)
        goto free_skip_medium;

find_read_device:
    /* check if the media is already in a drive */
    dev = search_loaded_media(sched, medium_id.name);
    if (dev != NULL) {
        media_info_free(*alloc_medium);
        *alloc_medium = NULL;
        if (dev->ld_ongoing_io) {
            rc = -EAGAIN;
            goto skip_medium;
        } else {
            goto select_device;
        }
    }

    /* lock medium */
    if ((*alloc_medium)->lock.hostname)
        rc = check_renew_lock(sched, DSS_MEDIA, *alloc_medium,
                              &(*alloc_medium)->lock);
    else
        rc = take_and_update_lock(&sched->sched_thread.dss, DSS_MEDIA,
                                  *alloc_medium, &(*alloc_medium)->lock);

    if (rc)
        goto free_skip_medium;

    dev = dev_picker(sched, PHO_DEV_OP_ST_UNSPEC, select_empty_loaded_mount,
                     0, &NO_TAGS, *alloc_medium, false);
    if (dev)
        goto select_device;

    sched_medium_release(sched, *alloc_medium);
    if (compatible_drive_exists(sched, *alloc_medium,
                                *reqc->params.rwalloc.respc->devices,
                                next_to_select, index_to_alloc))
        rc = -EAGAIN;
    else
        rc = -ENODEV;

free_skip_medium:
    media_info_free(*alloc_medium);
    *alloc_medium = NULL;
skip_medium:
    rc = skip_read_alloc_medium(rc, reqc, index_to_alloc, next_to_select,
                                nb_already_eagain);
    if (rc)
        return rc;
    else
        goto find_read_medium;

select_device:
    dev->ld_ongoing_io = true;
    reqc->params.rwalloc.respc->devices[index_to_alloc] = dev;
    return 0;
}

/**
 * Handle a read allocation request by finding the specified media and choose
 * the right device to read them
 */
static int sched_handle_read_alloc(struct lrs_sched *sched,
                                   struct req_container *reqc)
{
    size_t nb_already_eagain = 0;
    int rc = 0;
    size_t i;

    pho_debug("read: allocation request (%ld medias)",
              reqc->req->ralloc->n_med_ids);

    for (i = 0; i < reqc->req->ralloc->n_required; i++) {
        rc = sched_read_alloc_one_medium(sched, reqc, i, i + 1,
                                         &nb_already_eagain);
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

        if (iter->ld_op_status == PHO_DEV_OP_ST_FAILED)
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

    m.family = (enum rsc_family)freq->med_id->family;
    pho_id_name_set(&m, freq->med_id->name);
    rc = sched_fill_media_info(sched, &reqc->params.format.medium_to_format,
                               &m);
    if (rc) {
        if (rc == -EALREADY)
            LOG_GOTO(err_out, rc = -EBUSY,
                     "medium '%s' to format is already locked", m.name);
        else
            goto err_out;
    }

    if (reqc->params.format.medium_to_format->fs.status != PHO_FS_STATUS_BLANK)
        LOG_GOTO(err_out, rc = -EINVAL,
                 "Medium '%s' has a fs.status '%s', expected "
                 "PHO_FS_STATUS_BLANK to be formatted", m.name,
                 fs_status2str(
                    reqc->params.format.medium_to_format->fs.status));

    rc = get_fs_adapter((enum fs_type)freq->fs, &reqc->params.format.fsa);
    if (rc)
        LOG_GOTO(err_out, rc, "Invalid fs_type (%d)", freq->fs);

    if (!format_medium_add(&sched->ongoing_format,
                           reqc->params.format.medium_to_format))
        LOG_GOTO(err_out, rc = -EINVAL,
                 "trying to format the medium '%s' while it is already being "
                 "formatted", m.name);

    device = search_loaded_media(sched, m.name);
    if (device) {
        if (device->ld_ongoing_io || device->ld_needs_sync)
            LOG_GOTO(remove_format_err_out, rc = -EINVAL,
                     "medium %s is already loaded into a busy device %s, "
                     "unexpected state, will abort request",
                     m.name, device->ld_dss_dev_info->rsc.id.name);

        device->ld_ongoing_io = true;
    } else {
        int suitable_devices;

        suitable_devices = count_suitable_devices(sched,
                              reqc->params.format.medium_to_format);
        if (suitable_devices == 0)
            LOG_GOTO(remove_format_err_out, rc = -ENODEV,
                     "No device can format medium '%s', will abort request",
                     m.name);

        /* medium to format isn't already loaded into any drive, need lock */
        if (!reqc->params.format.medium_to_format->lock.hostname) {
            rc = take_and_update_lock(&sched->sched_thread.dss, DSS_MEDIA,
                    reqc->params.format.medium_to_format,
                    &reqc->params.format.medium_to_format->lock);
            if (rc == -EEXIST)
                LOG_GOTO(remove_format_err_out, rc = -EBUSY,
                         "Media '%s' is locked by an other LRS node", m.name);
            else if (rc)
                LOG_GOTO(remove_format_err_out, rc,
                         "Unable to lock the media '%s' to format it", m.name);
        }

        device = dev_picker(sched, PHO_DEV_OP_ST_UNSPEC,
                            select_empty_loaded_mount, 0, &NO_TAGS,
                            reqc->params.format.medium_to_format,
                            false);
        if (!device) {
            pho_verb("Unable to find an available device to format medium '%s'",
                     m.name);
            format_medium_remove(&sched->ongoing_format,
                                 reqc->params.format.medium_to_format);
            return -EAGAIN;
        }
    }

    format_sub_request = malloc(sizeof(*format_sub_request));
    if (!format_sub_request)
        LOG_GOTO(remove_format_err_out, rc = -ENOMEM,
                 "Unable to alloc format sub_request of medium '%s'", m.name);

    format_sub_request->medium_index = 0;
    format_sub_request->reqc = reqc;
    device->ld_sub_request = format_sub_request;
    return 0;

remove_format_err_out:
    format_medium_remove(&sched->ongoing_format,
                         reqc->params.format.medium_to_format);

err_out:
    if (rc != -EAGAIN) {
        int rc2;

        pho_error(rc, "format: failed to schedule format for medium '%s'",
                  m.name);
        rc2 = queue_error_response(sched->response_queue, rc, reqc);
        sched_req_free(reqc);
        if (rc2)
            pho_error(rc2, "Error on sending format error response");

        /*
         * LRS global error only if we face a response error.
         * If there is no response error, rc2 is equal to zero and we set rc to
         * the no error zero value, else we track the response error rc2 into
         * rc.
         */
        rc = rc2;
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
static int queue_notify_response(struct lrs_sched *sched,
                                 struct req_container *reqc)
{
    pho_req_notify_t *nreq = reqc->req->notify;
    struct resp_container *respc;
    pho_resp_t *resp;
    int rc;

    respc = malloc(sizeof(*respc));
    if (respc == NULL)
        return -errno;

    respc->socket_id = reqc->socket_id;

    respc->resp = malloc(sizeof(*respc->resp));
    if (respc->resp == NULL) {
        rc = -errno;
        free(respc);
        return rc;
    }

    rc = pho_srl_response_notify_alloc(respc->resp);
    if (rc) {
        free(respc->resp);
        free(respc);
        return rc;
    }

    resp = respc->resp;

    resp->req_id = reqc->req->id;
    resp->notify->rsrc_id->family = nreq->rsrc_id->family;
    resp->notify->rsrc_id->name = strdup(nreq->rsrc_id->name);

    tsqueue_push(sched->response_queue, respc);

    return 0;
}

/* reqc is freed unless -EAGAIN is returned */
static int sched_handle_notify(struct lrs_sched *sched,
                               struct req_container *reqc)
{
    pho_req_notify_t *nreq = reqc->req->notify;
    struct lrs_dev *dev;
    int rc = 0;

    pho_info("Notify: device '%s'", nreq->rsrc_id->name);

    switch (nreq->op) {
    case PHO_NTFY_OP_DEVICE_ADD:
        rc = sched_device_add(sched, (enum rsc_family)nreq->rsrc_id->family,
                              nreq->rsrc_id->name);
        break;
    case PHO_NTFY_OP_DEVICE_LOCK:
        dev = reqc->params.notify.notified_device;

        if (dev == NULL) {
            rc = sched_device_lock(sched, nreq->rsrc_id->name, &dev);
            if (rc == -EAGAIN)
                reqc->params.notify.notified_device = dev;
        } else {
            rc = sched_device_retry_lock(sched, nreq->rsrc_id->name, dev);
        }
        break;
    case PHO_NTFY_OP_DEVICE_UNLOCK:
        rc = sched_device_unlock(sched, nreq->rsrc_id->name);
        break;
    default:
        LOG_GOTO(err, rc = -EINVAL, "The requested operation is not "
                 "recognized");
    }

    if (!nreq->wait) {
        if (rc != 0 && rc != -EAGAIN) {
            pho_error(rc, "Notify failed for '%s'", nreq->rsrc_id->name);
            rc = 0;
        }

        return rc;
    }

    if (rc)
        goto err;

    rc = queue_notify_response(sched, reqc);
    if (rc)
        goto err;

    sched_req_free(reqc);
    return 0;

err:
    if (rc != -EAGAIN) {
        rc = queue_error_response(sched->response_queue, rc, reqc);
        sched_req_free(reqc);
    }

    return rc;
}

void rwalloc_cancel_DONE_devices(struct req_container *reqc)
{
    bool is_write = pho_request_is_write(reqc->req);
    size_t i;

    for (i = 0; i < reqc->params.rwalloc.n_media; i++) {
        if (reqc->params.rwalloc.media[i].status == SUB_REQUEST_DONE) {
            struct resp_container *respc = reqc->params.rwalloc.respc;
            pho_resp_t *resp = respc->resp;

            reqc->params.rwalloc.media[i].status = SUB_REQUEST_CANCEL;
            respc->devices[i]->ld_ongoing_io = false;
            respc->devices[i] = NULL;
            if (is_write) {
                pho_resp_write_elt_t *wresp = resp->walloc->media[i];

                free(wresp->root_path);
                wresp->root_path = NULL;
                free(wresp->med_id->name);
                wresp->med_id->name = NULL;
            } else {
                pho_resp_read_elt_t *rresp = resp->ralloc->media[i];

                free(rresp->root_path);
                rresp->root_path = NULL;
                free(rresp->med_id->name);
                rresp->med_id->name = NULL;
            }
        }
    }
}

/**
 * Called with a lock on sreq->reqc
 */
static int sched_handle_read_or_write_error(struct lrs_sched *sched,
                                            struct sub_request *sreq,
                                            bool *sreq_pushed_or_requeued,
                                            bool *req_ended)
{
    struct rwalloc_params *rwalloc = &sreq->reqc->params.rwalloc;
    int rc = 0;

    *sreq_pushed_or_requeued = false;
    *req_ended = false;
    if (pho_request_is_read(sreq->reqc->req)) {
        size_t nb_already_eagain = 0;

        rc = sched_read_alloc_one_medium(sched, sreq->reqc, sreq->medium_index,
                                         rwalloc->n_media,
                                         &nb_already_eagain);
    } else {
        device_select_func_t dev_select_policy;

        dev_select_policy = get_dev_policy();
        if (!dev_select_policy) {
            pho_error(rc = -EINVAL,
                      "Unable to get device select policy at write error");
        } else {
            rc = sched_write_alloc_one_medium(sched, sreq->reqc,
                                              sreq->medium_index,
                                              dev_select_policy, true);
        }
    }

    if (!rc) {
        rwalloc->respc->devices[sreq->medium_index]->ld_sub_request = sreq;
        *sreq_pushed_or_requeued = true;
    } else {
        if (rc == -EAGAIN) {
            tsqueue_push(&sched->retry_queue, sreq);
            rc = 0;
            *sreq_pushed_or_requeued = true;
        } else {
            *sreq_pushed_or_requeued = false;
            rwalloc->rc = rc;
            rwalloc->media[sreq->medium_index].status = SUB_REQUEST_ERROR;
            rc = queue_error_response(sched->response_queue, -ESHUTDOWN,
                                      sreq->reqc);
            rwalloc_cancel_DONE_devices(sreq->reqc);
            *req_ended = is_rwalloc_ended(sreq->reqc);
        }
    }

    return rc;
}

static int sched_handle_error(struct lrs_sched *sched, struct sub_request *sreq)
{
    struct req_container *reqc = sreq->reqc;
    bool sreq_pushed_or_requeued = false;
    bool req_ended = false;
    int rc = 0;

    MUTEX_LOCK(&reqc->mutex);
    if (locked_cancel_rwalloc_on_error(sreq, &req_ended))
        goto end_handle_error;

    if (!running) {
        struct rwalloc_medium *rwalloc_medium;

        rwalloc_medium = &reqc->params.rwalloc.media[sreq->medium_index];
        reqc->params.rwalloc.rc = -ESHUTDOWN;
        rwalloc_medium->status = SUB_REQUEST_ERROR;
        media_info_free(rwalloc_medium->alloc_medium);
        rwalloc_medium->alloc_medium = NULL;
        rc = queue_error_response(sched->response_queue, -ESHUTDOWN, reqc);
        rwalloc_cancel_DONE_devices(reqc);
        req_ended = is_rwalloc_ended(reqc);
        goto end_handle_error;
    }

    /**
     * At this time, only read and write use the error queue.
     * Format are still requeued through the request_requeue and must be
     * requeued through the retry_queue.
     */
    rc = sched_handle_read_or_write_error(sched, sreq, &sreq_pushed_or_requeued,
                                          &req_ended);
end_handle_error:
    MUTEX_UNLOCK(&reqc->mutex);
    if (!sreq_pushed_or_requeued) {
        if (!req_ended)
            sreq->reqc = NULL;

        sub_request_free(sreq);
    }

    return rc;
}

int sched_handle_requests(struct lrs_sched *sched)
{
    struct req_container *reqc;
    struct sub_request *sreq;
    int rc = 0;

    /**
     * First try to re-run sub-request errors
     */
    while ((sreq = tsqueue_pop(&sched->retry_queue)) != NULL) {
        rc = sched_handle_error(sched, sreq);
        if (rc)
            return rc;
    }

    /*
     * Very simple algorithm (FIXME): serve requests until the first EAGAIN is
     * encountered.
     */
    while ((reqc = tsqueue_pop(&sched->req_queue)) != NULL) {
        pho_req_t *req = reqc->req;

        if (!running) {
            rc = queue_error_response(sched->response_queue, -ESHUTDOWN, reqc);
            sched_req_free(reqc);
            if (rc)
                break;
        } else if (pho_request_is_format(req)) {
            pho_debug("lrs received format request (%p)", req);
            rc = sched_handle_format(sched, reqc);
        } else if (pho_request_is_notify(req)) {
            pho_debug("lrs received notify request (%p)", req);
            rc = sched_handle_notify(sched, reqc);
        } else if (pho_request_is_write(req)) {
            pho_debug("lrs received write request (%p)", req);
            rc = sched_handle_write_alloc(sched, reqc);
        } else if (pho_request_is_read(req)) {
            pho_debug("lrs received read allocation request (%p)", req);
            rc = sched_handle_read_alloc(sched, reqc);
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

        if (running) {
            /* Requeue last request on -EAGAIN and running */
            tsqueue_push(&sched->req_queue, reqc);
            rc = 0;
            break;
        }

        /* create an -ESHUTDOWN error on -EAGAIN and !running */
        if (!pho_request_is_notify(reqc->req) || reqc->req->notify->wait) {
            rc = queue_error_response(sched->response_queue, -ESHUTDOWN, reqc);
            if (rc) {
                sched_req_free(reqc);
                break;
            }
        }

        sched_req_free(reqc);
    }

    return rc;
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

static void sched_fetch_device_status(struct lrs_dev *device,
                                      json_t *device_status)
{
    json_t *integer;

    _json_object_set_str(device_status, "name", device->ld_dss_dev_info->path);
    _json_object_set_str(device_status, "device", device->ld_dev_path);
    _json_object_set_str(device_status, "serial",
                         device->ld_sys_dev_state.lds_serial);

    integer = json_integer(device->ld_lib_dev_info.ldi_addr.lia_addr -
                           device->ld_lib_dev_info.ldi_first_addr);
    if (integer) {
        json_object_set(device_status, "address", integer);
        json_decref(integer);
    }

    if (device->ld_dss_media_info) {
        json_t *ongoing_io;

        _json_object_set_str(device_status, "mount_path", device->ld_mnt_path);
        _json_object_set_str(device_status, "media",
                             device->ld_dss_media_info->rsc.id.name);

        ongoing_io = json_boolean(device->ld_ongoing_io);
        if (ongoing_io) {
            json_object_set(device_status, "ongoing_io", ongoing_io);
            json_decref(ongoing_io);
        }
    }
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

static void *lrs_sched_thread(void *sdata)
{
    struct lrs_sched *sched = (struct lrs_sched *) sdata;
    struct thread_info *thread = &sched->sched_thread;
    int rc;

    while (thread_is_running(thread)) {
        rc = thread_signal_wait(thread);
        if (rc < 0)
            LOG_GOTO(end_thread, thread->status = rc,
                     "sched thread '%d': fatal error", sched->family);
    }

end_thread:
    thread->state = THREAD_STOPPED;
    pthread_exit(&thread->status);
}
