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
 * \brief   Testsuite xfer structure helpers. This file was a part of phobos
 *          until the xfer file descriptors were managed by the CLI.
 */

#ifndef _PHO_TEST_XFER_UTILS_H
#define _PHO_TEST_XFER_UTILS_H

/* Standard includes */
#include <assert.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <ftw.h>

/* Local Phobos includes */
#include "pho_common.h"
#include "phobos_store.h"

/**
 * Select the open flags depending on the operation made on the object
 * when the file is created.
 * Return the selected flags.
 */
static int xfer2open_flags(enum pho_xfer_flags flags)
{
    return (flags & PHO_XFER_OBJ_REPLACE) ? O_CREAT|O_WRONLY|O_TRUNC
                                          : O_CREAT|O_WRONLY|O_EXCL;
}

/**
 * Open the file descriptor contained in the xfer descriptor data structure
 * using path, op and flags.
 * Return the file descriptor.
 */
static int xfer_desc_open_path(struct pho_xfer_desc *xfer, const char *path,
                               enum pho_xfer_op op, enum pho_xfer_flags flags)
{
    struct stat st;

    memset(xfer->xd_targets, 0, sizeof(*xfer->xd_targets));

    if (path == NULL) {
        xfer->xd_targets->xt_fd = -1;
        return -EINVAL;
    }

    xfer->xd_op = op;
    xfer->xd_flags = flags;
    xfer->xd_ntargets = 1;

    if (xfer->xd_op == PHO_XFER_OP_GET)
        /* Set file permission to 666 and let user umask filter unwanted bits */
        xfer->xd_targets->xt_fd = open(path, xfer2open_flags(xfer->xd_flags),
                                       0666);
    else
        xfer->xd_targets->xt_fd = open(path, O_RDONLY);

    if (xfer->xd_targets->xt_fd < 0)
        LOG_RETURN(-errno, "open(%s) failed", path);

    if (xfer->xd_op == PHO_XFER_OP_PUT) {
        fstat(xfer->xd_targets->xt_fd, &st);
        xfer->xd_targets->xt_size = st.st_size;
    }

    return xfer->xd_targets->xt_fd;
}

/**
 * Close the file descriptor contained in the xfer dsecriptor data structure.
 * Return 0 on success, -errno on failure.
 */
static int xfer_close_fd(struct pho_xfer_target *target)
{
    int rc;

    if (target->xt_fd >= 0) {
        rc = close(target->xt_fd);
        if (rc)
            return -errno;
    }

    return 0;
}

/** Template of the temporary directory created by setup_tmp_dir() */
#define PHO_TMP_DIR_TEMPLATE "/tmp/pho_XXXXXX"

/** Max number of file descriptors kept open by nftw() in rm_tmp_dir() */
#define RM_OPEN_FDS 16

static char *TMP_DIR;

/**
 * Remove anything, as a callback of nftw(): rm_tmp_dir() walks the
 * temporary directory tree bottom-up and removes every entry.
 */
static inline int rm_anything(const char *pathname, const struct stat *sbuf,
                              int type, struct FTW *ftwb)
{
    if (remove(pathname) < 0) {
        perror("ERROR: remove");
        return -1;
    }
    return 0;
}

/** Recursively remove the temporary directory created by setup_tmp_dir() */
static inline void rm_tmp_dir(void)
{
    if (!TMP_DIR)
        return;
    /* Recursive rm */
    nftw(TMP_DIR, rm_anything, RM_OPEN_FDS, FTW_DEPTH | FTW_MOUNT | FTW_PHYS);
    free(TMP_DIR);
}

/** Create a temporary directory, recursively removed at process exit */
static inline char *setup_tmp_dir(void)
{
    TMP_DIR = xmalloc(sizeof(PHO_TMP_DIR_TEMPLATE));
    strcpy(TMP_DIR, PHO_TMP_DIR_TEMPLATE);
    assert(mkdtemp(TMP_DIR) != NULL);
    atexit(rm_tmp_dir);
    return TMP_DIR;
}

/**
 * Free the object id, uuid and file descriptor of the current operation,
 * then open the xfer for a new operation on the given path, with the given
 * object id.
 */
static inline void reinit_xfer(struct pho_xfer_desc *xfer, const char *path,
                               const char *objid, enum pho_xfer_op op)
{
    free((void *)xfer->xd_targets->xt_objid);
    free(xfer->xd_targets->xt_objuuid);
    close(xfer->xd_targets->xt_fd);

    xfer_desc_open_path(xfer, path, op, 0);
    xfer->xd_op = op;
    /* xd_params is a union: reset it, so that the parameters of the previous
     * operation are not read as the parameters of the new one (a put family
     * resolved to PHO_RSC_DIR was read as the get copy_name pointer).
     */
    memset(&xfer->xd_params, 0, sizeof(xfer->xd_params));
    xfer->xd_targets->xt_objid = xstrdup(objid);
    if (op == PHO_XFER_OP_PUT)
        xfer->xd_params.put.family = PHO_RSC_INVAL;
}

#endif

