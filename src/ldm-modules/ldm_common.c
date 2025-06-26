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
 * \brief  Common functions for LDM adapters.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "pho_common.h"
#include "ldm_common.h"

#include <assert.h>
#include <inttypes.h>
#include <jansson.h>
#include <sys/user.h>
#include <sys/statfs.h>

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

static int compute_available_space(const char *path, struct statfs stfs,
                                   struct ldm_fs_space *fs_spc)
{
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

int logged_statfs(const char *path, struct ldm_fs_space *fs_spc,
                  json_t **message)
{
    struct phobos_global_context *context = phobos_context();
    struct statfs stfs;

    ENTRY;

    *message = NULL;

    if (path == NULL)
        return -EINVAL;

    if (context->mocks.mock_ltfs.mock_statfs == NULL)
        context->mocks.mock_ltfs.mock_statfs = statfs;

    if (context->mocks.mock_ltfs.mock_statfs(path, &stfs) != 0) {
        *message = json_pack("s++", "statfs('", path, "') failed");
        LOG_RETURN(-errno, "statfs('%s') failed", path);
    }

    return compute_available_space(path, stfs, fs_spc);
}

int simple_statfs(const char *path, struct ldm_fs_space *fs_spc)
{
    struct statfs stfs;

    ENTRY;

    if (path == NULL)
        return -EINVAL;

    if (statfs(path, &stfs) != 0)
        LOG_RETURN(-errno, "statfs('%s') failed", path);

    return compute_available_space(path, stfs, fs_spc);
}

void apply_full_threshold(int full_threshold, struct ldm_fs_space *fs_spc)
{
    ssize_t avail;
    ssize_t free;
    ssize_t used;

    /* Some LTFS doc says:
     * When the tape cartridge is almost full, further write operations will be
     * prevented.  The free space on the tape (e.g.  from the df command) will
     * indicate that there is still some capacity available, but that is
     * reserved for updating the index.
     *
     * Indeed, we state that LTFS return ENOSPC whereas the previous statfs()
     * call indicated there was enough space to write...
     * We found that this early ENOSPC occured 5% before the expected limit.
     *
     * For example, with a threshold of 5% :
     * reserved = 5% * total
     * total = used + free
     * avail_space = total - reserved - used
     *             = (used + free) - 5% * (used + free) - used
     *             = 95% free - 5% * used
     */

    free = ((100 - full_threshold) * fs_spc->spc_avail) / 100;
    used = (full_threshold * fs_spc->spc_used) / 100;
    avail = free - used;

    fs_spc->spc_avail = avail > 0 ? fs_spc->spc_avail : 0;

    /* A full medium cannot be written */
    if (fs_spc->spc_avail == 0)
        fs_spc->spc_flags |= PHO_FS_READONLY;
}
