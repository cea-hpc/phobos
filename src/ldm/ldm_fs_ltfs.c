/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 * Copyright 2015-2016 CEA/DAM. All Rights Reserved.
 */
/**
 * \brief  Phobos Local Device Manager: LTFS management.
 *
 * Implement filesystem primitives for LTFS.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "pho_ldm.h"
#include "pho_cfg.h"
#include "pho_common.h"
#include "ldm_common.h"
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

/**
 * Build a command to mount a LTFS filesystem at a given path.
 * The result must be released by the caller using free(3).
 */
static char *ltfs_mount_cmd(const char *device, const char *path)
{
    const char          *cmd_cfg;
    char                *cmd_out;

    cmd_cfg = pho_cfg_get(PHO_CFG_LDM_cmd_mount_ltfs);
    if (cmd_cfg == NULL)
        return NULL;

    if (asprintf(&cmd_out, cmd_cfg, device, path) < 0)
        return NULL;

    return cmd_out;
}

/**
 * Build a command to unmount a LTFS filesystem at a given path.
 * The result must be released by the caller using free(3).
 */
static char *ltfs_umount_cmd(const char *device, const char *path)
{
    const char          *cmd_cfg;
    char                *cmd_out;

    cmd_cfg = pho_cfg_get(PHO_CFG_LDM_cmd_umount_ltfs);
    if (cmd_cfg == NULL)
        return NULL;

    if (asprintf(&cmd_out, cmd_cfg, device, path) < 0)
        return NULL;

    return cmd_out;
}

/**
 * Build a command to format a LTFS filesystem with the given label.
 * The result must be released by the caller using free(3).
 */
static char *ltfs_format_cmd(const char *device, const char *label)
{
    const char          *cmd_cfg;
    char                *cmd_out;

    cmd_cfg = pho_cfg_get(PHO_CFG_LDM_cmd_format_ltfs);
    if (cmd_cfg == NULL)
        return NULL;

    if (asprintf(&cmd_out, cmd_cfg, device, label) < 0)
        return NULL;

    return cmd_out;
}

static int ltfs_collect_output(void *arg, char *line, size_t size, int stream)
{
    if (stream == STDERR_FILENO) {
        pho_verb("%s", rstrip(line));
        return 0;
    }

    /* drop other streams for now */
    return 0;
}

static int ltfs_format_filter(void *arg, char *line, size_t size, int stream)
{
    struct ldm_fs_space *fs_spc = arg;
    int                  rc;

    rc = ltfs_collect_output(arg, line, size, stream);
    if (rc)
        return rc;

    rc = sscanf(line, "LTFS%*uI Volume capacity is %zu GB", &fs_spc->spc_avail);
    if (rc == 1) {
        pho_verb("Formatted media, available space: %zu GB", fs_spc->spc_avail);
        fs_spc->spc_avail *= 1024*1024*1024;  /* convert to bytes */
    }

    return 0;
}

static int ltfs_mount(const char *dev_path, const char *mnt_path)
{
    char    *cmd = NULL;
    int      rc;
    ENTRY;

    cmd = ltfs_mount_cmd(dev_path, mnt_path);
    if (!cmd)
        LOG_GOTO(out_free, rc = -ENOMEM, "Failed to build LTFS mount command");

    /* create the mount point */
    if (mkdir(mnt_path, 0750) != 0 && errno != EEXIST)
        LOG_GOTO(out_free, rc = -errno, "Failed to create mount point %s",
                 mnt_path);

    /* mount the filesystem */
    rc = command_call(cmd, ltfs_collect_output, NULL);
    if (rc)
        LOG_GOTO(out_free, rc, "Mount command failed: '%s'", cmd);

out_free:
    free(cmd);
    return rc;
}

static int ltfs_umount(const char *dev_path, const char *mnt_path)
{
    char    *cmd = NULL;
    int      rc;
    ENTRY;

    cmd = ltfs_umount_cmd(dev_path, mnt_path);
    if (!cmd)
        LOG_GOTO(out_free, rc = -ENOMEM, "Failed to build LTFS umount command");

    /* unmount the filesystem */
    rc = command_call(cmd, ltfs_collect_output, NULL);
    if (rc)
        LOG_GOTO(out_free, rc, "Umount command failed: '%s'", cmd);

out_free:
    free(cmd);
    return rc;
}

static int ltfs_format(const char *dev_path, const char *label,
                       struct ldm_fs_space *fs_spc)
{
    char    *cmd = NULL;
    int      rc;
    ENTRY;

    cmd = ltfs_format_cmd(dev_path, label);
    if (!cmd)
        LOG_GOTO(out_free, rc = -ENOMEM, "Failed to build ltfs_format command");

    if (fs_spc != NULL)
        memset(fs_spc, 0, sizeof(*fs_spc));

    /* Format the media */
    rc = command_call(cmd, ltfs_format_filter, fs_spc);
    if (rc)
        LOG_GOTO(out_free, rc, "ltfs_format command failed: '%s'", cmd);

out_free:
    free(cmd);
    return rc;
}

struct mntent_check_info {
    const char *device;
    char       *mnt_dir;
    size_t      mnt_size;
};

/* fsname for ltfs is 'ltfs:<dev_path>' */
#define LTFS_PREFIX      "ltfs:"
#define LTFS_PREFIX_LEN  strlen(LTFS_PREFIX)

/* fstype for ltfs is 'fuse' */
#define LTFS_FSTYPE      "fuse"

/**
 * Check if a mount entry matches a given device.
 * @retval 0            The mnt entry doesn't match the device (continue to iterateo
 *                      over mount entries).
 * @retval 1            The device matched and the FS type is LTFS.
 * @retval -EMEDIUMTYPE The device matches the mount entry but the FS type
 *                      is not LTFS.
 */
static int _ltfs_mount_check(const struct mntent *mntent, void *cb_data)
{
    struct mntent_check_info *check_info = cb_data;
    ENTRY;

    /* unlike standard filesystems, LTFS appear as 'fuse' fstype
     * and fsname is ltfs:<dev> */
    if (strncmp(mntent->mnt_fsname, LTFS_PREFIX, LTFS_PREFIX_LEN))
        /* not a ltfs filesystem */
        return 0;

    if (strcmp(check_info->device, mntent->mnt_fsname + LTFS_PREFIX_LEN))
        /* device name doesn't match */
        return 0;

    if (strcmp(mntent->mnt_type, LTFS_FSTYPE))
        /* fs type doesn't match */
        LOG_RETURN(-EMEDIUMTYPE, "Device '%s' is mounted with unexpected "
                   "FS type '%s'", mntent->mnt_fsname, mntent->mnt_type);

    strncpy(check_info->mnt_dir, mntent->mnt_dir, check_info->mnt_size);

    /* found it! */
    return 1;
}


static int ltfs_mounted(const char *dev_path, char *mnt_path,
                        size_t mnt_path_size)
{
    struct mntent_check_info check_info = {0};
    int rc;
    ENTRY;

    check_info.device = dev_path;
    check_info.mnt_dir = mnt_path;
    check_info.mnt_size = mnt_path_size;

    rc = mnttab_foreach(_ltfs_mount_check, &check_info);

    switch (rc) {
    case 0:
        /* end of mount tab reached without finding device */
        return -ENOENT;
    case 1:
        /* found the device */
        return 0;
    default:
        /* no other positive value expected */
        assert(rc < 0);
        return rc;
    }
}

static int ltfs_df(const char *path, struct ldm_fs_space *fs_spc)
{
    ssize_t  avail;
    int      rc;

    /* Some LTFS doc says:
     * When the tape cartridge is almost full, further write operations will be
     * prevented.  The free space on the tape (e.g.  from the df command) will
     * indicate that there is still some capacity available, but that is
     * reserved for updating the index.
     *
     * Indeed, we state that LTFS return ENOSPC whereas the previous statfs()
     * call indicated there was enough space to write...
     * We found that this early ENOSPC occured 5% before the expected limit.
     * For now, use an hardcoded value at 5% of the full tape space.
     * A possible enhancement is to turn it to a configurable value.
     *
     * reserved = 5% * total
     * total = used + free
     * avail_space = total - reserved - used
     *             = (used + free) - 5% * (used + free) - used
     *             = 95% free - 5% * used
     */
    rc = common_statfs(path, fs_spc);
    if (rc)
        return rc;

    /** TODO make the 5% threshold configurable */
    avail = (95 * fs_spc->spc_avail - 5 * fs_spc->spc_used) / 100;

    fs_spc->spc_avail = avail > 0 ? avail : 0;
    return 0;
}

struct fs_adapter fs_adapter_ltfs = {
    .fs_mount   = ltfs_mount,
    .fs_umount  = ltfs_umount,
    .fs_format  = ltfs_format,
    .fs_mounted = ltfs_mounted,
    .fs_df      = ltfs_df,
};
