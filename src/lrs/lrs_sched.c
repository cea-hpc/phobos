/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2019 CEA/DAM.
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
#include "pho_type_utils.h"

#include <assert.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <jansson.h>

#define TAPE_TYPE_SECTION_CFG "tape_type \"%s\""
#define MODELS_CFG_PARAM "models"
#define DRIVE_RW_CFG_PARAM "drive_rw"
#define DRIVE_TYPE_SECTION_CFG "drive_type \"%s\""

static int sched_handle_release_reqs(struct lrs_sched *sched,
                                     GArray *resp_array);
static void sched_req_free_wrapper(void *req);
static void sched_resp_free_wrapper(void *resp);

enum sched_operation {
    LRS_OP_NONE = 0,
    LRS_OP_READ,
    LRS_OP_WRITE,
    LRS_OP_FORMAT,
};

/**
 * Build a mount path for the given identifier.
 * @param[in] id    Unique drive identified on the host.
 * The result must be released by the caller using free(3).
 */
static char *mount_point(const char *id)
{
    const char  *mnt_cfg;
    char        *mnt_out;

    mnt_cfg = PHO_CFG_GET(cfg_lrs, PHO_CFG_LRS, mount_prefix);
    if (mnt_cfg == NULL)
        return NULL;

    /* mount the device as PHO_MNT_PREFIX<id> */
    if (asprintf(&mnt_out, "%s%s", mnt_cfg, id) < 0)
        return NULL;

    return mnt_out;
}

/** all needed information to select devices */
struct dev_descr {
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
    bool                 locked_local;          /**< dss lock acquired by us */
};

/* Needed local function declarations */
static struct dev_descr *search_loaded_media(struct lrs_sched *sched,
                                             const char *name);

/** check that device info from DB is consistent with actual status */
static int check_dev_info(const struct dev_descr *dev)
{
    ENTRY;

    if (dev->dss_dev_info->rsc.model == NULL
        || dev->sys_dev_state.lds_model == NULL) {
        if (dev->dss_dev_info->rsc.model != dev->sys_dev_state.lds_model)
            LOG_RETURN(-EINVAL, "%s: missing or unexpected device model",
                       dev->dev_path);
        else
            pho_debug("%s: no device model is set", dev->dev_path);

    } else if (cmp_trimmed_strings(dev->dss_dev_info->rsc.model,
                                   dev->sys_dev_state.lds_model)) {
        LOG_RETURN(-EINVAL, "%s: configured device model '%s' differs from "
                   "actual device model '%s'", dev->dev_path,
                   dev->dss_dev_info->rsc.model, dev->sys_dev_state.lds_model);
    }

    if (dev->sys_dev_state.lds_serial == NULL) {
        if (dev->dss_dev_info->rsc.id.name != dev->sys_dev_state.lds_serial)
            LOG_RETURN(-EINVAL, "%s: missing or unexpected device serial",
                       dev->dss_dev_info->path);
        else
            pho_debug("%s: no device serial is set", dev->dev_path);
    } else if (strcmp(dev->dss_dev_info->rsc.id.name,
                      dev->sys_dev_state.lds_serial) != 0) {
        LOG_RETURN(-EINVAL, "%s: configured device serial '%s' differs from "
                   "actual device serial '%s'", dev->dev_path,
                   dev->dss_dev_info->rsc.id.name,
                   dev->sys_dev_state.lds_serial);
    }

    return 0;
}

/**
 * Lock a device at DSS level to prevent concurrent access.
 */
static int sched_dev_acquire(struct lrs_sched *sched, struct dev_descr *pdev)
{
    int rc;

    ENTRY;

    if (!pdev)
        return -EINVAL;

    if (pdev->locked_local) {
        pho_debug("Device '%s' already locked (ignoring)", pdev->dev_path);
        return 0;
    }

    rc = dss_lock(&sched->dss, DSS_DEVICE, pdev->dss_dev_info, 1);
    if (rc) {
        pho_warn("Cannot lock device '%s': %s", pdev->dev_path,
                 strerror(-rc));
        return rc;
    }

    pho_debug("Acquired ownership on device '%s'", pdev->dev_path);
    pdev->locked_local = true;

    return 0;
}

/**
 * Unlock a device at DSS level.
 */
static int sched_dev_release(struct lrs_sched *sched, struct dev_descr *pdev)
{
    int rc;

    ENTRY;

    if (!pdev)
        return -EINVAL;

    if (!pdev->locked_local) {
        pho_debug("Device '%s' is not locked (ignoring)", pdev->dev_path);
        return 0;
    }

    rc = dss_unlock(&sched->dss, DSS_DEVICE, pdev->dss_dev_info, 1, false);
    if (rc)
        LOG_RETURN(rc, "Cannot unlock device '%s'", pdev->dev_path);

    pho_debug("Released ownership on device '%s'", pdev->dev_path);
    pdev->locked_local = false;

    return 0;
}

/**
 * Lock a media at DSS level to prevent concurrent access.
 */
static int sched_media_acquire(struct lrs_sched *sched,
                               struct media_info *pmedia)
{
    int          rc;

    ENTRY;

    if (!pmedia)
        return -EINVAL;

    rc = dss_lock(&sched->dss, DSS_MEDIA, pmedia, 1);
    if (rc) {
        pmedia->lock.is_external = true;
        LOG_RETURN(rc, "Cannot lock media '%s'", pmedia->rsc.id.name);
    }

    pho_debug("Acquired ownership on media '%s'", pmedia->rsc.id.name);
    return 0;
}

/**
 * Unlock a media at DSS level.
 */
static int sched_media_release(struct lrs_sched *sched,
                               struct media_info *pmedia)
{
    int          rc;

    ENTRY;

    if (!pmedia)
        return -EINVAL;

    rc = dss_unlock(&sched->dss, DSS_MEDIA, pmedia, 1, false);
    if (rc)
        LOG_RETURN(rc, "Cannot unlock media '%s'", pmedia->rsc.id.name);

    pho_debug("Released ownership on media '%s'", pmedia->rsc.id.name);
    return 0;
}

/**
 * False if the device is locked or if it contains a locked media,
 * true otherwise.
 */
static bool dev_is_available(const struct dev_descr *devd)
{
    if (devd->locked_local) {
        pho_debug("'%s' is locked\n", devd->dev_path);
        return false;
    }

    /* Test if the contained media lock is taken by another phobos */
    if (devd->dss_media_info != NULL &&
        devd->dss_media_info->lock.is_external) {
        pho_debug("'%s' contains a locked media", devd->dev_path);
        return false;
    }
    return true;
}

/**
 * Retrieve media info from DSS for the given ID.
 * @param pmedia[out] returned pointer to a media_info structure
 *                    allocated by this function.
 * @param id[in]      ID of the media.
 */
static int sched_fill_media_info(struct dss_handle *dss,
                                 struct media_info **pmedia,
                                 const struct pho_id *id)
{
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
    } else if (mcnt > 1)
        LOG_GOTO(out_free, rc = -EINVAL,
                 "Too many media found matching id '%s'", id->name);

    media_info_free(*pmedia);
    *pmedia = media_info_dup(media_res);

    /* If the lock is already taken, mark it as externally locked */
    if ((*pmedia)->lock.hostname) {
        pho_info("Media '%s' is locked (%d)", id->name, (*pmedia)->lock.owner);
        (*pmedia)->lock.is_external = true;
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
 * @param[out] devd dev_descr structure filled with all needed information.
 */
static int sched_fill_dev_info(struct dss_handle *dss, struct lib_adapter *lib,
                               struct dev_descr *devd)
{
    struct dev_adapter deva;
    struct dev_info   *devi;
    int                rc;

    ENTRY;

    if (devd == NULL)
        return -EINVAL;

    devi = devd->dss_dev_info;

    media_info_free(devd->dss_media_info);
    devd->dss_media_info = NULL;

    rc = get_dev_adapter(devi->rsc.id.family, &deva);
    if (rc)
        return rc;

    /* get path for the given serial */
    rc = ldm_dev_lookup(&deva, devi->rsc.id.name, devd->dev_path,
                        sizeof(devd->dev_path));
    if (rc) {
        pho_debug("Device lookup failed: serial '%s'", devi->rsc.id.name);
        return rc;
    }

    /* now query device by path */
    ldm_dev_state_fini(&devd->sys_dev_state);
    rc = ldm_dev_query(&deva, devd->dev_path, &devd->sys_dev_state);
    if (rc) {
        pho_debug("Failed to query device '%s'", devd->dev_path);
        return rc;
    }

    /* compare returned device info with info from DB */
    rc = check_dev_info(devd);
    if (rc)
        return rc;

    /* Query the library about the drive location and whether it contains
     * a media.
     */
    rc = ldm_lib_drive_lookup(lib, devi->rsc.id.name, &devd->lib_dev_info);
    if (rc) {
        pho_debug("Failed to query the library about device '%s'",
                  devi->rsc.id.name);
        return rc;
    }

    if (devd->lib_dev_info.ldi_full) {
        struct pho_id *medium_id;
        struct fs_adapter fsa;

        devd->op_status = PHO_DEV_OP_ST_LOADED;
        medium_id = &devd->lib_dev_info.ldi_medium_id;

        pho_debug("Device '%s' (S/N '%s') contains medium '%s'", devd->dev_path,
                  devi->rsc.id.name, medium_id->name);

        /* get media info for loaded drives */
        rc = sched_fill_media_info(dss, &devd->dss_media_info, medium_id);

        /*
         * If the drive is marked as locally locked, the contained media was in
         * fact locked by us, this happens when the raid1 layout refreshes the
         * drive list.
         */
        if (devd->locked_local && devd->dss_media_info &&
            devd->dss_media_info->lock.is_external) {
            devd->dss_media_info->lock.is_external = false;
        }

        /* Medium hasn't been found, mark the device as unusable */
        if (rc == -ENXIO)
            devd->op_status = PHO_DEV_OP_ST_FAILED;
        else if (rc)
            return rc;
        else {
            /* See if the device is currently mounted */
            rc = get_fs_adapter(devd->dss_media_info->fs.type, &fsa);
            if (rc)
                return rc;

            /* If device is loaded, check if it is mounted as a filesystem */
            rc = ldm_fs_mounted(&fsa, devd->dev_path, devd->mnt_path,
                                sizeof(devd->mnt_path));

            if (rc == 0) {
                pho_debug("Discovered mounted filesystem at '%s'",
                          devd->mnt_path);
                devd->op_status = PHO_DEV_OP_ST_MOUNTED;
            } else if (rc == -ENOENT)
                /* not mounted, not an error */
                rc = 0;
            else
                LOG_RETURN(rc, "Cannot determine if device '%s' is mounted",
                           devd->dev_path);
        }
    } else {
        devd->op_status = PHO_DEV_OP_ST_EMPTY;
    }

    pho_debug("Drive '%s' is '%s'", devd->dev_path,
              op_status2str(devd->op_status));

    return rc;
}

/** Wrap library open operations
 * @param[out] lib  Library handler.
 */
static int wrap_lib_open(enum rsc_family dev_type, struct lib_adapter *lib)
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

/**
 * Load device states into memory.
 * Do nothing if device status is already loaded.
 */
static int sched_load_dev_state(struct lrs_sched *sched)
{
    struct dev_info    *devs = NULL;
    int                 dcnt = 0;
    struct lib_adapter  lib;
    int                 i;
    int                 rc;

    ENTRY;

    if (sched->family == PHO_RSC_INVAL)
        return -EINVAL;

    /* If no device has previously been loaded, load the list of available
     * devices from DSS; otherwise just refresh informations for the current
     * list of devices
     */
    if (sched->devices->len == 0) {
        struct dss_filter   filter;

        rc = dss_filter_build(&filter,
                              "{\"$AND\": ["
                              "  {\"DSS::DEV::host\": \"%s\"},"
                              "  {\"DSS::DEV::adm_status\": \"%s\"},"
                              "  {\"DSS::DEV::family\": \"%s\"}"
                              "]}",
                              get_hostname(),
                              rsc_adm_status2str(PHO_RSC_ADM_ST_UNLOCKED),
                              rsc_family2str(sched->family));
        if (rc)
            return rc;

        /* get all unlocked devices from DB for the given family */
        rc = dss_device_get(&sched->dss, &filter, &devs, &dcnt);
        dss_filter_free(&filter);
        if (rc)
            GOTO(err_no_res, rc);

        if (dcnt == 0) {
            pho_info("No usable device found (%s): check devices status",
                     rsc_family2str(sched->family));
            GOTO(err, rc = -ENXIO);
        }

        g_array_set_size(sched->devices, dcnt);

        /* Copy information from DSS to local device list */
        for (i = 0 ; i < dcnt; i++) {
            struct dev_descr *dev = &g_array_index(sched->devices,
                                                   struct dev_descr, i);
            dev->dss_dev_info = dev_info_dup(&devs[i]);
        }
    }

    /* get a handle to the library to query it */
    rc = wrap_lib_open(sched->family, &lib);
    if (rc)
        GOTO(err, rc);

    for (i = 0 ; i < sched->devices->len; i++) {
        struct dev_descr *dev = &g_array_index(sched->devices,
                                               struct dev_descr, i);
        rc = sched_fill_dev_info(&sched->dss, &lib, dev);
        if (rc) {
            pho_debug("Marking device '%s' as failed", dev->dev_path);
            dev->op_status = PHO_DEV_OP_ST_FAILED;
        }
    }

    /* close handle to the library */
    ldm_lib_close(&lib);

    rc = 0;

err:
    /* free devs array, as they have been copied to devices[].device */
    dss_res_free(devs, dcnt);
err_no_res:
    return rc;
}

static void dev_descr_fini(gpointer ptr)
{
    struct dev_descr *dev = (struct dev_descr *)ptr;
    dev_info_free(dev->dss_dev_info, true);
    dev->dss_dev_info = NULL;

    media_info_free(dev->dss_media_info);
    dev->dss_media_info = NULL;

    ldm_dev_state_fini(&dev->sys_dev_state);
}

/**
 * Checks the lock host is the same as the daemon instance.
 *
 * @param   sched       Scheduler handle, contains the daemon lock string.
 * @param   lock        Lock string to compare to the daemon one.
 * @return              true if hosts are the same,
 *                      false otherwise.
 */
static bool compare_lock_hosts(struct lrs_sched *sched, const char *lock)
{
    if (!lock)
        return false;

    /* Compare the sched->lock_hostname with the given lock */
    return !strcmp(sched->lock_hostname, lock);
}

/**
 * Unlocks all devices that were locked by a previous instance on this host.
 *
 * We consider that for a given configuration and so a given database, it can
 * only exists one daemon instance at a time on a host. If two instances exist
 * at the same time on one host, they need to be related to different databases.
 *
 * @param   sched       Scheduler handle.
 * @return              0 on success,
 *                      first encountered negative posix error on failure.
 */
static int sched_check_device_locks(struct lrs_sched *sched)
{
    int rc = 0;
    int i;

    ENTRY;

    for (i = 0; i < sched->devices->len; ++i) {
        struct dev_info *dev;
        int rc2;

        dev = g_array_index(
            sched->devices, struct dev_descr, i).dss_dev_info;

        if (!compare_lock_hosts(sched, dev->lock.hostname))
            continue;

        pho_info("Device '%s' was previously locked by this host, releasing it",
                 dev->path);
        rc2 = dss_unlock(&sched->dss, DSS_DEVICE, dev, 1, true);
        if (rc2) {
            pho_error(rc2, "Device '%s' could not be unlocked", dev->path);
            rc = rc ? : rc2;
        }
    }

    return rc;
}

/**
 * Unlocks all media that were locked by a previous instance on this host.
 *
 * We consider that for a given configuration and so a given database, it can
 * only exists one daemon instance at a time on a host. If two instances exist
 * at the same time on one host, they need to be related to different databases.
 *
 * @param   sched       Scheduler handle.
 * @return              0 on success,
 *                      first encountered negative posix error on failure.
 */
static int sched_check_medium_locks(struct lrs_sched *sched)
{
    struct media_info *media = NULL;
    struct dss_filter filter;
    int mcnt = 0;
    int rc = 0;
    int i;

    ENTRY;

    // get all media of the right family
    rc = dss_filter_build(&filter, "{\"DSS::MDA::family\": \"%s\"}",
                          rsc_family2str(sched->family));
    if (rc) {
        pho_error(rc, "Filter build failed");
        return rc;
    }

    rc = dss_media_get(&sched->dss, &filter, &media, &mcnt);
    dss_filter_free(&filter);
    if (rc) {
        pho_error(rc, "Media could not be retrieved from the database");
        return rc;
    }

    // for all check if the lock is from this host
    for (i = 0; i < mcnt; ++i) {
        int rc2;

        if (!compare_lock_hosts(sched, media[i].lock.hostname))
            continue;

        // if it is, unlock it
        pho_info("Medium '%s' was previously locked by this host, releasing it",
                 media[i].rsc.id.name);
        rc2 = dss_unlock(&sched->dss, DSS_MEDIA, &media[i], 1, true);
        if (rc2) {
            pho_error(rc2, "Medium '%s' could not be unlocked",
                      media[i].rsc.id.name);
            rc = rc ? : rc2;
        }
    }

    dss_res_free(media, mcnt);

    return rc;
}

int sched_init(struct lrs_sched *sched, enum rsc_family family)
{
    int rc;

    sched->family = family;

    rc = fill_host_owner(&sched->lock_hostname, &sched->lock_owner);
    if (rc)
        LOG_RETURN(rc, "Failed to get LRS hostname");

    /* Connect to the DSS */
    rc = dss_init(&sched->dss);
    if (rc)
        return rc;

    sched->devices = g_array_new(FALSE, TRUE, sizeof(struct dev_descr));
    g_array_set_clear_func(sched->devices, dev_descr_fini);
    sched->req_queue = g_queue_new();
    sched->release_queue = g_queue_new();

    /* Load the device state -- not critical if no device is found */
    sched_load_dev_state(sched);

    rc = sched_check_device_locks(sched);
    if (rc) {
        sched_fini(sched);
        return rc;
    }

    rc = sched_check_medium_locks(sched);
    if (rc) {
        sched_fini(sched);
        return rc;
    }

    return 0;
}

void sched_fini(struct lrs_sched *sched)
{
    if (sched == NULL)
        return;

    /* Handle all pending release requests */
    sched_handle_release_reqs(sched, NULL);

    dss_fini(&sched->dss);

    g_queue_free_full(sched->req_queue, sched_req_free_wrapper);
    g_queue_free_full(sched->release_queue, sched_req_free_wrapper);
    g_array_free(sched->devices, TRUE);
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

static bool medium_in_devices(const struct media_info *medium,
                                  struct dev_descr **devs, size_t n_dev)
{
    size_t i;

    for (i = 0; i < n_dev; i++) {
        if (devs[i]->dss_media_info == NULL)
            continue;
        if (pho_id_equal(&medium->rsc.id, &devs[i]->dss_media_info->rsc.id))
            return true;
    }

    return false;
}

/**
 * Get a suitable medium for a write operation.
 * @param[in]  sched         Current scheduler
 * @param[out] p_media       Selected medium
 * @param[in]  required_size Size of the extent to be written.
 * @param[in]  family        Medium family from which getting the medium
 * @param[in]  tags          Tags used to filter candidate media, the
 *                           selected medium must have all the specified tags.
 * @param[in]  devs          Array of selected devices to write with.
 * @param[in]  n_dev         Nb in devs of already allocated devices with loaded
 *                           and mounted media
 */
static int sched_select_media(struct lrs_sched *sched,
                              struct media_info **p_media, size_t required_size,
                              enum rsc_family family, const struct tags *tags,
                              struct dev_descr **devs, size_t n_dev)
{
    struct media_info   *pmedia_res = NULL;
    struct media_info   *split_media_best;
    size_t               avail_size;
    struct media_info   *whole_media_best;
    struct media_info   *chosen_media;
    struct dss_filter    filter;
    char                *tag_filter_json = NULL;
    bool                 with_tags = tags != NULL && tags->n_tags > 0;
    bool                 full_avail_media = false;
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
                          /* Check get media operation flags */
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

    rc = dss_media_get(&sched->dss, &filter, &pmedia_res, &mcnt);
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

        /* exclude medium already booked for this allocation */
        if (medium_in_devices(curr, devs, n_dev))
            continue;

        avail_size += curr->stats.phys_spc_free;

        if ((split_media_best == NULL ||
            curr->stats.phys_spc_free > split_media_best->stats.phys_spc_free)
            && !curr->lock.is_external)
            split_media_best = curr;

        if (curr->stats.phys_spc_free < required_size)
            continue;

        if (whole_media_best == NULL ||
            curr->stats.phys_spc_free < whole_media_best->stats.phys_spc_free) {
            /* Remember that at least one fitting media has been found */
            full_avail_media = true;

            /* The media is already locked, continue searching */
            if (curr->lock.is_external)
                continue;

            whole_media_best = curr;
        }
    }

    if (avail_size < required_size) {
        pho_warn("Available space on media : %zd, required size : %zd",
                 avail_size, required_size);
        GOTO(free_res, rc = -ENOSPC);
    }

    if (whole_media_best != NULL)
        chosen_media = whole_media_best;
    else if (full_avail_media) {
        pho_info("Wait an existing compatible medium with full available size");
        GOTO(free_res, rc = -EAGAIN);
    } else if (split_media_best != NULL) {
        chosen_media = split_media_best;
        pho_info("Split %zd required_size on %zd avail size on %s medium",
                 required_size, chosen_media->stats.phys_spc_free,
                 chosen_media->rsc.id.name);
    } else {
        pho_info("No medium available, wait for one");
        GOTO(free_res, rc = -EAGAIN);
    }

    pho_debug("Acquiring selected media '%s'", chosen_media->rsc.id.name);
    rc = sched_media_acquire(sched, chosen_media);
    if (rc) {
        pho_debug("Failed to lock media '%s', looking for another one",
                  chosen_media->rsc.id.name);
        goto lock_race_retry;
    }

    pho_verb("Selected %s '%s': %zd bytes free", rsc_family2str(family),
             chosen_media->rsc.id.name,
             chosen_media->stats.phys_spc_free);

    *p_media = media_info_dup(chosen_media);
    if (*p_media == NULL)
        GOTO(free_res, rc = -ENOMEM);

    rc = 0;

free_res:
    if (rc != 0)
        sched_media_release(sched, chosen_media);

    dss_res_free(pmedia_res, mcnt);

err_nores:
    dss_filter_free(&filter);
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
                             const struct dev_descr *drive, bool *res)
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

        rc = search_in_list(drive_model_list, drive->dss_dev_info->rsc.model,
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
                                    struct dev_descr *dev_curr,
                                    struct dev_descr **dev_selected);

/**
 * Select a device according to a given status and policy function.
 * Returns a device in locked state; if a media is in the device, the media is
 * locked first.
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
static struct dev_descr *dev_picker(struct lrs_sched *sched,
                                    enum dev_op_status op_st,
                                    device_select_func_t select_func,
                                    size_t required_size,
                                    const struct tags *media_tags,
                                    struct media_info *pmedia)
{
    struct dev_descr    *selected = NULL;
    int                  selected_i = -1;
    int                  i;
    int                  rc;
    /*
     * lazily allocated array to remember which devices were unsuccessfully
     * acquired
     */
    bool                *failed_dev = NULL;

    ENTRY;

retry:
    for (i = 0; i < sched->devices->len; i++) {
        struct dev_descr *itr = &g_array_index(sched->devices,
                                               struct dev_descr, i);
        struct dev_descr *prev = selected;

        /* Already unsuccessfully tried to acquire this device */
        if (failed_dev && failed_dev[i])
            continue;

        if (!dev_is_available(itr)) {
            pho_debug("Skipping locked or busy device '%s'", itr->dev_path);
            continue;
        }

        if (op_st != PHO_DEV_OP_ST_UNSPEC && itr->op_status != op_st) {
            pho_debug("Skipping device '%s' with incompatible status %s",
                      itr->dev_path, op_status2str(itr->op_status));
            continue;
        }

        /*
         * The intent is to write: exclude media that are administratively
         * locked, full, do not have the put operation flag and do not have the
         * requested tags
         */
        /*
         * @TODO: using the size arg to check if the requested action is a write
         * seems to be error prone for object with a zero size content
         */
        if (required_size > 0 && itr->dss_media_info) {
            if (itr->dss_media_info->rsc.adm_status !=
                    PHO_RSC_ADM_ST_UNLOCKED) {
                pho_debug("Media '%s' is not unlocked",
                          itr->dss_media_info->rsc.id.name);
                continue;
            }

            if (itr->dss_media_info->fs.status == PHO_FS_STATUS_FULL) {
                pho_debug("Media '%s' is full",
                          itr->dss_media_info->rsc.id.name);
                continue;
            }

            if (!itr->dss_media_info->flags.put) {
                pho_debug("Media '%s' has a false put operation flag",
                          itr->dss_media_info->rsc.id.name);
                continue;
            }

            if (!tags_in(&itr->dss_media_info->tags, media_tags)) {
                pho_debug("Media '%s' does not match required tags",
                          itr->dss_media_info->rsc.id.name);
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
        struct media_info *local_pmedia = selected->dss_media_info;

        pho_debug("Picked dev number %d (%s)", selected_i, selected->dev_path);
        rc = 0;
        if (local_pmedia != NULL) {
            pho_debug("Acquiring %s media '%s'",
                      op_status2str(selected->op_status),
                      local_pmedia->rsc.id.name);
            rc = sched_media_acquire(sched, local_pmedia);
            if (rc)
                /* Avoid releasing a media that has not been acquired */
                local_pmedia = NULL;
        }

        /* Potential media locking suceeded (or no media was loaded): acquire
         * device
         */
        if (rc == 0)
            rc = sched_dev_acquire(sched, selected);

        /* Something went wrong */
        if (rc != 0) {
            /* Release media if necessary */
            sched_media_release(sched, local_pmedia);
            /* clear previously selected device */
            selected = NULL;
            /* Allocate failed_dev if necessary */
            if (failed_dev == NULL) {
                failed_dev = calloc(sched->devices->len, sizeof(bool));
                if (failed_dev == NULL)
                    return NULL;
            }
            /* Locally mark this device as failed */
            failed_dev[selected_i] = true;
            /* resume loop where it was */
            goto retry;
        }
    } else
        pho_debug("Could not find a suitable %s device", op_status2str(op_st));

    free(failed_dev);
    return selected;
}

/**
 * Get the first device with enough space.
 * @retval 0 to stop searching for a device
 * @retval 1 to check next device.
 */
static int select_first_fit(size_t required_size,
                            struct dev_descr *dev_curr,
                            struct dev_descr **dev_selected)
{
    ENTRY;

    if (dev_curr->dss_media_info == NULL)
        return 1;

    if (dev_curr->dss_media_info->stats.phys_spc_free >= required_size) {
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
                           struct dev_descr *dev_curr,
                           struct dev_descr **dev_selected)
{
    ENTRY;

    if (dev_curr->dss_media_info == NULL)
        return 1;

    /* does it fit? */
    if (dev_curr->dss_media_info->stats.phys_spc_free < required_size)
        return 1;

    /* no previous fit, or better fit */
    if (*dev_selected == NULL || (dev_curr->dss_media_info->stats.phys_spc_free
                      < (*dev_selected)->dss_media_info->stats.phys_spc_free)) {
        *dev_selected = dev_curr;

        if (required_size == dev_curr->dss_media_info->stats.phys_spc_free)
            /* exact match, stop searching */
            return 0;
    }
    return 1;
}

/**
 * Select any device without checking media or available size.
 * @return 0 on first device found, 1 else (to continue searching).
 */
static int select_any(size_t required_size,
                      struct dev_descr *dev_curr,
                      struct dev_descr **dev_selected)
{
    ENTRY;

    if (*dev_selected == NULL) {
        *dev_selected = dev_curr;
        /* found an item, stop searching */
        return 0;
    }
    return 1;
}

/* Get the device with the least space available on the loaded media.
 * If a tape is loaded, it just needs to be unloaded.
 * If the filesystem is mounted, umount is needed before unloading.
 * @return 1 (always check all devices).
 */
static int select_drive_to_free(size_t required_size,
                                struct dev_descr *dev_curr,
                                struct dev_descr **dev_selected)
{
    ENTRY;

    /* skip failed and locked drives */
    if (dev_curr->op_status == PHO_DEV_OP_ST_FAILED
            || !dev_is_available(dev_curr)) {
        pho_debug("Skipping drive '%s' with status %s%s",
                  dev_curr->dev_path, op_status2str(dev_curr->op_status),
                  !dev_is_available(dev_curr) ? " (locked or busy)" : "");
        return 1;
    }

    /* if this function is called, no drive should be empty */
    if (dev_curr->op_status == PHO_DEV_OP_ST_EMPTY) {
        pho_warn("Unexpected drive status for '%s': '%s'",
                 dev_curr->dev_path, op_status2str(dev_curr->op_status));
        return 1;
    }

    /* less space available on this device than the previous ones? */
    if (*dev_selected == NULL || dev_curr->dss_media_info->stats.phys_spc_free
                    < (*dev_selected)->dss_media_info->stats.phys_spc_free) {
        *dev_selected = dev_curr;
        return 1;
    }

    return 1;
}

/** Mount the filesystem of a ready device */
static int sched_mount(struct dev_descr *dev)
{
    char                *mnt_root;
    struct fs_adapter    fsa;
    const char          *id;
    int                  rc;

    ENTRY;

    rc = get_fs_adapter(dev->dss_media_info->fs.type, &fsa);
    if (rc)
        goto out_err;

    rc = ldm_fs_mounted(&fsa, dev->dev_path, dev->mnt_path,
                        sizeof(dev->mnt_path));
    if (rc == 0) {
        dev->op_status = PHO_DEV_OP_ST_MOUNTED;
        return 0;
    }

    /**
     * @TODO If library indicates a media is in the drive but the drive
     * doesn't, we need to query the drive to load the tape.
     */

    id = basename(dev->dev_path);
    if (id == NULL)
        return -EINVAL;

    /* mount the device as PHO_MNT_PREFIX<id> */
    mnt_root = mount_point(id);
    if (!mnt_root)
        return -ENOMEM;

    pho_verb("Mounting device '%s' as '%s'", dev->dev_path, mnt_root);

    rc = ldm_fs_mount(&fsa, dev->dev_path, mnt_root,
                      dev->dss_media_info->fs.label);
    if (rc)
        LOG_GOTO(out_free, rc, "Failed to mount device '%s'",
                 dev->dev_path);

    /* update device state and set mount point */
    dev->op_status = PHO_DEV_OP_ST_MOUNTED;
    strncpy(dev->mnt_path,  mnt_root, sizeof(dev->mnt_path));
    dev->mnt_path[sizeof(dev->mnt_path) - 1] = '\0';

out_free:
    free(mnt_root);
out_err:
    if (rc != 0)
        dev->op_status = PHO_DEV_OP_ST_FAILED;
    return rc;
}

/** Unmount the filesystem of a 'mounted' device */
static int sched_umount(struct dev_descr *dev)
{
    struct fs_adapter  fsa;
    int                rc;

    ENTRY;

    if (dev->op_status != PHO_DEV_OP_ST_MOUNTED)
        LOG_RETURN(-EINVAL, "Unexpected drive status for '%s': '%s'",
                   dev->dev_path, op_status2str(dev->op_status));

    if (dev->mnt_path[0] == '\0')
        LOG_RETURN(-EINVAL, "No mount point for mounted device '%s'?!",
                   dev->dev_path);

    if (dev->dss_media_info == NULL)
        LOG_RETURN(-EINVAL, "No media in mounted device '%s'?!",
                   dev->dev_path);

    pho_verb("Unmounting device '%s' mounted as '%s'",
             dev->dev_path, dev->mnt_path);

    rc = get_fs_adapter(dev->dss_media_info->fs.type, &fsa);
    if (rc)
        return rc;

    rc = ldm_fs_umount(&fsa, dev->dev_path, dev->mnt_path);
    if (rc)
        LOG_RETURN(rc, "Failed to umount device '%s' mounted as '%s'",
                   dev->dev_path, dev->mnt_path);

    /* update device state and unset mount path */
    dev->op_status = PHO_DEV_OP_ST_LOADED;
    dev->mnt_path[0] = '\0';

    return 0;
}

/**
 * Load a media into a drive.
 *
 * @return 0 on success, -error number on error. -EBUSY is returned when a drive
 * to drive media movement was prevented by the library.
 */
static int sched_load(struct dev_descr *dev, struct media_info *media)
{
    struct lib_item_addr media_addr;
    struct lib_adapter   lib;
    int                  rc;
    int                  rc2;

    ENTRY;

    if (dev->op_status != PHO_DEV_OP_ST_EMPTY)
        LOG_RETURN(-EAGAIN, "%s: unexpected drive status: status='%s'",
                   dev->dev_path, op_status2str(dev->op_status));

    if (dev->dss_media_info != NULL)
        LOG_RETURN(-EAGAIN, "No media expected in device '%s' (found '%s')",
                   dev->dev_path, dev->dss_media_info->rsc.id.name);

    pho_verb("Loading '%s' into '%s'", media->rsc.id.name, dev->dev_path);

    /* get handle to the library depending on device type */
    rc = wrap_lib_open(dev->dss_dev_info->rsc.id.family, &lib);
    if (rc)
        return rc;

    /* lookup the requested media */
    rc = ldm_lib_media_lookup(&lib, media->rsc.id.name, &media_addr);
    if (rc)
        LOG_GOTO(out_close, rc, "Media lookup failed");

    rc = ldm_lib_media_move(&lib, &media_addr, &dev->lib_dev_info.ldi_addr);
    /* A movement from drive to drive can be prohibited by some libraries.
     * If a failure is encountered in such a situation, it probably means that
     * the state of the library has changed between the moment it has been
     * scanned and the moment the media and drive have been selected. The
     * easiest solution is therefore to return EBUSY to signal this situation to
     * the caller.
     */
    if (rc == -EINVAL
            && media_addr.lia_type == MED_LOC_DRIVE
            && dev->lib_dev_info.ldi_addr.lia_type == MED_LOC_DRIVE) {
        pho_debug("Failed to move a media from one drive to another, trying "
                  "again later");
        /* @TODO: acquire source drive on the fly? */
        return -EBUSY;
    } else if (rc != 0) {
        /* Set operationnal failure state on this drive. It is incomplete since
         * the error can originate from a defect tape too...
         *  - consider marking both as failed.
         *  - consider maintaining lists of errors to diagnose and decide who to
         *    exclude from the cool game.
         */
        dev->op_status = PHO_DEV_OP_ST_FAILED;
        LOG_GOTO(out_close, rc, "Media move failed");
    }

    /* update device status */
    dev->op_status = PHO_DEV_OP_ST_LOADED;
    /* associate media to this device */
    dev->dss_media_info = media;
    rc = 0;

out_close:
    rc2 = ldm_lib_close(&lib);
    return rc ? rc : rc2;
}

/**
 * Unload a media from a drive and unlock the media.
 */
static int sched_unload(struct lrs_sched *sched, struct dev_descr *dev)
{
    /* let the library select the target location */
    struct lib_item_addr    free_slot = { .lia_type = MED_LOC_UNKNOWN };
    struct lib_adapter      lib;
    int                     rc;
    int                     rc2;

    ENTRY;

    if (dev->op_status != PHO_DEV_OP_ST_LOADED)
        LOG_RETURN(-EINVAL, "Unexpected drive status for '%s': '%s'",
                   dev->dev_path, op_status2str(dev->op_status));

    if (dev->dss_media_info == NULL)
        LOG_RETURN(-EINVAL, "No media in loaded device '%s'?!",
                   dev->dev_path);

    pho_verb("Unloading '%s' from '%s'", dev->dss_media_info->rsc.id.name,
             dev->dev_path);

    /* get handle to the library, depending on device type */
    rc = wrap_lib_open(dev->dss_dev_info->rsc.id.family, &lib);
    if (rc)
        return rc;

    rc = ldm_lib_media_move(&lib, &dev->lib_dev_info.ldi_addr, &free_slot);
    if (rc != 0) {
        /* Set operationnal failure state on this drive. It is incomplete since
         * the error can originate from a defect tape too...
         *  - consider marking both as failed.
         *  - consider maintaining lists of errors to diagnose and decide who to
         *    exclude from the cool game.
         */
        dev->op_status = PHO_DEV_OP_ST_FAILED;
        LOG_GOTO(out_close, rc, "Media move failed");
    }

    /* update device status */
    dev->op_status = PHO_DEV_OP_ST_EMPTY;

    /* Locked by caller, by convention */
    sched_media_release(sched, dev->dss_media_info);

    /* free media resources */
    media_info_free(dev->dss_media_info);
    dev->dss_media_info = NULL;
    rc = 0;

out_close:
    rc2 = ldm_lib_close(&lib);
    return rc ? rc : rc2;
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
 * @param(in) pmedia          Media that should be used by the drive to check
 *                            compatibility (ignored if NULL, any not failed and
 *                            not administrator locked drive will fit.
 * @param(in) selected_devs   Devices already selected for this operation.
 * @param(in) n_selected_devs Number of devices already selected.
 * @return                    True if one compatible drive is found, else false.
 */
static bool compatible_drive_exists(struct lrs_sched *sched,
                                    struct media_info *pmedia,
                                    struct dev_descr *selected_devs,
                                    const int n_selected_devs)
{
    int i, j;

    for (i = 0; i < sched->devices->len; i++) {
        struct dev_descr *dev = &g_array_index(sched->devices,
                                               struct dev_descr, i);
        bool is_already_selected = false;

        if (dev->op_status == PHO_DEV_OP_ST_FAILED)
            continue;

        /* check the device is not already selected */
        for (j = 0; j < n_selected_devs; ++j)
            if (!strcmp(dev->dss_dev_info->rsc.id.name,
                        selected_devs[i].dss_dev_info->rsc.id.name)) {
                is_already_selected = true;
                break;
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
/**
 * Free one of the devices to allow mounting a new media.
 * On success, the returned device is locked.
 * @param(out) dev_descr       Pointer to an empty drive.
 * @param(in)  pmedia          Media that should be used by the drive to check
 *                             compatibility (ignored if NULL)
 * @param(in)  selected_devs   Devices already selected for this operation.
 * @param(in)  n_selected_devs Number of devices already selected.
 */
static int sched_free_one_device(struct lrs_sched *sched,
                                 struct dev_descr **dev_descr,
                                 struct media_info *pmedia,
                                 struct dev_descr *selected_devs,
                                 const int n_selected_devs)
{
    struct dev_descr *tmp_dev;
    int               rc;

    ENTRY;

    while (1) {

        /* get a drive to free (PHO_DEV_OP_ST_UNSPEC for any state) */
        tmp_dev = dev_picker(sched, PHO_DEV_OP_ST_UNSPEC, select_drive_to_free,
                             0, &NO_TAGS, pmedia);
        if (tmp_dev == NULL) {
            if (compatible_drive_exists(sched, pmedia, selected_devs,
                                        n_selected_devs))
                LOG_RETURN(-EAGAIN, "No suitable device to free");
            else
                LOG_RETURN(-ENODEV, "No compatible device exists not failed "
                                    "and not locked by admin");
        }

        if (tmp_dev->op_status == PHO_DEV_OP_ST_MOUNTED) {
            /* unmount it */
            rc = sched_umount(tmp_dev);
            if (rc) {
                /* set it failed and get another device */
                tmp_dev->op_status = PHO_DEV_OP_ST_FAILED;
                goto next;
            }
        }

        if (tmp_dev->op_status == PHO_DEV_OP_ST_LOADED) {
            /* unload the media */
            rc = sched_unload(sched, tmp_dev);
            if (rc) {
                /* set it failed and get another device */
                tmp_dev->op_status = PHO_DEV_OP_ST_FAILED;
                goto next;
            }
        }

        if (tmp_dev->op_status != PHO_DEV_OP_ST_EMPTY)
            LOG_RETURN(-EINVAL,
                       "Unexpected dev status '%s' for '%s': should be empty",
                       op_status2str(tmp_dev->op_status), tmp_dev->dev_path);

        /* success: we've got an empty device */
        *dev_descr = tmp_dev;
        return 0;

next:
        sched_dev_release(sched, tmp_dev);
        sched_media_release(sched, tmp_dev->dss_media_info);
    }
}

/**
 * Get an additionnal prepared device to perform a write operation.
 * @param[in]     size          Size of the extent to be written.
 * @param[in]     tags          Tags used to filter candidate media, the
 *                              selected media must have all the specified tags.
 * @param[in/out] devs          Array of selected devices to write with.
 * @param[in]     new_dev_index Index of the new device to find. Devices from
 *                              0 to i-1 must be already allocated (with loaded
 *                              and mounted media)
 */
static int sched_get_write_res(struct lrs_sched *sched, size_t size,
                               const struct tags *tags, struct dev_descr **devs,
                               size_t new_dev_index)
{
    struct dev_descr **new_dev = &devs[new_dev_index];
    device_select_func_t dev_select_policy;
    struct media_info *pmedia;
    bool media_owner;
    int rc;

    ENTRY;

    /*
     * @FIXME: externalize this to sched_responses_get to load the device state
     * only once per sched_responses_get call.
     */
    rc = sched_load_dev_state(sched);
    if (rc != 0)
        return rc;

    dev_select_policy = get_dev_policy();
    if (!dev_select_policy)
        return -EINVAL;

    pmedia = NULL;
    media_owner = false;

    /* 1a) is there a mounted filesystem with enough room? */
    *new_dev = dev_picker(sched, PHO_DEV_OP_ST_MOUNTED, dev_select_policy,
                          size, tags, NULL);
    if (*new_dev != NULL)
        return 0;

    /* 1b) is there a loaded media with enough room? */
    *new_dev = dev_picker(sched, PHO_DEV_OP_ST_LOADED, dev_select_policy, size,
                          tags, NULL);
    if (*new_dev != NULL) {
        /* mount the filesystem and return */
        rc = sched_mount(*new_dev);
        if (rc != 0)
            goto out_release;
        return 0;
    }

    /* V1: release a drive and load a tape with enough room.
     * later versions:
     * 2a) is there an idle drive, to eject the loaded tape?
     * 2b) is there an operation that will end soon?
     */

    /* 2) For the next steps, we need a media to write on.
     * It will be loaded into a free drive.
     * Note: sched_select_media locks the media.
     */
    pho_verb("Not enough space on loaded media: selecting another one");
    rc = sched_select_media(sched, &pmedia, size, sched->family, tags,
                            devs, new_dev_index);
    if (rc)
        return rc;
    /* we own the media structure */
    media_owner = true;

    /* Check if the media is already in a drive and try to acquire it. This
     * should never fail because media are locked before drives and a drive
     * shall never been locked if the media in it has not previously been
     * locked.
     */
    *new_dev = search_loaded_media(sched, pmedia->rsc.id.name);
    if (*new_dev != NULL) {
        rc = sched_dev_acquire(sched, *new_dev);
        if (rc != 0)
            GOTO(out_release, rc = -EAGAIN);
        /* Media is in dev, update dev->dss_media_info with fresh media info */
        media_info_free((*new_dev)->dss_media_info);
        (*new_dev)->dss_media_info = pmedia;
        return 0;
    }

    /* 3) is there a free drive? */
    *new_dev = dev_picker(sched, PHO_DEV_OP_ST_EMPTY, select_any, 0, &NO_TAGS,
                            pmedia);
    if (*new_dev == NULL) {
        pho_verb("No free drive: need to unload one");
        rc = sched_free_one_device(sched, new_dev, pmedia, *devs,
                                   new_dev_index);
        if (rc)
            goto out_release;
    }

    /* 4) load the selected media into the selected drive */
    rc = sched_load(*new_dev, pmedia);
    /* EBUSY means the tape could not be moved between two drives, try again
     * later
     */
    if (rc == -EBUSY)
        GOTO(out_release, rc = -EAGAIN);
    else if (rc)
        goto out_release;

    /* On success or sched_load, target device becomes the owner of pmedia
     * so pmedia must not be released after that.
     */
    media_owner = false;

    /* 5) mount the filesystem */
    rc = sched_mount(*new_dev);
    if (rc == 0)
        return 0;

out_release:
    if (*new_dev != NULL) {
        sched_dev_release(sched, *new_dev);
        /* Avoid releasing the same media twice */
        if (pmedia != (*new_dev)->dss_media_info)
            sched_media_release(sched, (*new_dev)->dss_media_info);
    }

    if (pmedia != NULL) {
        pho_debug("Releasing selected media '%s'", pmedia->rsc.id.name);
        sched_media_release(sched, pmedia);
        if (media_owner)
            media_info_free(pmedia);
    }
    return rc;
}

static struct dev_descr *search_loaded_media(struct lrs_sched *sched,
                                             const char *name)
{
    int         i;

    ENTRY;

    if (name == NULL)
        return NULL;

    for (i = 0; i < sched->devices->len; i++) {
        const char          *media_id;
        enum dev_op_status   op_st;
        struct dev_descr    *dev;

        dev = &g_array_index(sched->devices, struct dev_descr, i);
        op_st = dev->op_status;

        if (op_st != PHO_DEV_OP_ST_MOUNTED && op_st != PHO_DEV_OP_ST_LOADED)
            continue;

        /* The drive may contain a media unknown to phobos, skip it */
        if (dev->dss_media_info == NULL)
            continue;

        media_id = dev->dss_media_info->rsc.id.name;
        if (media_id == NULL) {
            pho_warn("Cannot retrieve media ID from device '%s'",
                     dev->dev_path);
            continue;
        }

        if (!strcmp(name, media_id))
            return dev;
    }
    return NULL;
}

static int sched_media_prepare(struct lrs_sched *sched,
                               const struct pho_id *id,
                               enum sched_operation op, struct dev_descr **pdev,
                               struct media_info **pmedia)
{
    struct dev_descr    *dev;
    struct media_info   *med = NULL;
    bool                 post_fs_mount;
    int                  rc;

    ENTRY;

    *pdev = NULL;
    *pmedia = NULL;

    rc = sched_fill_media_info(&sched->dss, &med, id);
    if (rc != 0)
        return rc;

    /* Check that the media is not already locked */
    if (med->lock.is_external) {
        pho_debug("Media '%s' is locked, returning EAGAIN", id->name);
        GOTO(out, rc = -EAGAIN);
    }

    switch (op) {
    case LRS_OP_READ:
        if (!med->flags.get)
            LOG_RETURN(-EPERM, "Cannot do a get, get flag is false on '%s'",
                       id->name);
        /* fall through */
    case LRS_OP_WRITE:
        if (med->fs.status == PHO_FS_STATUS_BLANK)
            LOG_RETURN(-EINVAL, "Cannot do I/O on unformatted media '%s'",
                       id->name);
        if (med->rsc.adm_status != PHO_RSC_ADM_ST_UNLOCKED)
            LOG_RETURN(-EPERM, "Cannot do I/O on an unavailable medium '%s'",
                       id->name);
        if (op == LRS_OP_WRITE && !med->flags.put)
            LOG_RETURN(-EPERM, "Cannot do a put, put flag is false on '%s'",
                       id->name);
        post_fs_mount = true;
        break;
    case LRS_OP_FORMAT:
        if (med->fs.status != PHO_FS_STATUS_BLANK)
            LOG_RETURN(-EINVAL, "Cannot format non-blank media '%s'",
                       id->name);
        post_fs_mount = false;
        break;
    default:
        LOG_RETURN(-ENOSYS, "Unknown operation %x", (int)op);
    }

    rc = sched_media_acquire(sched, med);
    if (rc != 0)
        GOTO(out, rc = -EAGAIN);

    /* check if the media is already in a drive */
    dev = search_loaded_media(sched, id->name);
    if (dev != NULL) {
        rc = sched_dev_acquire(sched, dev);
        if (rc != 0)
            GOTO(out_mda_unlock, rc = -EAGAIN);
        /* Media is in dev, update dev->dss_media_info with fresh media info */
        media_info_free(dev->dss_media_info);
        dev->dss_media_info = med;
    } else {
        pho_verb("Media '%s' is not in a drive", id->name);

        /* Is there a free drive? */
        dev = dev_picker(sched, PHO_DEV_OP_ST_EMPTY, select_any, 0, &NO_TAGS,
                         med);
        if (dev == NULL) {
            pho_verb("No free drive: need to unload one");
            rc = sched_free_one_device(sched, &dev, med, NULL, 0);
            if (rc != 0)
                LOG_GOTO(out_mda_unlock, rc, "No device available");
        }

        /* load the media in it */
        rc = sched_load(dev, med);
        /* EBUSY means the tape could not be moved between two drives, try again
         * later
         */
        if (rc == -EBUSY)
            GOTO(out_dev_unlock, rc = -EAGAIN);
        else if (rc != 0)
            goto out_dev_unlock;
    }

    /* Mount only for READ/WRITE and if not already mounted */
    if (post_fs_mount && dev->op_status != PHO_DEV_OP_ST_MOUNTED)
        rc = sched_mount(dev);

out_dev_unlock:
    if (rc)
        sched_dev_release(sched, dev);

out_mda_unlock:
    if (rc)
        sched_media_release(sched, med);

out:
    if (rc) {
        media_info_free(med);
        *pmedia = NULL;
        *pdev = NULL;
    } else {
        *pmedia = med;
        *pdev = dev;
    }
    return rc;
}

/**
 * Load and format a medium to the given fs type.
 *
 * \param[in]       sched       Initialized sched.
 * \param[in]       id          Medium ID for the medium to format.
 * \param[in]       fs          Filesystem type (only PHO_FS_LTFS for now).
 * \param[in]       unlock      Unlock tape if successfully formated.
 * \return                      0 on success, negative error code on failure.
 */
static int sched_format(struct lrs_sched *sched, const struct pho_id *id,
                        enum fs_type fs, bool unlock)
{
    struct dev_descr    *dev = NULL;
    struct media_info   *media_info = NULL;
    int                  rc;
    int                  rc2;
    struct ldm_fs_space  spc = {0};
    struct fs_adapter    fsa;

    ENTRY;

    rc = sched_load_dev_state(sched);
    if (rc != 0)
        return rc;

    rc = sched_media_prepare(sched, id, LRS_OP_FORMAT, &dev, &media_info);
    if (rc != 0)
        return rc;

    /* -- from now on, device is owned -- */

    if (dev->dss_media_info == NULL)
        LOG_GOTO(err_out, rc = -EINVAL, "Invalid device state");

    pho_verb("Format media '%s' as %s", id->name, fs_type2str(fs));

    rc = get_fs_adapter(fs, &fsa);
    if (rc)
        LOG_GOTO(err_out, rc, "Failed to get FS adapter");

    rc = ldm_fs_format(&fsa, dev->dev_path, id->name, &spc);
    if (rc)
        LOG_GOTO(err_out, rc, "Cannot format media '%s'", id->name);

    /* Systematically use the media ID as filesystem label */
    strncpy(media_info->fs.label, id->name, sizeof(media_info->fs.label));
    media_info->fs.label[sizeof(media_info->fs.label) - 1] = '\0';

    media_info->stats.phys_spc_used = spc.spc_used;
    media_info->stats.phys_spc_free = spc.spc_avail;

    /* Post operation: update media information in DSS */
    media_info->fs.status = PHO_FS_STATUS_EMPTY;

    if (unlock) {
        pho_verb("Unlocking media '%s'", id->name);
        media_info->rsc.adm_status = PHO_RSC_ADM_ST_UNLOCKED;
    }

    rc = dss_media_set(&sched->dss, media_info, 1, DSS_SET_UPDATE);
    if (rc != 0)
        LOG_GOTO(err_out, rc, "Failed to update state of media '%s'",
                 id->name);

err_out:
    /* Release ownership. Do not fail the whole operation if unlucky here... */
    rc2 = sched_dev_release(sched, dev);
    if (rc2)
        pho_error(rc2, "Failed to release lock on '%s'", dev->dev_path);

    rc2 = sched_media_release(sched, media_info);
    if (rc2)
        pho_error(rc2, "Failed to release lock on '%s'", id->name);

    /* Don't free media_info since it is still referenced inside dev */
    return rc;
}

static bool sched_mount_is_writable(const char *fs_root, enum fs_type fs_type)
{
    struct ldm_fs_space  fs_info = {0};
    struct fs_adapter    fsa;
    int                  rc;

    rc = get_fs_adapter(fs_type, &fsa);
    if (rc)
        LOG_RETURN(rc, "No FS adapter found for '%s' (type %s)",
                   fs_root, fs_type2str(fs_type));

    rc = ldm_fs_df(&fsa, fs_root, &fs_info);
    if (rc)
        LOG_RETURN(rc, "Cannot retrieve media usage information");

    return !(fs_info.spc_flags & PHO_FS_READONLY);
}

/**
 * Query to write a given amount of data by acquiring a new device with medium
 *
 * @param(in)     sched         Initialized LRS.
 * @param(in)     write_size    Size that will be written on the medium.
 * @param(in)     tags          Tags used to select a medium to write on, the
 *                              selected medium must have the specified tags.
 * @param(in/out) devs          Array of devices with the reserved medium
 *                              mounted and loaded in it (no need to free it).
 * @param(in)     new_dev_index Index in dev of the new device to alloc (devices
 *                              from 0 to i-1 must be already allocated : medium
 *                              mounted and loaded)
 *
 * @return 0 on success, -1 * posix error code on failure
 */
static int sched_write_prepare(struct lrs_sched *sched, size_t write_size,
                               const struct tags *tags,
                               struct dev_descr **devs, int new_dev_index)
{
    struct media_info  *media = NULL;
    struct dev_descr   *new_dev;
    int                 rc;

    ENTRY;

retry:
    rc = sched_get_write_res(sched, write_size, tags, devs, new_dev_index);
    if (rc != 0)
        return rc;

    new_dev = devs[new_dev_index];
    media = new_dev->dss_media_info;

    /* LTFS can cunningly mount almost-full tapes as read-only, and so would
     * damaged disks. Mark the media as full and retry when this occurs.
     */
    if (!sched_mount_is_writable(new_dev->mnt_path,
                                 media->fs.type)) {
        pho_warn("Media '%s' OK but mounted R/O, marking full and retrying...",
                 media->rsc.id.name);

        media->fs.status = PHO_FS_STATUS_FULL;

        rc = dss_media_set(&sched->dss, media, 1, DSS_SET_UPDATE);
        if (rc)
            LOG_GOTO(err_cleanup, rc, "Cannot update media information");

        sched_dev_release(sched, new_dev);
        sched_media_release(sched, media);
        new_dev = NULL;
        media = NULL;
        goto retry;
    }

    pho_verb("Writing to media '%s' using device '%s' "
             "(free space: %zu bytes)",
             media->rsc.id.name, new_dev->dev_path,
             new_dev->dss_media_info->stats.phys_spc_free);

err_cleanup:
    if (rc != 0) {
        sched_dev_release(sched, new_dev);
        sched_media_release(sched, media);
    }

    return rc;
}

/**
 * Query to read from a given set of medium.
 *
 * @param(in)     sched   Initialized LRS.
 * @param(in)     id      The id of the medium to load
 * @param(out)    dev     Device with the required medium mounted and loaded in
 *                        it (no need to free it).
 *
 * @return 0 on success, -1 * posix error code on failure
 */
static int sched_read_prepare(struct lrs_sched *sched,
                              const struct pho_id *id, struct dev_descr **dev)
{
    struct media_info   *media_info = NULL;
    int                  rc;

    ENTRY;

    rc = sched_load_dev_state(sched);
    if (rc != 0)
        return rc;

    /* Fill in information about media and mount it if needed */
    rc = sched_media_prepare(sched, id, LRS_OP_READ, dev, &media_info);
    if (rc)
        return rc;

    if ((*dev)->dss_media_info == NULL)
        LOG_GOTO(out, rc = -EINVAL, "Invalid device state, expected media '%s'",
                 id->name);

out:
    /* Don't free media_info since it is still referenced inside dev */
    return rc;
}

/** Update media_info stats and push its new state to the DSS */
static int sched_media_update(struct lrs_sched *sched,
                              struct media_info *media_info,
                              size_t size_written, int media_rc,
                              const char *fsroot, bool is_full)
{
    struct ldm_fs_space  spc = {0};
    struct fs_adapter    fsa;
    enum fs_type         fs_type = media_info->fs.type;
    int                  rc;

    rc = get_fs_adapter(fs_type, &fsa);
    if (rc)
        LOG_RETURN(rc, "No FS adapter found for '%s' (type %s)",
                   fsroot, fs_type2str(fs_type));

    rc = ldm_fs_df(&fsa, fsroot, &spc);
    if (rc)
        LOG_RETURN(rc, "Cannot retrieve media usage information");

    if (size_written) {
        media_info->stats.nb_obj += 1;
        media_info->stats.phys_spc_used = spc.spc_used;
        media_info->stats.phys_spc_free = spc.spc_avail;

        if (media_rc == 0)
            media_info->stats.logc_spc_used += size_written;
    }

    if (media_info->fs.status == PHO_FS_STATUS_EMPTY)
        media_info->fs.status = PHO_FS_STATUS_USED;

    if (is_full || media_info->stats.phys_spc_free == 0)
        media_info->fs.status = PHO_FS_STATUS_FULL;

    /* TODO update nb_load, nb_errors, last_load */

    /* @FIXME: this DSS update could be done when releasing the media */
    rc = dss_media_set(&sched->dss, media_info, 1, DSS_SET_UPDATE);
    if (rc)
        LOG_RETURN(rc, "Cannot update media information");

    return 0;
}

/*
 * @TODO: support releasing multiple medias at a time (handle a
 * full media_release_req).
 */
static int sched_io_complete(struct lrs_sched *sched,
                             struct media_info *media_info, size_t size_written,
                             int media_rc, const char *fsroot)
{
    struct io_adapter    ioa;
    bool                 is_full = false;
    int                  rc;

    ENTRY;

    rc = get_io_adapter(media_info->fs.type, &ioa);
    if (rc)
        LOG_RETURN(rc, "No suitable I/O adapter for filesystem type: '%s'",
                   fs_type2str(media_info->fs.type));

    rc = ioa_medium_sync(&ioa, fsroot);
    if (rc)
        LOG_RETURN(rc, "Cannot flush media at: %s", fsroot);

    if (is_medium_global_error(media_rc) || is_medium_global_error(rc))
        is_full = true;

    rc = sched_media_update(sched, media_info, size_written, media_rc, fsroot,
                          is_full);
    if (rc)
        LOG_RETURN(rc, "Cannot update media information");

    return 0;
}

/******************************************************************************/
/* Request/response manipulation **********************************************/
/******************************************************************************/

static int sched_device_add(struct lrs_sched *sched, enum rsc_family family,
                            const char *name)
{
    struct dev_descr device = {0};
    struct dev_info *devi = NULL;
    struct dss_filter filter;
    struct lib_adapter lib;
    int dev_cnt = 0;
    int rc = 0;

    pho_verb("Adding device '%s' to lrs\n", name);
    rc = dss_filter_build(&filter,
                          "{\"$AND\": ["
                          "  {\"DSS::DEV::host\": \"%s\"},"
                          "  {\"DSS::DEV::family\": \"%s\"},"
                          "  {\"DSS::DEV::serial\": \"%s\"},"
                          "  {\"DSS::DEV::adm_status\": \"%s\"}"
                          "]}",
                          get_hostname(),
                          rsc_family2str(family),
                          name,
                          rsc_adm_status2str(PHO_RSC_ADM_ST_UNLOCKED));
    if (rc)
        goto err;

    rc = dss_device_get(&sched->dss, &filter, &devi, &dev_cnt);
    dss_filter_free(&filter);
    if (rc)
        goto err;

    if (dev_cnt == 0) {
        pho_info("No usable device found (%s:%s): check device status",
                 rsc_family2str(family), name);
        GOTO(err_res, rc = -ENXIO);
    }

    device.dss_dev_info = dev_info_dup(devi);
    if (!device.dss_dev_info)
        LOG_GOTO(err_res, rc = -ENOMEM, "Device info duplication failed");

    /* get a handle to the library to query it */
    rc = wrap_lib_open(device.dss_dev_info->rsc.id.family, &lib);
    if (rc)
        goto err_dev;

    rc = sched_fill_dev_info(&sched->dss, &lib, &device);
    if (rc)
        goto err_lib;

    /* Add the newly initialized device to the device list */
    g_array_append_val(sched->devices, device);

err_lib:
    ldm_lib_close(&lib);

err_dev:
    if (rc)
        dev_info_free(device.dss_dev_info, true);

err_res:
    dss_res_free(devi, dev_cnt);

err:
    return rc;
}

/**
 * Remove the locked device from the local device array.
 * It will be inserted back once the device status is changed to 'unlocked'.
 */
static int sched_device_lock(struct lrs_sched *sched, const char *name)
{
    struct dev_descr *dev;
    int i;

    for (i = 0; i < sched->devices->len; ++i) {
        dev = &g_array_index(sched->devices, struct dev_descr, i);

        if (!strcmp(name, dev->dss_dev_info->rsc.id.name)) {
            g_array_remove_index_fast(sched->devices, i);
            pho_verb("Removed locked device '%s' from the local database",
                     name);
            return 0;
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
    struct dev_descr *dev;
    int i;

    for (i = 0; i < sched->devices->len; ++i) {
        dev = &g_array_index(sched->devices, struct dev_descr, i);

        if (!strcmp(name, dev->dss_dev_info->rsc.id.name)) {
            pho_verb("Updating device '%s' state to unlocked", name);
            dev->dss_dev_info->rsc.adm_status = PHO_RSC_ADM_ST_UNLOCKED;
            return 0;
        }
    }

    pho_verb("Cannot find local device info for '%s', will fetch it "
             "from the database", name);

    return sched_device_add(sched, sched->family, name);
}

/** Wrapper of sched_req_free to be used as glib callback */
static void sched_req_free_wrapper(void *reqc)
{
    pho_srl_request_free(((struct req_container *)reqc)->req, true);
    free(((struct req_container *)reqc)->req);
}

/** Wrapper of sched_resp_free to be used as glib callback */
static void sched_resp_free_wrapper(void *respc)
{
    pho_srl_response_free(((struct resp_container *)respc)->resp, false);
    free(((struct resp_container *)respc)->resp);
}

int sched_request_enqueue(struct lrs_sched *sched, struct req_container *reqc)
{
    pho_req_t *req;

    if (!reqc)
        return -EINVAL;

    req = reqc->req;

    if (!req)
        return -EINVAL;

    if (pho_request_is_release(req))
        g_queue_push_tail(sched->release_queue, reqc);
    else
        g_queue_push_tail(sched->req_queue, reqc);

    return 0;
}

/**
 * Flush, update dss status and release locks on a medium and its associated
 * device.
 */
static int sched_handle_medium_release(struct lrs_sched *sched,
                                       pho_req_release_elt_t *medium)
{
    int rc = 0;
    struct dev_descr *dev;

    /* Find the where the media is loaded */
    dev = search_loaded_media(sched, medium->med_id->name);
    if (dev == NULL) {
        LOG_RETURN(-ENOENT,
                 "Could not find '%s' mount point, the media is not loaded",
                 medium->med_id->name);
    }

    /* Flush media and update media info in dss */
    rc = sched_io_complete(sched, dev->dss_media_info, medium->size_written,
                         medium->rc, dev->mnt_path);

    /* Release all associated locks */
    sched_dev_release(sched, dev);
    sched_media_release(sched, dev->dss_media_info);

    return rc;
}

/**
 * Flush, update dss status and release locks for all media from a release
 * request and their associated devices.
 */
static int sched_handle_media_release(struct lrs_sched *sched,
                                      pho_req_release_t *req)
{
    size_t i;
    int rc = 0;

    for (i = 0; i < req->n_media; i++) {
        int rc2 = sched_handle_medium_release(sched, req->media[i]);

        rc = rc ? : rc2;
    }

    return rc;
}

/*
 * @FIXME: this assumes one media is reserved for one only one request. In the
 * future, we may want to give a media allocation to multiple requests, we will
 * therefore need to be more careful not to call sched_media_release too
 * early, or count nested locks.
 */
/**
 * Handle a write allocation request by finding an appropriate medias to write
 * to and mounting them.
 *
 * The request succeeds totally or all the performed allocations are rolled
 * back.
 */
static int sched_handle_write_alloc(struct lrs_sched *sched, pho_req_t *req,
                                    pho_resp_t *resp)
{
    struct dev_descr **devs = NULL;
    size_t i;
    int rc = 0;
    pho_req_write_t *wreq = req->walloc;

    pho_debug("Write allocation request (%ld medias)", wreq->n_media);

    rc = pho_srl_response_write_alloc(resp, wreq->n_media);
    if (rc)
        return rc;

    devs = calloc(wreq->n_media, sizeof(*devs));
    if (devs == NULL)
        return -ENOMEM;

    resp->req_id = req->id;

    /*
     * @TODO: if media locking becomes ref counted, ensure all selected medias
     * are different
     */
    for (i = 0; i < wreq->n_media; i++) {
        struct tags t;

        pho_resp_write_elt_t *wresp = resp->walloc->media[i];

        pho_debug("Write allocation request media %ld: need %ld bytes",
                  i, wreq->media[i]->size);

        t.n_tags = wreq->media[i]->n_tags;
        t.tags = wreq->media[i]->tags;

        rc = sched_write_prepare(sched, wreq->media[i]->size, &t, devs, i);
        if (rc)
            goto out;

        /* build response */
        wresp->avail_size = devs[i]->dss_media_info->stats.phys_spc_free;
        wresp->med_id->family = devs[i]->dss_media_info->rsc.id.family;
        wresp->med_id->name = strdup(devs[i]->dss_media_info->rsc.id.name);
        wresp->root_path = strdup(devs[i]->mnt_path);
        wresp->fs_type = devs[i]->dss_media_info->fs.type;
        wresp->addr_type = devs[i]->dss_media_info->addr_type;

        pho_debug("Allocated media %s for write request", wresp->med_id->name);

        if (wresp->root_path == NULL) {
            /*
             * Increment i so that the currently selected media is released
             * as well in cleanup
             */
            i++;
            GOTO(out, rc = -ENOMEM);
        }
    }

out:
    free(devs);

    if (rc) {
        size_t n_media_acquired = i;

        /* Rollback device and media acquisition */
        for (i = 0; i < n_media_acquired; i++) {
            struct dev_descr *dev;
            pho_resp_write_elt_t *wresp = resp->walloc->media[i];

            dev = search_loaded_media(sched, wresp->med_id->name);
            sched_dev_release(sched, dev);
            sched_media_release(sched, dev->dss_media_info);
        }

        pho_srl_response_free(resp, false);
        if (rc != -EAGAIN) {
            int rc2 = pho_srl_response_error_alloc(resp);

            if (rc2)
                return rc2;

            resp->error->rc = rc;
            resp->error->req_kind = PHO_REQUEST_KIND__RQ_WRITE;

            /* Request processing error, not an LRS error */
            rc = 0;
        }
    }

    return rc;
}

/**
 * Handle a read allocation request by finding the specified medias and mounting
 * them.
 *
 * The request succeeds totally or all the performed allocations are rolled
 * back.
 */
static int sched_handle_read_alloc(struct lrs_sched *sched, pho_req_t *req,
                                   pho_resp_t *resp)
{
    struct dev_descr *dev = NULL;
    size_t n_selected = 0;
    size_t i;
    int rc = 0;
    pho_req_read_t *rreq = req->ralloc;

    rc = pho_srl_response_read_alloc(resp, rreq->n_required);
    if (rc)
        return rc;

    /*
     * FIXME: this is a very basic selection algorithm that does not try to
     * select the most available media first.
     */
    for (i = 0; i < rreq->n_med_ids; i++) {
        pho_resp_read_elt_t *rresp = resp->ralloc->media[n_selected];
        struct pho_id m;

        m.family = (enum rsc_family)rreq->med_ids[i]->family;
        pho_id_name_set(&m, rreq->med_ids[i]->name);

        rc = sched_read_prepare(sched, &m, &dev);
        if (rc)
            continue;

        n_selected++;

        rresp->fs_type = dev->dss_media_info->fs.type;
        rresp->addr_type = dev->dss_media_info->addr_type;
        rresp->root_path = strdup(dev->mnt_path);
        rresp->med_id->family = rreq->med_ids[i]->family;
        rresp->med_id->name = strdup(rreq->med_ids[i]->name);

        if (n_selected == rreq->n_required)
            break;
    }

    if (rc) {
        /* rollback */
        for (i = 0; i < n_selected; i++) {
            pho_resp_read_elt_t *rresp = resp->ralloc->media[i];

            dev = search_loaded_media(sched, rresp->med_id->name);
            sched_dev_release(sched, dev);
            sched_media_release(sched, dev->dss_media_info);
        }

        pho_srl_response_free(resp, false);
        if (rc != -EAGAIN) {
            int rc2 = pho_srl_response_error_alloc(resp);

            if (rc2)
                return rc2;

            resp->error->rc = rc;
            resp->error->req_kind = PHO_REQUEST_KIND__RQ_READ;

            /* Request processing error, not an LRS error */
            rc = 0;
        }
    }

    return rc;
}

/**
 * Handle incoming release requests, appending corresponding release responses
 * to resp_array if it is not NULL.
 *
 * Can be called with a NULL resp_array to handle all pending release requests
 * without generating responses, for example when destroying an LRS with
 * sched_fini.
 */
static int sched_handle_release_reqs(struct lrs_sched *sched,
                                     GArray *resp_array)
{
    struct req_container *reqc;

    while ((reqc = g_queue_pop_tail(sched->release_queue)) != NULL) {
        pho_req_t *req = reqc->req;
        int rc = 0;
        struct resp_container *respc;

        rc = sched_handle_media_release(sched, req->release);

        /* If resp_array is NULL, just release media, do not save responses */
        if (resp_array == NULL) {
            pho_srl_request_free(req, true);
            free(reqc);
            continue;
        }

        g_array_set_size(resp_array, resp_array->len + 1);
        respc = &g_array_index(resp_array, struct resp_container,
                               resp_array->len - 1);
        respc->token = reqc->token;
        respc->resp = malloc(sizeof(*respc->resp));
        if (!respc->resp) {
            pho_srl_request_free(req, true);
            free(reqc);
            continue;
        }

        if (rc) {
            int rc2 = pho_srl_response_error_alloc(respc->resp);

            if (rc2) {
                pho_srl_request_free(req, true);
                free(reqc);
                return rc2;
            }

            respc->resp->error->rc = rc;
            respc->resp->error->req_kind = PHO_REQUEST_KIND__RQ_RELEASE;
        } else {
            pho_req_release_t *rel = req->release;
            size_t n_media = rel->n_media;
            size_t i;
            pho_resp_release_t *respl;

            rc = pho_srl_response_release_alloc(respc->resp, n_media);
            if (rc) {
                pho_srl_request_free(req, true);
                free(reqc);
                return rc;
            }

            /* Build the answer */
            respc->resp->req_id = req->id;
            respl = respc->resp->release;

            for (i = 0; i < n_media; ++i) {
                respl->med_ids[i]->family = rel->media[i]->med_id->family;
                respl->med_ids[i]->name = strdup(rel->media[i]->med_id->name);
            }

            /* Free incoming request */
            pho_srl_request_free(req, true);
            free(reqc);
        }
    }

    return 0;
}

static int sched_handle_format(struct lrs_sched *sched, pho_req_t *req,
                               pho_resp_t *resp)
{
    int rc = 0;
    pho_req_format_t *freq = req->format;
    struct pho_id m;

    rc = pho_srl_response_format_alloc(resp);
    if (rc)
        return rc;

    m.family = (enum rsc_family)freq->med_id->family;
    pho_id_name_set(&m, freq->med_id->name);

    rc = sched_format(sched, &m, (enum fs_type)freq->fs, freq->unlock);
    if (rc) {
        pho_srl_response_free(resp, false);
        if (rc != -EAGAIN) {
            int rc2 = pho_srl_response_error_alloc(resp);

            if (rc2)
                return rc2;

            resp->req_id = req->id;
            resp->error->rc = rc;
            resp->error->req_kind = PHO_REQUEST_KIND__RQ_FORMAT;

            /* Request processing error, not an LRS error */
            rc = 0;
        }
    } else {
        resp->req_id = req->id;
        resp->format->med_id->family = freq->med_id->family;
        resp->format->med_id->name = strdup(freq->med_id->name);
    }

    return rc;
}

static int sched_handle_notify(struct lrs_sched *sched, pho_req_t *req,
                               pho_resp_t *resp)
{
    pho_req_notify_t *nreq = req->notify;
    int rc = 0;

    rc = pho_srl_response_notify_alloc(resp);
    if (rc)
        return rc;

    switch (nreq->op) {
    case PHO_NTFY_OP_DEVICE_ADD:
        rc = sched_device_add(sched, (enum rsc_family)nreq->rsrc_id->family,
                              nreq->rsrc_id->name);
        break;
    case PHO_NTFY_OP_DEVICE_LOCK:
        rc = sched_device_lock(sched, nreq->rsrc_id->name);
        break;
    case PHO_NTFY_OP_DEVICE_UNLOCK:
        rc = sched_device_unlock(sched, nreq->rsrc_id->name);
        break;
    default:
        LOG_GOTO(err, rc = -EINVAL, "The requested operation is not "
                 "recognized");
    }

    if (rc)
        goto err;

    resp->req_id = req->id;
    resp->notify->rsrc_id->family = nreq->rsrc_id->family;
    resp->notify->rsrc_id->name = strdup(nreq->rsrc_id->name);

    return rc;

err:
    pho_srl_response_free(resp, false);

    if (rc != -EAGAIN) {
        int rc2 = pho_srl_response_error_alloc(resp);

        if (rc2)
            return rc2;

        resp->req_id = req->id;
        resp->error->rc = rc;
        resp->error->req_kind = PHO_REQUEST_KIND__RQ_NOTIFY;

        /* Request processing error, not an LRS error */
        rc = 0;
    }

    return rc;
}

int sched_responses_get(struct lrs_sched *sched, int *n_resp,
                        struct resp_container **respc)
{
    GArray *resp_array;
    size_t release_queue_len = g_queue_get_length(sched->release_queue);
    struct req_container *reqc;
    int rc = 0;

    /* At least release_queue_len responses will be emitted */
    resp_array = g_array_sized_new(FALSE, FALSE, sizeof(struct resp_container),
                                   release_queue_len);
    if (resp_array == NULL)
        return -ENOMEM;
    g_array_set_clear_func(resp_array, sched_resp_free_wrapper);

    /*
     * First release everything that can be.
     *
     * NOTE: in the future, media could be "released" as soon as possible, but
     * only flushed in batch later on. The response to the "release" request
     * would then have to wait for the full flush.
     *
     * TODO: if there are multiple release requests for one media, only release
     * it once but answer to all requests.
     */
    rc = sched_handle_release_reqs(sched, resp_array);
    if (rc)
        goto out;

    /*
     * Very simple algorithm (FIXME): serve requests until the first EAGAIN is
     * encountered.
     */
    while ((reqc = g_queue_pop_tail(sched->req_queue)) != NULL) {
        pho_req_t *req = reqc->req;
        struct resp_container *respc;

        g_array_set_size(resp_array, resp_array->len + 1);
        respc = &g_array_index(resp_array, struct resp_container,
                               resp_array->len - 1);

        respc->token = reqc->token;
        respc->resp = malloc(sizeof(*respc->resp));
        if (!respc->resp)
            LOG_GOTO(out, rc = -ENOMEM, "lrs cannot allocate response");

        if (pho_request_is_write(req)) {
            pho_debug("lrs received write request (%p)", req);
            rc = sched_handle_write_alloc(sched, req, respc->resp);
        } else if (pho_request_is_read(req)) {
            pho_debug("lrs received read allocation request (%p)", req);
            rc = sched_handle_read_alloc(sched, req, respc->resp);
        } else if (pho_request_is_format(req)) {
            pho_debug("lrs received format request (%p)", req);
            rc = sched_handle_format(sched, req, respc->resp);
        } else if (pho_request_is_notify(req)) {
            pho_debug("lrs received notify request (%p)", req);
            rc = sched_handle_notify(sched, req, respc->resp);
        } else {
            /* Unexpected req->kind, very probably a programming error */
            pho_error(rc = -EPROTO,
                      "lrs received an invalid request "
                      "(no walloc, ralloc or release field)");
        }

        /*
         * Break on EAGAIN and mark the whole run as a success (but there may be
         * no response).
         */
        if (rc == -EAGAIN) {
            /* Requeue last request */
            g_queue_push_tail(sched->req_queue, reqc);
            g_array_remove_index(resp_array, resp_array->len - 1);
            rc = 0;
            break;
        }

        pho_srl_request_free(reqc->req, true);
        free(reqc);
    }

out:
    /* Error return means a fatal error for this LRS (FIXME) */
    if (rc) {
        g_array_free(resp_array, TRUE);
    } else {
        *n_resp = resp_array->len;
        *respc = (struct resp_container *)g_array_free(resp_array, FALSE);
    }

    /*
     * Media that have not been re-acquired at this point could be "globally
     * unlocked" here rather than at the beginning of this function.
     */

    return rc;
}

