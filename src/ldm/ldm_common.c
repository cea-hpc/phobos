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
 * \brief  Common functions for LDM adapters.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "pho_common.h"
#include "ldm_common.h"

#include <sys/user.h>
#include <assert.h>
#include <sys/statfs.h>
#include <inttypes.h>

int mnttab_foreach(mntent_cb_t cb_func, void *cb_data)
{
    struct mntent  mnt_ent;
    struct mntent *p_mnt;
    FILE          *fp;
    int            rc = 0;
    char           mnt_buff[PAGE_SIZE];

    assert(cb_func != NULL);

    fp = setmntent(_PATH_MOUNTED, "r");

    if (fp == NULL)
        LOG_RETURN(-errno, "Failed to open mount table");

    while ((p_mnt = getmntent_r(fp, &mnt_ent, mnt_buff,
                                sizeof(mnt_buff))) != NULL) {
        pho_debug("mount tab: fs='%s', type='%s'", p_mnt->mnt_fsname,
                  p_mnt->mnt_type);
        rc = cb_func(p_mnt, cb_data);
        if (rc)
            goto out_close;
    }

out_close:
    endmntent(fp);
    return rc;
}

static inline size_t statfs_spc_used(const struct statfs *stfs)
{
    return (stfs->f_blocks - stfs->f_bfree) * stfs->f_bsize;
}

static inline size_t statfs_spc_free(const struct statfs *stfs)
{
    return stfs->f_bavail * stfs->f_bsize;
}

int common_statfs(const char *path, struct ldm_fs_space *fs_spc)
{
    struct statfs  stfs;
    ENTRY;

    if (path == NULL)
        return -EINVAL;

    if (statfs(path, &stfs) != 0)
        LOG_RETURN(-errno, "statfs(%s) failed", path);

    /* check df consistency:
     * used = total - free = f_blocks - f_bfree
     * if used + available < 0, there's something wrong
     */
    if (stfs.f_blocks + stfs.f_bavail < stfs.f_bfree)
        LOG_RETURN(-EIO, "statfs(%s) returned inconsistent values: "
                   "blocks=%ld, avail=%ld, free=%ld",
                   path, stfs.f_blocks, stfs.f_bavail, stfs.f_bfree);

    if (fs_spc != NULL) {
        /* used = total - free */
        fs_spc->spc_used = statfs_spc_used(&stfs);

        /* actually, only available blocks can be written */
        fs_spc->spc_avail = statfs_spc_free(&stfs);

        /* Are we R/O? */
        fs_spc->spc_flags = 0;

#if HAVE_ST_RDONLY
        if (stfs.f_flags & ST_RDONLY)
            fs_spc->spc_flags |= PHO_FS_READONLY;
#else
        /* oh RHEL, oh despair, oh age my enemy!
         * f_flags and ST_RDONLY only available since 2.6.36, ie. RHEL7
         * Let's not do too much voodoo to figure things out, assume it's
         * writable for now and catch -EROFS later on... */
#endif
    }

    pho_debug("%s: used=%zu, free=%zu",
              path, statfs_spc_used(&stfs), statfs_spc_free(&stfs));

    return 0;
}
