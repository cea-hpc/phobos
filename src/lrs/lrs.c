/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 * Copyright 2014-2016 CEA/DAM. All Rights Reserved.
 */
/**
 * \brief  Phobos Local Resource Scheduler (LRS)
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "pho_lrs.h"
#include "pho_common.h"
#include "pho_dss.h"
#include "pho_type_utils.h"
#include "pho_cfg.h"
#include "pho_ldm.h"
#include "pho_io.h"

#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <assert.h>
#include <sys/utsname.h>

/**
 * Build a mount path for the given identifier.
 * @param[in] id    Unique drive identified on the host.
 * The result must be released by the caller using free(3).
 */
static char *mount_point(const char *id)
{
    const char  *mnt_cfg;
    char        *mnt_out;

    mnt_cfg = pho_cfg_get(PHO_CFG_LRS_mount_prefix);
    if (mnt_cfg == NULL)
        return NULL;

    /* mount the device as PHO_MNT_PREFIX<id> */
    if (asprintf(&mnt_out, "%s%s", mnt_cfg, id) < 0)
        return NULL;

    return mnt_out;
}

/** return the default device family to write data */
static enum dev_family default_family(void)
{
    const char *fam_str;

    fam_str = pho_cfg_get(PHO_CFG_LRS_default_family);
    if (fam_str == NULL)
        return PHO_DEV_INVAL;

    return str2dev_family(fam_str);
}

static struct utsname host_info;

/** get host name once (/!\ not thread-safe). */
static const char *get_hostname(void)
{
    if (host_info.nodename[0] == '\0') {
        char *dot;

        if (uname(&host_info) != 0) {
            pho_error(errno, "Failed to get host name");
            return NULL;
        }
        dot = strchr(host_info.nodename, '.');
        if (dot)
            *dot = '\0';
    }
    return host_info.nodename;
}

/** all needed information to select devices */
struct dev_descr {
    struct dev_info     *dss_dev_info; /**< device info from DSS */
    struct lib_drv_info  lib_dev_info; /**< device info from library
                                            (for tape drives) */
    struct ldm_dev_state sys_dev_state; /**< device info from system */

    enum dev_op_status   op_status; /**< operational status of the device */
    char                 dev_path[PATH_MAX]; /**< path to the device */
    struct media_id      media_id; /**< id of the media (if loaded) */
    struct media_info   *dss_media_info;  /**< loaded media info from DSS, if any */
    char                 mnt_path[PATH_MAX]; /**< mount path of the filesystem */
};

/** global structure of available devices and media information
 * (initially, global and static variables are NULL or 0) */
static struct dev_descr *devices;
static int               dev_count;

/** check that device info from DB is consistent with actual status */
static int check_dev_info(const struct dev_descr *dev)
{
    if (dev->dss_dev_info->model == NULL
        || dev->sys_dev_state.lds_model == NULL) {
        if (dev->dss_dev_info->model != dev->sys_dev_state.lds_model)
            LOG_RETURN(-EINVAL, "%s: missing or unexpected device model",
                       dev->dev_path);
        else
            pho_debug("%s: no device model is set", dev->dev_path);

    } else if (strcmp(dev->dss_dev_info->model,
                      dev->sys_dev_state.lds_model) != 0) {
        /* @TODO ignore blanks at the end of the model */
        LOG_RETURN(-EINVAL, "%s: configured device model '%s' differs from "
                   "actual device model '%s'", dev->dev_path,
                   dev->dss_dev_info->model, dev->sys_dev_state.lds_model);
    }

    if (dev->dss_dev_info->serial == NULL
        || dev->sys_dev_state.lds_serial == NULL) {
        if (dev->dss_dev_info->serial != dev->sys_dev_state.lds_serial)
            LOG_RETURN(-EINVAL, "%s: missing or unexpected device serial",
                       dev->dss_dev_info->path);
        else
            pho_debug("%s: no device serial is set", dev->dev_path);
    } else if (strcmp(dev->dss_dev_info->serial,
                      dev->sys_dev_state.lds_serial) != 0) {
        LOG_RETURN(-EINVAL, "%s: configured device serial '%s' differs from "
                   "actual device serial '%s'", dev->dev_path,
                   dev->dss_dev_info->serial, dev->sys_dev_state.lds_serial);
    }

    return 0;
}

/**
 * Retrieve media info from DSS for the given label.
 * @param pmedia[out] returned pointer to a media_info structure
 *                    allocated by this function.
 * @param label[in]   label of the media.
 */
static int lrs_fill_media_info(struct dss_handle *dss,
                               struct media_info **pmedia,
                               const struct media_id *id)
{
    struct dss_crit med_crit[2]; /* criteria on family+id */
    int             med_crit_cnt = 0;
    int             mcnt = 0;
    int             rc;
    struct media_info *media_res = NULL;

    if (id == NULL || pmedia == NULL)
        return -EINVAL;

    pho_debug("Retrieving media info for %s '%s'", dev_family2str(id->type),
              media_id_get(id));
    dss_crit_add(med_crit, &med_crit_cnt, DSS_MDA_family, DSS_CMP_EQ, val_int,
                 id->type);
    dss_crit_add(med_crit, &med_crit_cnt, DSS_MDA_id, DSS_CMP_EQ, val_str,
                 media_id_get(id));

    /* get media info from DB */
    rc = dss_media_get(dss, med_crit, med_crit_cnt, &media_res, &mcnt);
    if (rc)
        return rc;

    if (mcnt == 0) {
        pho_info("No media found matching %s '%s'", dev_family2str(id->type),
                 media_id_get(id));
        GOTO(out_free, rc = -ENOSPC);
    } else if (mcnt > 1)
        LOG_GOTO(out_free, rc = -EINVAL,
                 "Too many media found matching id '%s'",
                 media_id_get(id));

    *pmedia = media_info_dup(media_res);

    pho_debug("%s: spc_free=%zu", media_id_get(&(*pmedia)->id),
              (*pmedia)->stats.phys_spc_free);
    rc = 0;

out_free:
    dss_res_free(media_res, mcnt);
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
 * @param[in]  devi device_info from DB
 * @param[out] devd dev_descr structure filled with all needed information.
 */
static int lrs_fill_dev_info(struct dss_handle *dss, struct lib_adapter *lib,
                             struct dev_descr *devd,
                             const struct dev_info *devi)
{
    int                rc;
    struct dev_adapter deva;

    if (devi == NULL || devd == NULL)
        return -EINVAL;

    devd->dss_dev_info = dev_info_dup(devi);

    rc = get_dev_adapter(devi->family, &deva);
    if (rc)
        return rc;

    /* get path for the given serial */
    rc = ldm_dev_lookup(&deva, devi->serial, devd->dev_path,
                        sizeof(devd->dev_path));
    if (rc)
        return rc;

    /* now query device by path */
    rc = ldm_dev_query(&deva, devd->dev_path, &devd->sys_dev_state);
    if (rc)
        return rc;

    /* compare returned device info with info from DB */
    rc = check_dev_info(devd);
    if (rc)
        return rc;

    /* Query the library about the drive location and whether it contains
     * a media. */
    rc = ldm_lib_drive_lookup(lib, devi->serial, &devd->lib_dev_info);
    if (rc)
        return rc;

    if (devd->lib_dev_info.ldi_full) {
        devd->op_status = PHO_DEV_OP_ST_LOADED;
        devd->media_id = devd->lib_dev_info.ldi_media_id;

        /* get media info for loaded drives */
        rc = lrs_fill_media_info(dss, &devd->dss_media_info, &devd->media_id);
        if (rc)
            return rc;
    } else {
        devd->op_status = PHO_DEV_OP_ST_EMPTY;
    }

    /* If device has been previously marked as loaded, check if it is mounted
     * as a filesystem */
    if (devd->op_status == PHO_DEV_OP_ST_LOADED) {
        struct fs_adapter fsa;

        rc = get_fs_adapter(devd->dss_media_info->fs_type, &fsa);
        if (rc)
            return rc;

        rc = ldm_fs_mounted(&fsa, devd->dev_path, devd->mnt_path,
                            sizeof(devd->mnt_path));

        if (rc == 0)
            devd->op_status = PHO_DEV_OP_ST_MOUNTED;
        else if (rc == -ENOENT)
            /* not mounted, not an error */
            rc = 0;
        else
            LOG_RETURN(rc, "Cannot determine if device '%s' is mounted",
                       devd->dev_path);
    }

    pho_debug("Drive '%s' is '%s'", devd->dev_path,
              op_status2str(devd->op_status));

    return rc;
}

/** Wrap library open operations
 * @param[out] lib  Library handler.
 */
static int wrap_lib_open(enum dev_family dev_type, struct lib_adapter *lib)
{
    int         rc;
    const char *lib_dev;

    /* non-tape cases: dummy lib adapter (no open required) */
    if (dev_type != PHO_DEV_TAPE)
        return get_lib_adapter(PHO_LIB_DUMMY, lib);

    /* tape case */
    rc = get_lib_adapter(PHO_LIB_SCSI, lib);
    if (rc)
        LOG_RETURN(rc, "Failed to get library adapter");

    /* For now, one single configurable path to library device.
     * This will have to be changed to manage multiple libraries.
     */
    lib_dev = pho_cfg_get(PHO_CFG_LRS_lib_device);
    if (!lib_dev)
        LOG_RETURN(rc, "Failed to get default library device from config");

    return ldm_lib_open(lib, lib_dev);
}

/**
 * Load device states into memory.
 * Do nothing if device status is already loaded.
 */
static int lrs_load_dev_state(struct dss_handle *dss)
{
    struct dev_info    *devs = NULL;
    int                 dcnt = 0;
    int                 i, rc;
    /* criteria: host, tape device, device adm_status */
    struct dss_crit     crit[3];
    int                 crit_cnt = 0;
    enum dev_family     family;
    struct lib_adapter  lib;

    if (devices != NULL && dev_count != 0)
        /* already loaded */
        return 0;

    family = default_family();
    if (family == PHO_DEV_INVAL)
        return -EINVAL;

    dss_crit_add(crit, &crit_cnt, DSS_DEV_host, DSS_CMP_EQ, val_str,
                 get_hostname());
    dss_crit_add(crit, &crit_cnt, DSS_DEV_adm_status, DSS_CMP_EQ, val_int,
                 PHO_DEV_ADM_ST_UNLOCKED);
    dss_crit_add(crit, &crit_cnt, DSS_DEV_family, DSS_CMP_EQ, val_int,
                 family);

    /* get all unlocked devices from DB for the given family */
    rc = dss_device_get(dss, crit, crit_cnt, &devs, &dcnt);
    if (rc)
        return rc;
    else if (dcnt == 0) {
        pho_info("No usable device found (%s): check devices status",
                 dev_family2str(family));
        return -EAGAIN;
    }

    dev_count = dcnt;
    devices = (struct dev_descr *)calloc(dcnt, sizeof(*devices));
    if (devices == NULL)
        return -ENOMEM;

    /* get a handle to the library to query it */
    rc = wrap_lib_open(family, &lib);
    if (rc)
        return rc;

    for (i = 0 ; i < dcnt; i++) {
        if (lrs_fill_dev_info(dss, &lib, &devices[i], &devs[i]) != 0)
            devices[i].op_status = PHO_DEV_OP_ST_FAILED;
    }

    /* close handle to the library */
    ldm_lib_close(&lib);

    /* free devs array, as they have been copied to devices[].device */
    dss_res_free(devs, dcnt);
    return 0;
}

/**
 * Get a suitable media for a write operation, and compatible
 * with the given drive model.
 */
static int lrs_select_media(struct dss_handle *dss, struct media_info **p_media,
                            size_t required_size, enum dev_family family,
                            const char *device_model)
{
    int                mcnt = 0;
    int                rc, i;
    struct dss_crit    crit[5];
    int                crit_cnt = 0;
    int                best_idx = -1;
    struct media_info *pmedia_res = NULL;

    /* criteria: family, (model,) adm_status, available size, fs_status */
    dss_crit_add(crit, &crit_cnt, DSS_MDA_family, DSS_CMP_EQ, val_int,
                 family);
    dss_crit_add(crit, &crit_cnt, DSS_MDA_adm_status, DSS_CMP_EQ, val_int,
                 PHO_MDA_ADM_ST_UNLOCKED);
    dss_crit_add(crit, &crit_cnt, DSS_MDA_vol_free, DSS_CMP_GE, val_biguint,
                 required_size);
    /* Exclude non-formatted media */
    dss_crit_add(crit, &crit_cnt, DSS_MDA_fs_status, DSS_CMP_NE, val_int,
                 PHO_FS_STATUS_BLANK);
    /* Exclude full-media */
    dss_crit_add(crit, &crit_cnt, DSS_MDA_fs_status, DSS_CMP_NE, val_int,
                 PHO_FS_STATUS_FULL);

    /**
     * @TODO
     * Use configurable compatility rules to determine writable
     * media models from device_model
     */

    rc = dss_media_get(dss, crit, crit_cnt, &pmedia_res, &mcnt);
    if (rc)
        return rc;

    /* get the best fit */
    for (i = 0; i < mcnt; i++) {
        if (pmedia_res[i].stats.phys_spc_free < required_size)
            continue;
        if (best_idx == -1 ||
            pmedia_res[i].stats.phys_spc_free
                < pmedia_res[best_idx].stats.phys_spc_free)
            best_idx = i;
    }

    if (best_idx == -1) {
        pho_info("No compatible media found to write %zu bytes", required_size);
        GOTO(free_res, rc = -ENOSPC);
    }

    pho_verb("Selected %s '%s': %zu bytes free", dev_family2str(family),
             media_id_get(&pmedia_res[best_idx].id),
             pmedia_res[best_idx].stats.phys_spc_free);

    *p_media = media_info_dup(&pmedia_res[best_idx]);
    if (*p_media == NULL)
        GOTO(free_res, rc = -ENOMEM);

    rc = 0;

free_res:
    dss_res_free(pmedia_res, mcnt);
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
 * @param op_st   Filter devices by the given operational status.
 *                No filtering is op_st is PHO_DEV_OP_ST_UNSPEC.
 * @param select_func    Drive selection function.
 * @param required_size  Required size for the operation.
 */
static struct dev_descr *dev_picker(enum dev_op_status op_st,
                                    device_select_func_t select_func,
                                    size_t required_size)
{
    struct dev_descr *selected = NULL;
    int  i, rc;

    if (devices == NULL)
        return NULL;

    for (i = 0; i < dev_count; i++) {
        if (op_st != PHO_DEV_OP_ST_UNSPEC
            && devices[i].op_status != op_st)
            continue;

        rc = select_func(required_size, &devices[i], &selected);
        if (rc < 0)
            return NULL;
        else if (rc == 0) /* stop searching */
            break;
    }
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
    /* skip failed and busy drives */
    if (dev_curr->op_status == PHO_DEV_OP_ST_FAILED
        || dev_curr->op_status == PHO_DEV_OP_ST_BUSY)
        return 1;

    /* if this function is called, no drive should be empty */
    if (dev_curr->op_status == PHO_DEV_OP_ST_EMPTY) {
        pho_warn("Unexpected drive status for '%s': '%s'",
                 dev_curr->dev_path,
                 op_status2str(dev_curr->op_status));
        return 1;
    }

    /* less space available on this device than the previous ones? */
    if (*dev_selected == NULL || (dev_curr->dss_media_info->stats.phys_spc_free
                      < (*dev_selected)->dss_media_info->stats.phys_spc_free)) {
        *dev_selected = dev_curr;
    }
    return 1;
}

/** Mount the filesystem of a ready device */
static int lrs_mount(struct dev_descr *dev)
{
    char                *mnt_root;
    int                  rc;
    const char          *id;
    struct fs_adapter    fsa;

#if 0
    /**
     * @TODO If library indicates a media is in the drive but the drive
     * doesn't, we need to query the drive to load the tape.
     */
    if (devd->lib_dev_info->ldi_full && !devd->lds_loaded) {
        pho_info("Tape '%s' is located in drive '%s' but is not online: "
                 "querying the drive to load it...",
                 media_id_get(&devd->ldi_media_id), devi->serial);
        rc = ldm_dev_load(&deva, devd->dev_path);
        if (rc)
            LOG_RETURN(rc, "Failed to load tape in drive '%s'",
                       devi->serial);
#endif

    id = basename(dev->dev_path);
    if (id == NULL)
        return -EINVAL;

    /* mount the device as PHO_MNT_PREFIX<id> */
    mnt_root = mount_point(id);
    if (!mnt_root)
        return -ENOMEM;

    pho_verb("Mounting device '%s' as '%s'", dev->dev_path, mnt_root);

    rc = get_fs_adapter(dev->dss_media_info->fs_type, &fsa);
    if (rc)
        goto out_free;

    rc = ldm_fs_mount(&fsa, dev->dev_path, mnt_root);
    if (rc)
        LOG_GOTO(out_free, rc, "Failed to mount device '%s'",
                 dev->dev_path);

    /* update device state and set mount point */
    dev->op_status = PHO_DEV_OP_ST_MOUNTED;
    strncpy(dev->mnt_path,  mnt_root, sizeof(dev->mnt_path));

out_free:
    free(mnt_root);
    if (rc != 0)
        dev->op_status = PHO_DEV_OP_ST_FAILED;
    return rc;
}

/** Unmount the filesystem of a 'mounted' device */
static int lrs_umount(struct dev_descr *dev)
{
    int                rc;
    struct fs_adapter  fsa;

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

    rc = get_fs_adapter(dev->dss_media_info->fs_type, &fsa);
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
 */
static int lrs_load(struct dev_descr *dev, struct media_info *media)
{
    int                  rc, rc2;
    struct lib_adapter   lib;
    struct lib_item_addr media_addr;

    if (dev->op_status != PHO_DEV_OP_ST_EMPTY)
        LOG_RETURN(-EINVAL, "%s: unexpected drive status: status='%s'",
                   dev->dev_path, op_status2str(dev->op_status));

    if (dev->dss_media_info != NULL)
        LOG_RETURN(-EINVAL, "No media expected in device '%s' (found '%s')",
                   dev->dev_path, media_id_get(&dev->dss_media_info->id));

    pho_verb("Loading '%s' into '%s'", media_id_get(&media->id),
             dev->dev_path);

    /* get handle to the library depending on device type */
    rc = wrap_lib_open(dev->dss_dev_info->family, &lib);
    if (rc)
        return rc;

    /* lookup the requested media */
    rc = ldm_lib_media_lookup(&lib, media_id_get(&media->id),
                              &media_addr);
    if (rc)
        LOG_GOTO(out_close, rc, "Media lookup failed");

    rc = ldm_lib_media_move(&lib, &media_addr, &dev->lib_dev_info.ldi_addr);
    if (rc != 0) {
        /* Set operationnal failure state on this drive. It is incomplete since
         * the error can originate from a defect tape too...
         *  - consider marking both as failed.
         *  - consider maintaining lists of errors to diagnose and decide who to
         *    exclude from the cool game. */
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
 * Unload a media from a drive.
 */
static int lrs_unload(struct dev_descr *dev)
{
    int                 rc, rc2;
    struct lib_adapter  lib;
    /* let the library select the target location */
    struct lib_item_addr free_slot = {
        .lia_type = MED_LOC_UNKNOWN,
        .lia_addr = 0
    };

    if (dev->op_status != PHO_DEV_OP_ST_LOADED)
        LOG_RETURN(-EINVAL, "Unexpected drive status for '%s': '%s'",
                   dev->dev_path, op_status2str(dev->op_status));

    if (dev->dss_media_info == NULL)
        LOG_RETURN(-EINVAL, "No media in loaded device '%s'?!",
                   dev->dev_path);

    pho_verb("Unloading '%s' from '%s'", media_id_get(&dev->dss_media_info->id),
             dev->dev_path);

    /* get handle to the library, depending on device type */
    rc = wrap_lib_open(dev->dss_dev_info->family, &lib);
    if (rc)
        return rc;

    rc = ldm_lib_media_move(&lib, &dev->lib_dev_info.ldi_addr, &free_slot);
    if (rc != 0) {
        /* Set operationnal failure state on this drive. It is incomplete since
         * the error can originate from a defect tape too...
         *  - consider marking both as failed.
         *  - consider maintaining lists of errors to diagnose and decide who to
         *    exclude from the cool game. */
        dev->op_status = PHO_DEV_OP_ST_FAILED;
        LOG_GOTO(out_close, rc, "Media move failed");
    }

    /* update device status */
    dev->op_status = PHO_DEV_OP_ST_EMPTY;

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


    policy_str = pho_cfg_get(PHO_CFG_LRS_policy);
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
 * Free one of the devices to allow mounting a new media.
 * @param(in)  dss       Handle to DSS.
 * @param(out) dev_descr Pointer to an empty drive.
 */
static int lrs_free_one_device(struct dss_handle *dss, struct dev_descr **devp)
{
    int rc;

    /* retry loop */
    while (1) {
        /* get a drive to free (PHO_DEV_OP_ST_UNSPEC for any state) */
        *devp = dev_picker(PHO_DEV_OP_ST_UNSPEC, select_drive_to_free, 0);
        if (*devp == NULL)
            /* no drive to free */
            return -EAGAIN;

        if ((*devp)->op_status == PHO_DEV_OP_ST_MOUNTED) {
            /* unmount it */
            rc = lrs_umount(*devp);
            if (rc) {
                /* set it failed and get another device */
                (*devp)->op_status = PHO_DEV_OP_ST_FAILED;
                continue;
            }
        }

        if ((*devp)->op_status == PHO_DEV_OP_ST_LOADED) {
            /* unload the media */
            rc = lrs_unload(*devp);
            if (rc) {
                /* set it failed and get another device */
                (*devp)->op_status = PHO_DEV_OP_ST_FAILED;
                continue;
            }
        }
        if ((*devp)->op_status != PHO_DEV_OP_ST_EMPTY)
            LOG_RETURN(rc = -EINVAL, "Unexpected device status '%s' for '%s': "
                       "should be empty",
                       op_status2str((*devp)->op_status),
                       (*devp)->dev_path);
        /* success: we've got an empty device */
        return 0;
    }
}

/**
 * Get a prepared device to perform a write operation.
 * @param[in]  size  Size of the extent to be written.
 * @param[out] devp  The selected device to write with.
 */
static int lrs_get_write_res(struct dss_handle *dss, size_t size,
                             struct dev_descr **devp)
{
    device_select_func_t dev_select_policy;
    struct media_info *pmedia = NULL;
    int rc = 0;

    rc = lrs_load_dev_state(dss);
    if (rc != 0)
        return rc;

    dev_select_policy = get_dev_policy();
    if (!dev_select_policy)
        return -EINVAL;

    /* 1a) is there a mounted filesystem with enough room? */
    *devp = dev_picker(PHO_DEV_OP_ST_MOUNTED, dev_select_policy, size);
    if (*devp != NULL) {
        /* drive is now in use */
        (*devp)->op_status = PHO_DEV_OP_ST_BUSY;
        /* drive is ready */
        return 0;
    }

    /* 1b) is there a loaded media with enough room? */
    *devp = dev_picker(PHO_DEV_OP_ST_LOADED, dev_select_policy, size);
    if (*devp != NULL) {
        /* mount the filesystem and return */
        rc = lrs_mount(*devp);
        if (rc == 0)
            (*devp)->op_status = PHO_DEV_OP_ST_BUSY;
        return rc;
    }

    /* V00: release a drive and load a tape with enough room.
     * later versions:
     * 2a) is there an idle drive, to eject the loaded tape?
     * 2b) is there an operation that will end soon?
     */

    /* 2) For the next steps, we need a media to write on.
     * It will be loaded into a free drive. */
    pho_verb("Not enough available space on loaded media: "
             "selecting another media");
    rc = lrs_select_media(dss, &pmedia, size, default_family(), NULL);
    if (rc)
        return rc;

    /* 3) is there a free drive? */
    *devp = dev_picker(PHO_DEV_OP_ST_EMPTY, select_any, 0);
    if (*devp == NULL) {
        pho_verb("No free drive: need to unload one");
        rc = lrs_free_one_device(dss, devp);
        if (rc)
            goto out_free;
    }

    /* 4) load the selected media into the selected drive */
    /* On success, target device becomes the owner of pmedia
     * so pmedia must not be released after that. */
    rc = lrs_load(*devp, pmedia);
    if (rc)
        goto out_free;

    /* 5) mount the filesystem */
    /* Don't release media on failure (it is still associated to the drive). */
    rc = lrs_mount(*devp);
    if (rc == 0)
        (*devp)->op_status = PHO_DEV_OP_ST_BUSY;
    return rc;

out_free:
    media_info_free(pmedia);
    return rc;
}

/** set location structure from device information */
static int set_loc_from_dev(const struct dev_descr *dev,
                            struct lrs_intent *intent)
{
    if (dev == NULL || dev->mnt_path == NULL)
        return -EINVAL;

    /* fill intent descriptor with mount point and media info */
    intent->li_location.root_path = strdup(dev->mnt_path);
    intent->li_location.extent.media     = dev->dss_media_info->id;
    intent->li_location.extent.fs_type   = dev->dss_media_info->fs_type;
    intent->li_location.extent.addr_type = dev->dss_media_info->addr_type;
    intent->li_location.extent.address   = PHO_BUFF_NULL;
    return 0;
}

static struct dev_descr *search_loaded_media(const struct media_id *id)
{
    int         i;
    const char *name;

    if (id == NULL)
        return NULL;

    name = media_id_get(id);

    for (i = 0; i < dev_count; i++) {
        if ((devices[i].op_status == PHO_DEV_OP_ST_MOUNTED ||
             devices[i].op_status == PHO_DEV_OP_ST_LOADED)
            && !strcmp(name, media_id_get(&devices[i].media_id)))
            return &devices[i];
    }
    return NULL;
}

static int lrs_media_prepare(struct dss_handle *dss, const struct media_id *id,
                             enum lrs_operation op, struct dev_descr **pdev,
                             struct media_info **pmedia)
{
    const char          *label = media_id_get(id);
    struct dev_descr    *dev;
    struct media_info   *med;
    bool                 post_fs_mount;
    int                  rc;

    *pdev = NULL;
    *pmedia = NULL;

    rc = lrs_fill_media_info(dss, &med, id);
    if (rc != 0)
        return rc;

    switch (op) {
    case LRS_OP_READ:
    case LRS_OP_WRITE:
        if (med->fs_status == PHO_FS_STATUS_BLANK)
            LOG_RETURN(-EINVAL, "Cannot do I/O on unformatted media '%s'",
                       label);
        post_fs_mount = true;
        break;
    case LRS_OP_FORMAT:
        if (med->fs_status != PHO_FS_STATUS_BLANK)
            LOG_RETURN(-EINVAL, "Cannot format non-blank media '%s'", label);
        post_fs_mount = false;
        break;
    default:
        LOG_RETURN(-ENOSYS, "Unknown operation %x", (int)op);
    }

    /* check if the media is already in a drive */
    dev = search_loaded_media(id);
    if (dev == NULL) {
        pho_verb("Media '%s' is not in a drive", media_id_get(id));

        /* Is there a free drive? */
        dev = dev_picker(PHO_DEV_OP_ST_EMPTY, select_any, 0);
        if (dev == NULL) {
            pho_verb("No free drive: need to unload one");
            rc = lrs_free_one_device(dss, &dev);
            if (rc != 0)
                LOG_GOTO(out, rc, "No device available");
        }

        rc = lrs_load(dev, med);
        if (rc != 0)
            goto out;
    }

    /* Mount only for READ/WRITE and if not already mounted */
    if (post_fs_mount && dev->op_status != PHO_DEV_OP_ST_MOUNTED) {
        rc = lrs_mount(dev);
        if (rc == 0)
            dev->op_status = PHO_DEV_OP_ST_BUSY;
    }

out:
    *pmedia = med;
    *pdev = dev;
    return rc;
}


/* see "pho_lrs.h" for function help */

int lrs_format(struct dss_handle *dss, const struct media_id *id,
               enum fs_type fs, bool unlock)
{
    const char          *label = media_id_get(id);
    struct dev_descr    *dev = NULL;
    struct media_info   *media_info;
    int                  rc;
    struct fs_adapter    fsa;

    if (fs != PHO_FS_LTFS)
        LOG_RETURN(-EINVAL, "Unsupported filesystem type");

    rc = lrs_load_dev_state(dss);
    if (rc != 0)
        return rc;

    rc = lrs_media_prepare(dss, id, LRS_OP_FORMAT, &dev, &media_info);
    if (rc != 0)
        return rc;

    if (dev->dss_media_info == NULL)
        LOG_RETURN(rc = -EINVAL, "Invalid device state");

    pho_verb("Format media '%s' as %s", label, fs_type2str(fs));

    rc = get_fs_adapter(fs, &fsa);
    if (rc)
        LOG_RETURN(rc, "Failed to get FS adapter");

    rc = ldm_fs_format(&fsa, dev->dev_path, label);
    if (rc != 0)
        LOG_RETURN(rc, "Cannot format media '%s'", label);

    /* mount the filesystem to get space information */
    rc = lrs_mount(dev);
    if (rc != 0)
        LOG_RETURN(rc, "Failed to mount newly formatted media '%s'", label);

    rc = ldm_fs_df(&fsa, dev->mnt_path, &media_info->stats.phys_spc_used,
                   &media_info->stats.phys_spc_free);
    if (rc != 0)
        LOG_RETURN(rc, "Failed to get usage for media '%s'", label);

    /* now unmount it (ignore unmount error) */
    (void)lrs_umount(dev);

    /* Post operation: update media information in DSS */
    media_info->fs_status = PHO_FS_STATUS_EMPTY;

    if (unlock) {
        pho_verb("Unlocking media '%s'", label);
        media_info->adm_status = PHO_MDA_ADM_ST_UNLOCKED;
    }

    rc = dss_media_set(dss, media_info, 1, DSS_SET_UPDATE);
    if (rc != 0)
        LOG_RETURN(rc, "Failed to update state of media '%s'", label);

    return 0;
}

int lrs_write_prepare(struct dss_handle *dss, size_t size,
                      const struct layout_info *layout,
                      struct lrs_intent *intent)
{
    struct dev_descr    *dev = NULL;
    int                  rc;

    intent->li_operation = LRS_OP_WRITE;

    rc = lrs_get_write_res(dss, size, &dev);
    if (rc != 0)
        return rc;

    if (dev != NULL)
        pho_verb("Writing to media '%s' using device '%s'",
                 media_id_get(&dev->dss_media_info->id), dev->dev_path);

    rc = set_loc_from_dev(dev, intent);
    if (rc != 0)
        LOG_GOTO(err_cleanup, rc, "Cannot set write location");

    /* a single part with the given size */
    intent->li_location.extent.layout_idx = 0;
    intent->li_location.extent.size = size;

err_cleanup:
    if (rc != 0) {
        free(intent->li_location.root_path);
        memset(intent, 0, sizeof(*intent));
    }

    return rc;
}

int lrs_read_prepare(struct dss_handle *dss, const struct layout_info *layout,
                     struct lrs_intent *intent)
{
    struct dev_descr    *dev = NULL;
    struct media_info   *media_info;
    struct media_id     *id;
    int                  rc = 0;

    if (layout->type != PHO_LYT_SIMPLE || layout->ext_count != 1)
        LOG_RETURN(-EINVAL, "Unexpected layout type '%s' or extent count %u",
                   layout_type2str(layout->type), layout->ext_count);

    intent->li_operation = LRS_OP_READ;
    intent->li_location.extent = layout->extents[0];

    rc = lrs_load_dev_state(dss);
    if (rc != 0)
        return rc;

    id = &intent->li_location.extent.media;

    /* Fill in information about media and mount it if needed */
    rc = lrs_media_prepare(dss, id, LRS_OP_READ, &dev, &media_info);
    if (rc)
        return rc;

    if (dev->dss_media_info == NULL)
        LOG_RETURN(rc = -EINVAL, "Invalid device state, expected media %s",
                   media_id_get(id));

    /* set fs_type and addr_type according to media description. */
    intent->li_location.root_path = strdup(dev->mnt_path);
    intent->li_location.extent.fs_type   = dev->dss_media_info->fs_type;
    intent->li_location.extent.addr_type = dev->dss_media_info->addr_type;

    return 0;
}

int lrs_done(struct lrs_intent *intent, int err_code)
{
    struct io_adapter   ioa;
    int                 rc = 0;
    ENTRY;

    if (intent->li_operation != LRS_OP_WRITE)
        goto out_free;

    rc = get_io_adapter(intent->li_location.extent.fs_type, &ioa);
    if (rc != 0)
        LOG_GOTO(out_free, rc,
                 "No suitable I/O adapter for filesystem type '%s'",
                 fs_type2str(intent->li_location.extent.fs_type));

    /* The same IOA must have been used to perform the actual data transfer */
    assert(io_adapter_is_valid(&ioa));

    rc = ioa_flush(&ioa, &intent->li_location);
    if (rc != 0)
        LOG_GOTO(out_free, rc, "Cannot flush media at: %s",
                 intent->li_location.root_path);

    /**
     * @TODO tag tape media as full if err_code = ENOSPC.
     * (trigger umount in this case?).
     */

out_free:
    free(intent->li_location.root_path);
    return rc;
}
