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
 * \brief  Phobos Local Device Manager: LTFS management.
 *
 * Implement filesystem primitives for LTFS.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "ldm_common.h"
#include "pho_cfg.h"
#include "pho_common.h"
#include "pho_ldm.h"
#include "pho_module_loader.h"

#include <jansson.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#define PLUGIN_NAME     "ltfs"
#define PLUGIN_MAJOR    0
#define PLUGIN_MINOR    1

static struct module_desc FS_ADAPTER_LTFS_MODULE_DESC = {
    .mod_name  = PLUGIN_NAME,
    .mod_major = PLUGIN_MAJOR,
    .mod_minor = PLUGIN_MINOR,
};

/** List of LTFS configuration parameters */
enum pho_cfg_params_ltfs {
    /* LDM parameters */
    PHO_CFG_LTFS_cmd_mount,
    PHO_CFG_LTFS_cmd_umount,
    PHO_CFG_LTFS_cmd_format,
    PHO_CFG_LTFS_cmd_release,
    PHO_CFG_LTFS_tape_full_threshold,

    /* Delimiters, update when modifying options */
    PHO_CFG_LTFS_FIRST = PHO_CFG_LTFS_cmd_mount,
    PHO_CFG_LTFS_LAST  = PHO_CFG_LTFS_tape_full_threshold,
};

/** Definition and default values of LTFS configuration parameters */
const struct pho_config_item cfg_ltfs[] = {
    [PHO_CFG_LTFS_cmd_mount] = {
        .section = "ltfs",
        .name    = "cmd_mount",
        .value   = PHO_LDM_HELPER" mount_ltfs \"%s\" \"%s\""
    },
    [PHO_CFG_LTFS_cmd_umount] = {
        .section = "ltfs",
        .name    = "cmd_umount",
        .value   = PHO_LDM_HELPER" umount_ltfs \"%s\" \"%s\""
    },
    [PHO_CFG_LTFS_cmd_format] = {
        .section = "ltfs",
        .name    = "cmd_format",
        .value   = PHO_LDM_HELPER" format_ltfs \"%s\" \"%s\""
    },
    [PHO_CFG_LTFS_cmd_release] = {
        .section = "ltfs",
        .name    = "cmd_release",
        .value   = PHO_LDM_HELPER" release_ltfs \"%s\""
    },
    [PHO_CFG_LTFS_tape_full_threshold] = {
        .section = "tape",
        .name = "tape_full_threshold",
        .value = "5",
    },
};

char *ltfs_mount_cmd(const char *device, const char *path)
{
    const char          *cmd_cfg;
    char                *cmd_out;

    cmd_cfg = PHO_CFG_GET(cfg_ltfs, PHO_CFG_LTFS, cmd_mount);
    if (cmd_cfg == NULL)
        return NULL;

    if (asprintf(&cmd_out, cmd_cfg, device, path) < 0)
        return NULL;

    return cmd_out;
}

char *ltfs_umount_cmd(const char *device, const char *path)
{
    const char          *cmd_cfg;
    char                *cmd_out;

    cmd_cfg = PHO_CFG_GET(cfg_ltfs, PHO_CFG_LTFS, cmd_umount);
    if (cmd_cfg == NULL)
        return NULL;

    if (asprintf(&cmd_out, cmd_cfg, device, path) < 0)
        return NULL;

    return cmd_out;
}

char *ltfs_format_cmd(const char *device, const char *label)
{
    const char          *cmd_cfg;
    char                *cmd_out;

    cmd_cfg = PHO_CFG_GET(cfg_ltfs, PHO_CFG_LTFS, cmd_format);
    if (cmd_cfg == NULL)
        return NULL;

    if (asprintf(&cmd_out, cmd_cfg, device, label) < 0)
        return NULL;

    return cmd_out;
}

static char *ltfs_release_cmd(const char *device)
{
    const char *cmd_cfg;
    char *cmd_out;

    cmd_cfg = PHO_CFG_GET(cfg_ltfs, PHO_CFG_LTFS, cmd_release);
    if (cmd_cfg == NULL)
        return NULL;

    if (asprintf(&cmd_out, cmd_cfg, device) < 0)
        return NULL;

    return cmd_out;
}

static int ltfs_collect_output(void *arg, char *line, size_t size, int stream)
{
    if (stream == STDERR_FILENO)
        pho_verb("%s", rstrip(line));

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

#define LTFS_VNAME_XATTR    "user.ltfs.volumeName"

static int ltfs_get_label(const char *mnt_path, char *fs_label, size_t llen,
                          json_t **message)
{
    struct phobos_global_context *context = phobos_context();
    ssize_t rc;

    if (message)
        *message = NULL;

    /* labels can (theorically) be as big as PHO_LABEL_MAX_LEN, add one byte */
    if (llen <= PHO_LABEL_MAX_LEN)
        return -EINVAL;

    memset(fs_label, 0, llen);

    if (context->mocks.mock_ltfs.mock_getxattr == NULL)
        context->mocks.mock_ltfs.mock_getxattr = getxattr;

    /* We really want null-termination */
    rc = context->mocks.mock_ltfs.mock_getxattr(mnt_path, LTFS_VNAME_XATTR,
                                                fs_label, llen - 1);
    if (rc < 0) {
        if (message)
            *message = json_pack(
                "{s:s}", "get_label",
                "Failed to get volume name '" LTFS_VNAME_XATTR "'");
        return -errno;
    }

    return 0;
}

static int ltfs_mount(const char *dev_path, const char *mnt_path,
                      const char *fs_label, json_t **message)
{
    struct phobos_global_context *context = phobos_context();
    char vol_label[PHO_LABEL_MAX_LEN + 1];
    char *cmd = NULL;
    int rc;

    ENTRY;

    if (message)
        *message = NULL;

    cmd = ltfs_mount_cmd(dev_path, mnt_path);
    if (!cmd)
        LOG_GOTO(out_free, rc = -ENOMEM, "Failed to build LTFS mount command");

    if (context->mocks.mock_ltfs.mock_mkdir == NULL)
        context->mocks.mock_ltfs.mock_mkdir = mkdir;

    /* create the mount point */
    if (context->mocks.mock_ltfs.mock_mkdir(mnt_path, 0750) != 0 &&
        errno != EEXIST) {
        if (message)
            *message = json_pack("{s:s+}", "mkdir",
                                 "Failed to create mount point: ", mnt_path);
        LOG_GOTO(out_free, rc = -errno, "Failed to create mount point %s",
                 mnt_path);
    }

    if (context->mocks.mock_ltfs.mock_command_call == NULL)
        context->mocks.mock_ltfs.mock_command_call = command_call;

    /* mount the filesystem */
    /* XXX: we do not instrument the "ltfs_collect_output" function to retrieve
     * errors to put into the DSS logs because LTFS writes everything to stderr,
     * so we either have way too much logs to put into the DB, or not enough. So
     * the compromise is to put the minimum in the DB (i.e. "we failed on this
     * command") and have the rest of the log in the daemon log.
     */
    rc = context->mocks.mock_ltfs.mock_command_call(cmd, ltfs_collect_output,
                                                    NULL);
    if (rc) {
        if (message)
            *message = json_pack("{s:s+}", "mount",
                                 "Mount command failed: ", cmd);
        LOG_GOTO(out_free, rc, "Mount command failed: '%s'", cmd);
    }

    /* Checking filesystem label is optional, if fs_label is NULL we are done */
    if (!fs_label || !fs_label[0])
        goto out_free;

    rc = ltfs_get_label(mnt_path, vol_label, sizeof(vol_label), message);
    if (rc)
        LOG_GOTO(out_free, rc, "Cannot retrieve fs label for '%s'", mnt_path);

    if (strcmp(vol_label, fs_label)) {
        if (message)
            *message = json_pack("{s:s+++}", "label mismatch",
                                 "found: ", vol_label, ", expected: ",
                                 fs_label);
        LOG_GOTO(out_free, rc = -EINVAL,
                 "FS label mismatch found:'%s' / expected:'%s'",
                 vol_label, fs_label);
    }

out_free:
    free(cmd);
    return rc;
}

static int ltfs_umount(const char *dev_path, const char *mnt_path,
                       json_t **message)
{
    struct phobos_global_context *context = phobos_context();
    char *cmd = NULL;
    int rc;

    ENTRY;

    if (message)
        *message = NULL;

    cmd = ltfs_umount_cmd(dev_path, mnt_path);
    if (!cmd)
        LOG_GOTO(out_free, rc = -ENOMEM, "Failed to build LTFS umount command");

    if (context->mocks.mock_ltfs.mock_command_call == NULL)
        context->mocks.mock_ltfs.mock_command_call = command_call;

    /* unmount the filesystem */
    rc = context->mocks.mock_ltfs.mock_command_call(cmd, ltfs_collect_output,
                                                    NULL);
    if (rc) {
        if (message)
            *message = json_pack("{s:s+}", "umount",
                                 "Umount command failed: ", cmd);
        LOG_GOTO(out_free, rc, "Umount command failed: '%s'", cmd);
    }

out_free:
    free(cmd);
    return rc;
}

static int ltfs_format(const char *dev_path, const char *label,
                       struct ldm_fs_space *fs_spc, json_t **message)
{
    struct phobos_global_context *context = phobos_context();
    char *cmd = NULL;
    int rc;

    ENTRY;

    if (message)
        *message = NULL;

    cmd = ltfs_format_cmd(dev_path, label);
    if (!cmd)
        LOG_GOTO(out_free, rc = -ENOMEM, "Failed to build ltfs_format command");

    if (fs_spc != NULL)
        memset(fs_spc, 0, sizeof(*fs_spc));

    if (context->mocks.mock_ltfs.mock_command_call == NULL)
        context->mocks.mock_ltfs.mock_command_call = command_call;

    /* Format the media */
    rc = context->mocks.mock_ltfs.mock_command_call(cmd, ltfs_format_filter,
                                                    fs_spc);
    if (rc) {
        if (message)
            *message = json_pack("{s:s+}", "format",
                                 "Format command failed: ", cmd);
        LOG_GOTO(out_free, rc, "Format command failed: '%s'", cmd);
    }

out_free:
    free(cmd);
    return rc;
}

static int ltfs_release(const char *dev_path, json_t **message)
{
    struct phobos_global_context *context = phobos_context();
    char *cmd = NULL;
    int rc;

    ENTRY;

    if (message)
        *message = NULL;

    cmd = ltfs_release_cmd(dev_path);
    if (!cmd)
        LOG_GOTO(out_free, rc = -ENOMEM, "Failed to build %s command",
                 __func__);

    if (context->mocks.mock_ltfs.mock_command_call == NULL)
        context->mocks.mock_ltfs.mock_command_call = command_call;

    /* Release the drive */
    rc = context->mocks.mock_ltfs.mock_command_call(cmd, ltfs_collect_output,
                                                    NULL);
    if (rc) {
        if (message)
            *message = json_pack("{s:s+}", "release",
                                 "Release command failed: ", cmd);
        LOG_GOTO(out_free, rc, "Release command failed: '%s'", cmd);
    }

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
 * @retval 0            The mnt entry doesn't match the device (continue to
 *                      iterate over mount entries).
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

static int ltfs_df(const char *path, struct ldm_fs_space *fs_spc,
                   json_t **message)
{
    json_t *statfs_message = NULL;
    int tape_full_threshold;
    int rc;

    if (message)
        *message = NULL;

    rc = logged_statfs(path, fs_spc, &statfs_message);
    if (rc) {
        if (statfs_message && message)
            *message = json_pack("{s:o}", "df", statfs_message);

        return rc;
    }

    /* get tape_full_threshold from conf */
    tape_full_threshold = PHO_CFG_GET_INT(cfg_ltfs, PHO_CFG_LTFS,
                                          tape_full_threshold, 5);
    if (tape_full_threshold == 0)
        LOG_RETURN(-EINVAL, "Unable to get tape_full_threshold from conf");

    apply_full_threshold(tape_full_threshold, fs_spc);

    return 0;
}

/** Exported fs adapter */
struct pho_fs_adapter_module_ops FS_ADAPTER_LTFS_OPS = {
    .fs_mount     = ltfs_mount,
    .fs_umount    = ltfs_umount,
    .fs_format    = ltfs_format,
    .fs_mounted   = ltfs_mounted,
    .fs_df        = ltfs_df,
    .fs_get_label = ltfs_get_label,
    .fs_release   = ltfs_release,
};

/** FS adapter module registration entry point */
int pho_module_register(void *module, void *context)
{
    struct fs_adapter_module *self = (struct fs_adapter_module *) module;

    phobos_module_context_set(context);

    self->desc = FS_ADAPTER_LTFS_MODULE_DESC;
    self->ops = &FS_ADAPTER_LTFS_OPS;

    return 0;
}
