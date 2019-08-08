/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2019 CEA/DAM.
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
 * \brief   Store related internal helpers.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* This module's header */
#include "pho_store_utils.h"

/* Standard includes */
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

/* Local Phobos includes */
#include "pho_common.h"

/**
 * Try to open a file with O_NOATIME flag.
 * Perform a standard open if it doesn't succeed.
 */
static int open_noatime(const char *path, int flags)
{
    int fd;

    fd = open(path, flags | O_NOATIME);
    /* not allowed to open with NOATIME arg, try without */
    if (fd < 0 && errno == EPERM)
        fd = open(path, flags & ~O_NOATIME);

    return fd;
}

static int xfer2open_flags(enum pho_xfer_flags flags)
{
    return (flags & PHO_XFER_OBJ_REPLACE) ? O_CREAT|O_WRONLY|O_TRUNC
                                          : O_CREAT|O_WRONLY|O_EXCL;
}

int pho_xfer_desc_get_fd(struct pho_xfer_desc *xfer)
{
    if (xfer->xd_fd >= 0)
        return xfer->xd_fd;

    if (xfer->xd_fpath == NULL)
        return -EINVAL;

    if (xfer->xd_op == PHO_XFER_OP_GET)
        /* Set file permission to 666 and let user umask filter unwanted bits */
        xfer->xd_fd = open(xfer->xd_fpath, xfer2open_flags(xfer->xd_flags),
                           0666);
    else
        xfer->xd_fd = open_noatime(xfer->xd_fpath, O_RDONLY);

    if (xfer->xd_fd < 0)
        LOG_RETURN(-errno, "open(%s) failed", xfer->xd_fpath);
    xfer->xd_close_fd = true;

    return xfer->xd_fd;
}

ssize_t pho_xfer_desc_get_size(struct pho_xfer_desc *xfer)
{
    struct stat st;
    int fd;
    int rc;

    if (xfer->xd_size >= 0)
        return xfer->xd_size;

    fd = pho_xfer_desc_get_fd(xfer);
    if (fd < 0)
        return fd;

    rc = fstat(fd, &st);
    if (rc < 0)
        LOG_RETURN(-errno, "stat(%s) failed", xfer->xd_fpath);

    xfer->xd_size = st.st_size;

    return xfer->xd_size;
}

