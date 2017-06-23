/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2017 CEA/DAM.
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
 * \brief  Phobos Local Device Manager: FS calls for inplace directories.
 *
 * Implement filesystem primitives for a directory.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "pho_ldm.h"
#include "pho_common.h"
#include "ldm_common.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

static int dir_present(const char *dev_path, char *mnt_path,
                       size_t mnt_path_size)
{
    struct stat st;
    ENTRY;

    if (stat(dev_path, &st) != 0)
        LOG_RETURN(-errno, "stat() failed on '%s'", dev_path);

    if (!S_ISDIR(st.st_mode))
        LOG_RETURN(-ENOTDIR, "'%s' is not a directory", dev_path);

    strncpy(mnt_path, dev_path, mnt_path_size);
    /* make sure mnt_path is null terminated */
    mnt_path[mnt_path_size-1] = '\0';

    return 0;
}

static char *get_label_path(const char *dir_path)
{
    char    *res;
    int      rc;

    rc = asprintf(&res, "%s/.phobos_dir_label", dir_path);
    if (rc < 0)
        return NULL;

    return res;
}

/**
 * Note that we don't actually format the directory in the mkltfs sense.
 * This operation is exposed for consistency with other media types.
 * It will perform the following operation:
 *   - Make sure the filesystem was not previously labelled;
 *   - Label it;
 *   - Fill the used/free space structure.
 */
static int dir_format(const char *dev_path, const char *label,
                      struct ldm_fs_space *fs_spc)
{
    char    *label_path = get_label_path(dev_path);
    int      fd;
    ssize_t  rc;
    ENTRY;

    if (!label_path)
        LOG_RETURN(-ENOMEM, "Cannot create filesystem label at '%s'", dev_path);

    fd = open(label_path, O_CREAT | O_EXCL | O_WRONLY, S_IRUSR | S_IWUSR);
    if (fd < 0)
        LOG_GOTO(out_free, rc = -errno, "Cannot create file at '%s'", dev_path);

    rc = write(fd, label, strlen(label));
    if (rc < 0)
        LOG_GOTO(out_close, rc = -errno, "Cannot set label at '%s'", dev_path);

    if (fs_spc) {
        memset(fs_spc, 0, sizeof(*fs_spc));
        rc = common_statfs(dev_path, fs_spc);
    }

out_close:
    close(fd);

out_free:
    free(label_path);
    return rc;
}

static int dir_get_label(const char *mnt_path, char *fs_label, size_t llen)
{
    char    *label_path = get_label_path(mnt_path);
    int      fd;
    ssize_t  rc;

    if (!label_path)
        LOG_RETURN(-ENOMEM, "Cannot lookup filesystem label at '%s'", mnt_path);

    fd = open(label_path, O_RDONLY);
    if (fd < 0)
        LOG_GOTO(out_free, rc = -errno, "Cannot open label: '%s'", label_path);

    rc = read(fd, fs_label, llen - 1);
    if (rc < 0)
        LOG_GOTO(out_close, rc = -errno, "Cannot read label: '%s'", label_path);

    fs_label[rc] = '\0';

out_close:
    close(fd);

out_free:
    free(label_path);
    return rc;
}

/**
 * Pseudo mount function. Does not actually mount anything but check the
 * filesystem label, to comply with the behavior of other backends.
 */
static int dir_labelled(const char *dev_path, const char *mnt_path,
                        const char *fs_label)
{
    char    mounted_label[PHO_LABEL_MAX_LEN + 1];
    int     rc;

    rc = dir_get_label(mnt_path, mounted_label, sizeof(mounted_label));
    if (rc)
        LOG_RETURN(rc, "Cannot retrieve label on '%s'", mnt_path);

    rc = strcmp(mounted_label, fs_label);
    if (rc)
        LOG_RETURN(-EINVAL, "Label mismatch on '%s': expected:'%s' found:'%s'",
                    mnt_path, fs_label, mounted_label);

    return 0;
}

struct fs_adapter fs_adapter_posix = {
    .fs_mount     = dir_labelled,
    .fs_umount    = NULL,
    .fs_format    = dir_format,
    .fs_mounted   = dir_present,
    .fs_df        = common_statfs,
    .fs_get_label = dir_get_label,
};
