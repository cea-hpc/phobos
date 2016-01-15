/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 * Copyright 2015-2016 CEA/DAM. All Rights Reserved.
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

static int dir_present(const char *dev_path, char *mnt_path,
                       size_t mnt_path_size)
{
    struct stat st;
    ENTRY;

    if (stat(dev_path, &st) != 0)
        LOG_RETURN(-errno, "lstat() failed on '%s'", dev_path);

    if (!S_ISDIR(st.st_mode))
        LOG_RETURN(-ENOTDIR, "'%s' is not a directory", dev_path);

    strncpy(mnt_path, dev_path, mnt_path_size);
    /* make sure mnt_path is null terminated */
    mnt_path[mnt_path_size-1] = '\0';

    return 0;
}

struct fs_adapter fs_adapter_posix = {
    .fs_mount   = NULL,
    .fs_umount  = NULL,
    .fs_format  = NULL,
    .fs_mounted = dir_present,
    .fs_df      = common_statfs,
};
