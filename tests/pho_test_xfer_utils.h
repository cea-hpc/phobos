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
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

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

    memset(xfer, 0, sizeof(*xfer));

    if (path == NULL) {
        xfer->xd_fd = -1;
        return -EINVAL;
    }

    xfer->xd_op = op;
    xfer->xd_flags = flags;

    if (xfer->xd_op == PHO_XFER_OP_GET)
        /* Set file permission to 666 and let user umask filter unwanted bits */
        xfer->xd_fd = open(path, xfer2open_flags(xfer->xd_flags), 0666);
    else
        xfer->xd_fd = open(path, O_RDONLY);

    if (xfer->xd_fd < 0)
        LOG_RETURN(-errno, "open(%s) failed", path);

    if (xfer->xd_op == PHO_XFER_OP_PUT) {
        fstat(xfer->xd_fd, &st);
        xfer->xd_params.put.size = st.st_size;
    }

    return xfer->xd_fd;
}

/**
 * Close the file descriptor contained in the xfer dsecriptor data structure.
 * Return 0 on success, -errno on failure.
 */
static int xfer_desc_close_fd(struct pho_xfer_desc *xfer)
{
    int rc;

    if (xfer->xd_fd >= 0) {
        rc = close(xfer->xd_fd);
        if (rc)
            return -errno;
    }

    return 0;
}

#endif

