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

#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <assert.h>
#include <sys/utsname.h>

/**
 * Build a command to query drive info.
 * The result must be released by the caller using free(3).
 */
static char *drive_query_cmd(const char *device)
{
    const char *cmd_cfg = NULL;
    char *cmd_out;

    if (pho_cfg_get(PHO_CFG_LRS_cmd_drive_query, &cmd_cfg))
        return NULL;

    if (asprintf(&cmd_out, cmd_cfg, device) < 0)
        return NULL;

    return cmd_out;
}

/**
 * Build a command to mount a filesystem at a given path.
 * The result must be released by the caller using free(3).
 */
static char *mount_cmd(const char *device, const char *path)
{
    const char *cmd_cfg = NULL;
    char *cmd_out;

    if (pho_cfg_get(PHO_CFG_LRS_cmd_mount, &cmd_cfg))
        return NULL;

    if (asprintf(&cmd_out, cmd_cfg, device, path) < 0)
        return NULL;

    return cmd_out;
}

/**
 * Build a mount path with the given index.
 * The result must be released by the caller using free(3).
 */
static char *mount_point(int idx)
{
    assert(idx >= 0);

    const char *mnt_cfg = NULL;
    char *mnt_out;

    if (pho_cfg_get(PHO_CFG_LRS_mount_prefix, &mnt_cfg))
        return NULL;

    /* mount the device as PHO_MNT_PREFIX<changer_idx> */
    if (asprintf(&mnt_out, "%s%d", mnt_cfg, idx) < 0)
        return NULL;

    return mnt_out;
}

/** return the default device family to write data */
static enum dev_family default_family(void)
{
    const char *fam_str = NULL;

    if (pho_cfg_get(PHO_CFG_LRS_default_family, &fam_str) || (fam_str == NULL))
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
            pho_error(errno, "failed to get host name");
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
    struct dev_info     device; /**< device description (from DSS) */
    struct dev_state    state;  /**< device state (from system) */
    struct media_info  *media;  /**< loaded media, if any */
};

/** global structure of available devices and media information
 * (initially, global and static variables are NULL or 0) */
static struct dev_descr *devices;
static int               dev_count;

/** check than device info from DB is consistent with actual status */
static int check_dev_info(const struct dev_descr *dev)
{
    if (dev->device.model == NULL || dev->state.model == NULL) {
        if (dev->device.model != dev->state.model)
            LOG_RETURN(-EINVAL, "%s: missing or unexpected device model",
                       dev->device.path);
        else
            pho_debug("%s: no device model is set", dev->device.path);

    } else if (strcmp(dev->device.model, dev->state.model) != 0) {
        /* @TODO ignore blanks at the end of the model */
        LOG_RETURN(-EINVAL, "%s: configured device model '%s' differs from "
                   "actual device model '%s'", dev->device.path,
                   dev->device.model, dev->state.model);
    }

    if (dev->device.serial == NULL || dev->state.serial == NULL) {
        if (dev->device.serial != dev->state.serial)
            LOG_RETURN(-EINVAL, "%s: missing or unexpected device serial",
                       dev->device.path);
        else
            pho_debug("%s: no device serial is set", dev->device.path);
    } else if (strcmp(dev->device.serial, dev->state.serial) != 0) {
        LOG_RETURN(-EINVAL, "%s: configured device serial '%s' differs from "
                   "actual device serial '%s'", dev->device.path,
                   dev->device.serial, dev->state.serial);
    }

    return 0;
};

/** concatenate a command output */
static int collect_output(void *cb_arg, char *line, size_t size)
{
    g_string_append_len((GString *)cb_arg, line, size);
    return 0;
}

/**
 * Retrieve media info from DSS for the given label.
 * @param pmedia[out] returned pointer to a media_info structure
 *                    allocated by this function.
 * @param label[in]   label of the media.
 */
static int lrs_fill_media_info(void *dss_hdl, struct media_info **pmedia,
                               const struct media_id *id)
{
    struct dss_crit med_crit[2]; /* criteria on family+id */
    int             med_crit_cnt = 0;
    int             mcnt = 0;
    int             rc;

    if (id == NULL || pmedia == NULL)
        return -EINVAL;

    pho_debug("Retrieving media info for %s '%s'", dev_family2str(id->type),
              media_id_get(id));
    dss_crit_add(med_crit, &med_crit_cnt, DSS_MDA_family, DSS_CMP_EQ, val_int,
                 id->type);
    dss_crit_add(med_crit, &med_crit_cnt, DSS_MDA_id, DSS_CMP_EQ, val_str,
                 media_id_get(id));

    /* get media info from DB */
    rc = dss_media_get(dss_hdl, med_crit, med_crit_cnt, pmedia, &mcnt);
    if (rc)
        return rc;

    if (mcnt == 0) {
        pho_info("No media found matching %s '%s'", dev_family2str(id->type),
                 media_id_get(id));
        dss_res_free(pmedia, mcnt);
        return -ENOENT;
    }

    pho_debug("%s: spc_free=%zu", media_id_get(&(*pmedia)->id),
              (*pmedia)->stats.phys_spc_free);
    return 0;
}

/**
 * Retrieve device information from system and complementary info from DB.
 * - check DB device info is consistent with mtx output.
 * - get operationnal status from system (loaded or not).
 * - for loaded drives, the mounted volume + LTFS mount point, if mounted.
 * - get media information from DB for loaded drives.
 *
 * @param[in]  dss_hdl handle to dss connection
 * @param[in]  devi device_info from DB
 * @param[out] devd dev_descr structure filled with all needed information.
 */
static int lrs_fill_dev_info(void *dss_hdl, struct dev_descr *devd,
                             const struct dev_info *devi)
{
    char    *cmd = NULL;
    GString *cmd_out = NULL;
    int      rc;

    if (devi == NULL || devd == NULL)
        return -EINVAL;

    devd->device = *devi;

    cmd = drive_query_cmd(devi->path);
    if (!cmd)
        LOG_GOTO(out, rc = -ENOMEM, "failed to build drive info command");

    cmd_out = g_string_new("");

    /* @TODO skip a step by reading JSON directly from a stream */

    /* retrieve physical device state */
    rc = command_call(cmd, collect_output, cmd_out);
    if (rc)
        LOG_GOTO(out, rc, "command failed: '%s'", cmd);

    /* parse command output */
    rc = device_state_from_json(cmd_out->str, &devd->state);
    if (rc != 0)
        GOTO(out, rc = -EINVAL);

    /* compared returned info with info from DB */
    if (check_dev_info(devd) != 0)
        GOTO(out, rc = -EINVAL);

    pho_debug("Drive '%s' is '%s'", devi->path,
              op_status2str(devd->state.op_status));

    /* get media info for loaded drives */
    if ((devd->state.op_status == PHO_DEV_OP_ST_LOADED)
       || (devd->state.op_status == PHO_DEV_OP_ST_MOUNTED))
        rc = lrs_fill_media_info(dss_hdl, &devd->media, &devd->state.media_id);

out:
    free(cmd);
    if (cmd_out != NULL)
        g_string_free(cmd_out, TRUE);
    return rc;
}

/**
 * Load device states into memory.
 * Do nothing if device status is already loaded.
 */
static int lrs_load_dev_state(void *dss_hdl)
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
    rc = dss_device_get(dss_hdl, crit, crit_cnt, &devs, &dcnt);
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
        if (lrs_fill_dev_info(dss_hdl, &devices[i], &devs[i]) != 0)
            devices[i].state.op_status = PHO_DEV_OP_ST_FAILED;
    }

    /* free devs array, as they have been copied to devices[].device */
    dss_res_free(devs, dcnt);
    return 0;
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
        if (devices[i].state.op_status != op_st)
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

/** Mount the filesystem of a ready device */
static int lrs_mount(struct dev_descr *dev)
{
    char    *cmd      = NULL;
    char    *mnt_root = NULL;
    GString *cmd_out  = NULL;
    int      rc;

    /* mount the device as PHO_MNT_PREFIX<changer_idx> */
    mnt_root = mount_point(dev->device.changer_idx);
    if (!mnt_root)
        return -ENOMEM;

    if (mkdir(mnt_root, 640) != 0 && errno != EEXIST)
        LOG_GOTO(out_free, rc = -errno, "Failed to create mount point %s",
                 mnt_root);

    pho_verb("mounting device '%s' as '%s'",
             dev->device.path, mnt_root);

    cmd = mount_cmd(dev->device.path, mnt_root);
    if (!cmd)
        LOG_GOTO(out_free, rc = -ENOMEM, "Failed to build mount command");

    cmd_out = g_string_new("");

    /* mount the filesystem */
    rc = command_call(cmd, collect_output, cmd_out);
    if (rc)
        LOG_GOTO(out_free, rc, "command failed: '%s'", cmd);

    /* update device state and set mount point */
    dev->state.op_status = PHO_DEV_OP_ST_MOUNTED;
    dev->state.mnt_path = mnt_root;

out_free:
    if (rc != 0)
        free(mnt_root);
    /* else: the memory is now owned by dev->state */

    free(cmd);
    if (cmd_out != NULL)
        g_string_free(cmd_out, TRUE);
    return rc;
}

/** return the device policy function depending on configuration */
static device_select_func_t get_dev_policy(void)
{
    const char *policy_str;

    if (pho_cfg_get(PHO_CFG_LRS_policy, &policy_str))
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
 * Get a prepared device to perform a write operation.
 * @param[in]  size size of the extent to write.
 * @param[out] devp the selected device
 */
static int lrs_get_write_res(void *dss_hdl, size_t size,
                             struct dev_descr **devp)
{
    device_select_func_t dev_select_policy;
    int rc = 0;

    rc = lrs_load_dev_state(dss_hdl);
    if (rc != 0)
        return rc;

    dev_select_policy = get_dev_policy();
    if (!dev_select_policy)
        return -EINVAL;

    /* 1a) is there a mounted filesystem with enough room? */
    *devp = dev_picker(PHO_DEV_OP_ST_MOUNTED, dev_select_policy, size);
    if (*devp != NULL) {
        /* drive is ready */
        return 0;
    }

    /* 1b) is there a loaded tape with enough room? */
    *devp = dev_picker(PHO_DEV_OP_ST_LOADED, dev_select_policy, size);
    if (*devp != NULL)
        /* mount the filesystem and return */
        return lrs_mount(*devp);

    /* 2) For the next steps, we need a media to write on.
     * It will be loaded to a free drive. */
    /* @TODO get a writable media from DSS */

    /* 3) is there a free drive? */
    *devp = dev_picker(PHO_DEV_OP_ST_EMPTY, select_any, 0);
    if (*devp == NULL) {
        /* @TODO no free drive: must unload one */
        pho_verb("No free drive: must unload one");
    }

    /* at this point we have a free drive, now load the selected media */

    /* V00: 3) release a drive and load a tape with enough room */

    /* later versions: */
    /* 3) is there an idle drive, to eject the loaded tape? */
    /* 4) is there an operation that will end soon? */

    /* load the device, mount the filesystem... */

    /* no free resources */
    *devp = NULL;
    return -EAGAIN;
}

/** set location structure from device information */
static int set_loc_from_dev(struct data_loc *loc,
                            const struct dev_descr *dev)
{
    if (dev == NULL || dev->state.mnt_path == NULL)
        return -EINVAL;

    /* fill data_loc structure with mount point and media info */
    loc->root_path = strdup(dev->state.mnt_path);
    loc->extent.media = dev->media->id;
    loc->extent.fs_type = dev->media->fs_type;
    loc->extent.addr_type = dev->media->addr_type;
    loc->extent.address = PHO_BUFF_NULL;

    return 0;
}

/* see "pho_lrs.h" for function help */
int lrs_write_intent(void *dss_hdl, size_t size,
                     const struct layout_descr *layout,
                     struct data_loc *loc)
{
    struct dev_descr *dev = NULL;
    int rc;

    rc = lrs_get_write_res(dss_hdl, size, &dev);
    if (rc != 0)
        return rc;

    if (dev != NULL)
        pho_verb("writing to media '%s' using device '%s'",
                 media_id_get(&dev->media->id),
                 dev->device.path);

    rc = set_loc_from_dev(loc, dev);
    if (rc)
        return rc; /* FIXME release resources? */

    /* a single part with the given size */
    loc->extent.layout_idx = 0;
    loc->extent.size = size;

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
        if ((devices[i].state.op_status == PHO_DEV_OP_ST_MOUNTED ||
             devices[i].state.op_status == PHO_DEV_OP_ST_LOADED)
            && (devices[i].media != NULL)
            && !strcmp(name, media_id_get(&devices[i].media->id)))
            return &devices[i];
    }
    return NULL;
}


int lrs_read_intent(void *dss_hdl, const struct layout_descr *layout,
                    struct data_loc *loc)
{
    int               rc = 0;
    struct dev_descr *dev = NULL;

    rc = lrs_load_dev_state(dss_hdl);
    if (rc != 0)
        return rc;

    /* check if the media is in already in a drive */
    dev = search_loaded_media(&loc->extent.media);

    /** @TODO find a free or idle drive */
    if (dev == NULL)
        return -ENOTSUP;

    /* mount the FS if it is not mounted */
    if (dev->state.op_status != PHO_DEV_OP_ST_MOUNTED) {
        rc = lrs_mount(dev);
        if (rc)
            return rc;
    }
    loc->root_path = strdup(dev->state.mnt_path);

    return 0;
}

int lrs_done(void *dss_hdl, struct data_loc *loc)
{
#ifdef _TEST
    return 0;
#else
    return -ENOTSUP;
#endif
}
