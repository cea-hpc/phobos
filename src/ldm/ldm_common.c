/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 * Copyright 2015 CEA/DAM. All Rights Reserved.
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
