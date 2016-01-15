/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 * Copyright 2015-2016 CEA/DAM. All Rights Reserved.
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

int common_statfs(const char *path, size_t *spc_used, size_t *spc_free)
{
    struct statfs  stfs;
    ENTRY;

    if (spc_used == NULL || spc_free == NULL)
        return -EINVAL;

    if (statfs(path, &stfs) != 0)
        LOG_RETURN(-errno, "statfs(%s) failed", path);

    /* check df consistency:
     * used = total - free = f_blocks - f_bfree
     * if used + available < 0, there's something wrong
     */
    if (stfs.f_blocks + stfs.f_bavail - stfs.f_bfree < 0)
        LOG_RETURN(-EIO, "statfs(%s) returned inconsistent values: "
                   "blocks=%ld, avail=%ld, free=%ld",
                   path, stfs.f_blocks, stfs.f_bavail, stfs.f_bfree);

    /* used = total - free */
    *spc_used = (stfs.f_blocks - stfs.f_bfree) * stfs.f_bsize;
    /* actually, only available blocks can be written */
    *spc_free = stfs.f_bavail * stfs.f_bsize;

    pho_debug("%s: used=%zu, free=%zu", path, *spc_used, *spc_free);

    return 0;
}
