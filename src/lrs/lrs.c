/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 * Copyright 2014-2015 CEA/DAM. All Rights Reserved.
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
 * Build a mount path with the given index.
 * The result must be released by the caller using free(3).
 */
static char *mount_point(int idx)
{
    const char  *mnt_cfg;
    char        *mnt_out;

    assert(idx >= 0);

    mnt_cfg = pho_cfg_get(PHO_CFG_LRS_mount_prefix);
    if (mnt_cfg == NULL)
        return NULL;

    /* mount the device as PHO_MNT_PREFIX<changer_idx> */
    if (asprintf(&mnt_out, "%s%d", mnt_cfg, idx) < 0)
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
    struct dev_info    *device; /**< device description (from DSS) */
    struct dev_state    state;  /**< device state (from system) */
    struct media_info  *media;  /**< loaded media, if any */
};

/** global structure of available devices and media information
 * (initially, global and static variables are NULL or 0) */
static struct dev_descr *devices;
static int               dev_count;

/** check that device info from DB is consistent with actual status */
static int check_dev_info(const struct dev_descr *dev)
{
    if (dev->device->model == NULL || dev->state.model == NULL) {
        if (dev->device->model != dev->state.model)
            LOG_RETURN(-EINVAL, "%s: missing or unexpected device model",
                       dev->device->path);
        else
            pho_debug("%s: no device model is set", dev->device->path);

    } else if (strcmp(dev->device->model, dev->state.model) != 0) {
        /* @TODO ignore blanks at the end of the model */
        LOG_RETURN(-EINVAL, "%s: configured device model '%s' differs from "
                   "actual device model '%s'", dev->device->path,
                   dev->device->model, dev->state.model);
    }

    if (dev->device->serial == NULL || dev->state.serial == NULL) {
        if (dev->device->serial != dev->state.serial)
            LOG_RETURN(-EINVAL, "%s: missing or unexpected device serial",
                       dev->device->path);
        else
            pho_debug("%s: no device serial is set", dev->device->path);
    } else if (strcmp(dev->device->serial, dev->state.serial) != 0) {
        LOG_RETURN(-EINVAL, "%s: configured device serial '%s' differs from "
                   "actual device serial '%s'", dev->device->path,
                   dev->device->serial, dev->state.serial);
    }

    return 0;
};

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
 * @param[in]  dss  handle to dss connection
 * @param[in]  devi device_info from DB
 * @param[out] devd dev_descr structure filled with all needed information.
 */
static int lrs_fill_dev_info(struct dss_handle *dss, struct dev_descr *devd,
                             const struct dev_info *devi)
{
    int rc;

    if (devi == NULL || devd == NULL)
        return -EINVAL;

    devd->device = dev_info_dup(devi);

    rc = ldm_device_query(devi->family, devi->path, &devd->state);
    if (rc)
        return rc;

    /* compared returned info with info from DB */
    rc = check_dev_info(devd);
    if (rc)
        return rc;

    pho_debug("Drive '%s' is '%s'", devi->path,
              op_status2str(devd->state.op_status));

    /* get media info for loaded drives */
    if ((devd->state.op_status == PHO_DEV_OP_ST_LOADED)
       || (devd->state.op_status == PHO_DEV_OP_ST_MOUNTED))
        rc = lrs_fill_media_info(dss, &devd->media, &devd->state.media_id);

    return rc;
}

/**
 * Load device states into memory.
 * Do nothing if device status is already loaded.
 */
static int lrs_load_dev_state(struct dss_handle *dss)
{
    struct dev_info *devs = NULL;
    int              dcnt = 0;
    int              i, rc;
    /* criteria: host, tape device, device adm_status */
    struct dss_crit  crit[3];
    int              crit_cnt = 0;
    enum dev_family  family;

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

    for (i = 0 ; i < dcnt; i++) {
        if (lrs_fill_dev_info(dss, &devices[i], &devs[i]) != 0)
            devices[i].state.op_status = PHO_DEV_OP_ST_FAILED;
    }

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
            && devices[i].state.op_status != op_st)
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
    if (dev_curr->media == NULL)
        return 1;

    if (dev_curr->media->stats.phys_spc_free >= required_size) {
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
    if (dev_curr->media == NULL)
        return 1;

    /* does it fit? */
    if (dev_curr->media->stats.phys_spc_free < required_size)
        return 1;

    /* no previous fit, or better fit */
    if (*dev_selected == NULL || (dev_curr->media->stats.phys_spc_free
                              < (*dev_selected)->media->stats.phys_spc_free)) {
        *dev_selected = dev_curr;

        if (required_size == dev_curr->media->stats.phys_spc_free)
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
    if (dev_curr->state.op_status == PHO_DEV_OP_ST_FAILED
        || dev_curr->state.op_status == PHO_DEV_OP_ST_BUSY)
        return 1;

    /* if this function is called, no drive should be empty */
    if (dev_curr->state.op_status == PHO_DEV_OP_ST_EMPTY) {
        pho_warn("Unexpected drive status for '%s': '%s'",
                 dev_curr->device->path,
                 op_status2str(dev_curr->state.op_status));
        return 1;
    }

    /* less space available on this device than the previous ones? */
    if (*dev_selected == NULL || (dev_curr->media->stats.phys_spc_free
                              < (*dev_selected)->media->stats.phys_spc_free)) {
        *dev_selected = dev_curr;
    }
    return 1;
}

/** Mount the filesystem of a ready device */
static int lrs_mount(struct dev_descr *dev)
{
    char    *mnt_root = NULL;
    int      rc;

    /* mount the device as PHO_MNT_PREFIX<changer_idx> */
    mnt_root = mount_point(dev->device->changer_idx);
    if (!mnt_root)
        return -ENOMEM;

    pho_verb("Mounting device '%s' as '%s'",
             dev->device->path, mnt_root);

    rc = ldm_fs_mount(dev->media->fs_type, dev->device->path, mnt_root);
    if (rc)
        LOG_GOTO(out_free, rc, "Failed to mount device '%s'",
                 dev->device->path);

    /* update device state and set mount point */
    dev->state.op_status = PHO_DEV_OP_ST_MOUNTED;
    dev->state.mnt_path = mnt_root;

out_free:
    if (rc != 0)
        free(mnt_root);
    /* else: the memory is now owned by dev->state */
    return rc;
}

/** Unmount the filesystem of a 'mounted' device */
static int lrs_umount(struct dev_descr *dev)
{
    int      rc;

    if (dev->state.op_status != PHO_DEV_OP_ST_MOUNTED)
        LOG_RETURN(-EINVAL, "Unexpected drive status for '%s': '%s'",
                   dev->device->path, op_status2str(dev->state.op_status));

    if (dev->state.mnt_path == NULL)
        LOG_RETURN(-EINVAL, "No mount point for mounted device '%s'?!",
                   dev->device->path);

    if (dev->media == NULL)
        LOG_RETURN(-EINVAL, "No media in mounted device '%s'?!",
                   dev->device->path);

    pho_verb("Unmounting device '%s' mounted as '%s'",
             dev->device->path, dev->state.mnt_path);

    rc = ldm_fs_umount(dev->media->fs_type, dev->device->path,
                        dev->state.mnt_path);
    if (rc)
        LOG_RETURN(rc, "Failed to umount device '%s' mounted as '%s'",
                   dev->device->path, dev->state.mnt_path);

    /* update device state and unset mount path */
    dev->state.op_status = PHO_DEV_OP_ST_LOADED;
    free(dev->state.mnt_path);
    dev->state.mnt_path = NULL;

    return 0;
}
/**
 * Load a media into a drive.
 */
static int lrs_load(struct dev_descr *dev, struct media_info *media)
{
    int rc;

    if (dev->state.op_status != PHO_DEV_OP_ST_EMPTY)
        LOG_RETURN(-EINVAL, "%s: unexpected drive status: status='%s'",
                   dev->device->path, op_status2str(dev->state.op_status));

    if (dev->media != NULL)
        LOG_RETURN(-EINVAL, "No media expected in device '%s' (found '%s')",
                   dev->device->path, media_id_get(&dev->media->id));

    pho_verb("Loading '%s' into '%s'", media_id_get(&media->id),
             dev->device->path);

    rc = ldm_device_load(dev->device->family, dev->device->path,
                         &media->id);
    if (rc)
        return rc;

    /* update device status */
    dev->state.op_status = PHO_DEV_OP_ST_LOADED;
    /* associate media to this device */
    dev->media = media;

    return 0;
}

/**
 * Unload a media from a drive.
 */
static int lrs_unload(struct dev_descr *dev)
{
    int rc;

    if (dev->state.op_status != PHO_DEV_OP_ST_LOADED)
        LOG_RETURN(-EINVAL, "Unexpected drive status for '%s': '%s'",
                   dev->device->path, op_status2str(dev->state.op_status));

    if (dev->media == NULL)
        LOG_RETURN(-EINVAL, "No media in loaded device '%s'?!",
                   dev->device->path);

    pho_verb("Unloading '%s' from '%s'", media_id_get(&dev->media->id),
             dev->device->path);

    rc = ldm_device_unload(dev->device->family, dev->device->path,
                           &dev->media->id);
    if (rc)
        return rc;

    /* update device status */
    dev->state.op_status = PHO_DEV_OP_ST_EMPTY;

    /* free media resources */
    media_info_free(dev->media);
    dev->media = NULL;
    return 0;
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

        if ((*devp)->state.op_status == PHO_DEV_OP_ST_MOUNTED) {
            /* unmount it */
            rc = lrs_umount(*devp);
            if (rc) {
                /* set it failed and get another device */
                (*devp)->state.op_status = PHO_DEV_OP_ST_FAILED;
                continue;
            }
        }

        if ((*devp)->state.op_status == PHO_DEV_OP_ST_LOADED) {
            /* unload the media */
            rc = lrs_unload(*devp);
            if (rc) {
                /* set it failed and get another device */
                (*devp)->state.op_status = PHO_DEV_OP_ST_FAILED;
                continue;
            }
        }
        if ((*devp)->state.op_status != PHO_DEV_OP_ST_EMPTY)
            LOG_RETURN(rc = -EINVAL, "Unexpected device status '%s' for '%s': "
                       "should be empty",
                       op_status2str((*devp)->state.op_status),
                       (*devp)->device->path);
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
        (*devp)->state.op_status = PHO_DEV_OP_ST_BUSY;
        /* drive is ready */
        return 0;
    }

    /* 1b) is there a loaded media with enough room? */
    *devp = dev_picker(PHO_DEV_OP_ST_LOADED, dev_select_policy, size);
    if (*devp != NULL) {
        /* mount the filesystem and return */
        rc = lrs_mount(*devp);
        if (rc == 0)
            (*devp)->state.op_status = PHO_DEV_OP_ST_BUSY;
        else
            (*devp)->state.op_status = PHO_DEV_OP_ST_FAILED;
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
    return lrs_mount(*devp);

out_free:
    media_info_free(pmedia);
    return rc;
}

/** set location structure from device information */
static int set_loc_from_dev(const struct dev_descr *dev,
                            struct lrs_intent *intent)
{
    if (dev == NULL || dev->state.mnt_path == NULL)
        return -EINVAL;

    /* fill intent descriptor with mount point and media info */
    intent->li_location.root_path = strdup(dev->state.mnt_path);
    intent->li_location.extent.media     = dev->media->id;
    intent->li_location.extent.fs_type   = dev->media->fs_type;
    intent->li_location.extent.addr_type = dev->media->addr_type;
    intent->li_location.extent.address   = PHO_BUFF_NULL;
    return 0;
}

/* see "pho_lrs.h" for function help */
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
                 media_id_get(&dev->media->id), dev->device->path);

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

static struct dev_descr *search_loaded_media(const struct media_id *id)
{
    int         i;
    const char *name;

    if (id == NULL)
        return NULL;

    name = media_id_get(id);

    for (i = 0; i < dev_count; i++) {
        if ((devices[i].state.op_status == PHO_DEV_OP_ST_MOUNTED ||
             devices[i].state.op_status == PHO_DEV_OP_ST_LOADED)
            && (devices[i].media != NULL)
            && !strcmp(name, media_id_get(&devices[i].media->id)))
            return &devices[i];
    }
    return NULL;
}


int lrs_read_prepare(struct dss_handle *dss, const struct layout_info *layout,
                     struct lrs_intent *intent)
{
    struct dev_descr *dev = NULL;
    int               rc = 0;

    if (layout->type != PHO_LYT_SIMPLE || layout->ext_count != 1)
        LOG_RETURN(-EINVAL, "Unexpected layout type '%s' or extent count %u",
                   layout_type2str(layout->type), layout->ext_count);

    intent->li_operation = LRS_OP_READ;
    intent->li_location.extent = layout->extents[0];

    rc = lrs_load_dev_state(dss);
    if (rc != 0)
        return rc;

    /* check if the media is already in a drive */
    dev = search_loaded_media(&intent->li_location.extent.media);
    if (dev == NULL) {
        pho_verb("Media '%s' is not in a drive",
                 media_id_get(&intent->li_location.extent.media));

        /* @TODO retrieve media information from DSS */

        /* is there a free drive? */
        dev = dev_picker(PHO_DEV_OP_ST_EMPTY, select_any, 0);
        if (dev == NULL) {
            pho_verb("No free drive: need to unload one");
            rc = lrs_free_one_device(dss, &dev);
            if (rc)
                return rc;
        }

        /* @TODO load the media in the selected drive */
    }

    /* mount the FS if it is not mounted */
    if (dev->state.op_status != PHO_DEV_OP_ST_MOUNTED) {
        rc = lrs_mount(dev);
        if (rc == 0) {
            dev->state.op_status = PHO_DEV_OP_ST_BUSY;
        } else {
            dev->state.op_status = PHO_DEV_OP_ST_FAILED;
            return rc;
        }
    }

    if (dev->media == NULL)
        LOG_RETURN(rc = -EINVAL, "Invalid device state");

    /* set fs_type and addr_type according to media description. */
    intent->li_location.root_path = strdup(dev->state.mnt_path);
    intent->li_location.extent.fs_type   = dev->media->fs_type;
    intent->li_location.extent.addr_type = dev->media->addr_type;

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
