/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2026 CEA/DAM.
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
 * \brief  Test that a failure of the posix reader/writer initialization is
 * reported by phobos_get/phobos_put instead of being swallowed.
 *
 * A closed xfer fd makes the dup() call of set_private_posix_io_descr fail
 * with EBADF. The error used to be overwritten by the success of the
 * io_block_size configuration retrieval in layout_decoder/layout_encoder,
 * leaving a destroyed data processor behind: the first processor step then
 * dereferenced its NULL private_reader/private_writer and crashed.
 */
#include "config.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "pho_common.h"
#include "pho_test_utils.h"
#include "pho_test_xfer_utils.h"
#include "phobos_store.h"

#define PHO_TMP_DIR_TEMPLATE "/tmp/pho_XXXXXX"
#define RM_OPEN_FDS 16
#define OBJID_GET "test_bad_fd_get_obj"
#define OBJID_PUT "test_bad_fd_put_obj"

/** Assert an rc is 0 or print a pretty error and exit.
 * The local variable must not shadow the asserted expression: the test
 * compiler would then read the uninitialized local instead of the caller's
 * variable when it is called with a same-named one (e.g. ASSERT_RC(rc)).
 */
#define ASSERT_RC(rc_expr) do {                                         \
        int _assert_rc = (rc_expr);                                     \
        if (_assert_rc) {                                               \
            pho_error(_assert_rc, "%s:%d:%s", __FILE__, __LINE__,       \
                      #rc_expr);                                        \
            exit(EXIT_FAILURE);                                         \
        }                                                               \
    } while (0)

/** Assert an rc equals the expected value or print a pretty error and exit */
#define ASSERT_RC_EQ(rc_expr, expected) do {                            \
        int _assert_rc = (rc_expr);                                     \
        if (_assert_rc != (expected)) {                                 \
            pho_error(_assert_rc, "%s:%d:%s: expected rc %d",           \
                      __FILE__, __LINE__, #rc_expr, (expected));        \
            exit(EXIT_FAILURE);                                         \
        }                                                               \
    } while (0)

static char *TMP_DIR;

static int rm_anything(const char *pathname, const struct stat *sbuf, int type,
                       struct FTW *ftwb)
{
    if (remove(pathname) < 0) {
        perror("ERROR: remove");
        return -1;
    }
    return 0;
}

static void rm_tmp_dir(void)
{
    if (!TMP_DIR)
        return;
    /* Recursive rm */
    nftw(TMP_DIR, rm_anything, RM_OPEN_FDS, FTW_DEPTH | FTW_MOUNT | FTW_PHYS);
    free(TMP_DIR);
}

static char *setup_tmp_dir(void)
{
    TMP_DIR = xmalloc(sizeof(PHO_TMP_DIR_TEMPLATE));
    strcpy(TMP_DIR, PHO_TMP_DIR_TEMPLATE);
    assert(mkdtemp(TMP_DIR) != NULL);
    atexit(rm_tmp_dir);
    return TMP_DIR;
}

/**
 * Return a fd number that is guaranteed to stay closed, so that dup() on it
 * fails with EBADF in the posix reader/writer initialization. Pick a number
 * far above the current fd usage: the connections phobos opens afterwards
 * get the lowest free fds and can never reuse it. Exit if none is found.
 */
static int pick_closed_fd(void)
{
    int fd;

    for (fd = 128; fd < 1024; fd++) {
        errno = 0;
        if (fcntl(fd, F_GETFD) == -1 && errno == EBADF)
            return fd;
    }

    pho_error(-EMFILE, "no closed fd number found below 1024");
    exit(EXIT_FAILURE);
}

static void reinit_xfer(struct pho_xfer_desc *xfer, const char *path,
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

/** Replace the xfer fd with a closed fd, to make dup() fail on it */
static void set_closed_fd(struct pho_xfer_desc *xfer, int bad_fd)
{
    close(xfer->xd_targets->xt_fd);
    xfer->xd_targets->xt_fd = bad_fd;
}

int main(int argc, char **argv)
{
    struct pho_xfer_desc   xfer = {0};
    struct pho_xfer_target target = {0};
    char                  *tmp_dir = setup_tmp_dir();
    char                   bad_get_path[256];
    char                   dst_path[256];
    int                    rc;
    int                    bad_fd;

    test_env_initialize();

    bad_fd = pick_closed_fd();

    /* xt_fd starts closed, so that the first reinit_xfer does not close fd 0 */
    target.xt_fd = -1;
    xfer.xd_targets = &target;

    /* Reference PUT: the test binary itself is the source object */
    reinit_xfer(&xfer, argv[0], OBJID_GET, PHO_XFER_OP_PUT);
    rc = phobos_put(&xfer, 1, NULL, NULL);
    ASSERT_RC(rc);
    ASSERT_RC(xfer.xd_rc);

    /* GET with a closed fd: the posix writer initialization fails on
     * dup(EBADF), the error must be reported instead of being swallowed.
     */
    snprintf(bad_get_path, sizeof(bad_get_path), "%s/get_bad_fd", tmp_dir);
    reinit_xfer(&xfer, bad_get_path, OBJID_GET, PHO_XFER_OP_GET);
    set_closed_fd(&xfer, bad_fd);

    rc = phobos_get(&xfer, 1, NULL, NULL);
    ASSERT_RC_EQ(rc, -EBADF);
    ASSERT_RC_EQ(xfer.xd_rc, -EBADF);

    /* do not let the final cleanup close the already closed fd number */
    xfer.xd_targets->xt_fd = -1;
    unlink(bad_get_path);

    /* PUT with a closed fd: the posix reader initialization fails on
     * dup(EBADF), the error must be reported as well.
     */
    reinit_xfer(&xfer, argv[0], OBJID_PUT, PHO_XFER_OP_PUT);
    set_closed_fd(&xfer, bad_fd);

    rc = phobos_put(&xfer, 1, NULL, NULL);
    ASSERT_RC_EQ(rc, -EBADF);
    ASSERT_RC_EQ(xfer.xd_rc, -EBADF);

    xfer.xd_targets->xt_fd = -1;

    /* a subsequent GET with a valid fd must still succeed: the failed
     * transfers must not leave corrupted state behind
     */
    snprintf(dst_path, sizeof(dst_path), "%s/get_ok", tmp_dir);
    reinit_xfer(&xfer, dst_path, OBJID_GET, PHO_XFER_OP_GET);
    rc = phobos_get(&xfer, 1, NULL, NULL);
    ASSERT_RC(rc);
    ASSERT_RC(xfer.xd_rc);

    free((void *)xfer.xd_targets->xt_objid);
    free(xfer.xd_targets->xt_objuuid);
    xfer_close_fd(xfer.xd_targets);

    return EXIT_SUCCESS;
}
