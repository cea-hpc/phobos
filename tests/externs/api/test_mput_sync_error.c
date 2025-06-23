/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2025 CEA/DAM.
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
 * \brief  Tests for the mput with error on partial release handling
 */

/* phobos stuff */
#include "dss_lock.h"
#include "phobos_store.h"
#include "phobos_admin.h"
#include "pho_common.h" /* get_hostname */
#include "pho_dss.h"
#include "pho_cfg.h"
#include "pho_ldm.h"
#include "pho_test_utils.h"
#include "test_setup.h"

/* standard stuff */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <glib.h>
#include <time.h>

/* cmocka stuff */
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <cmocka.h>

#define FILE_FOR_MPUT "/etc/hosts"
static int object_count;

static void sync_with_error(void **state)
{
    struct pho_xfer_desc xfer = {0};
    int rc;
    int i;

    xfer.xd_op = PHO_XFER_OP_PUT;
    xfer.xd_targets = xcalloc(object_count, sizeof(*xfer.xd_targets));
    xfer.xd_ntargets = object_count;

    for (i = 0; i < object_count; i++) {
        struct stat st;
        char str[32];

        sprintf(str, "hosts.%d", i);

        xfer.xd_targets[i].xt_fd = open(FILE_FOR_MPUT, O_RDONLY);
        assert(xfer.xd_targets[i].xt_fd > 0);

        fstat(xfer.xd_targets[i].xt_fd, &st);
        xfer.xd_targets[i].xt_size = st.st_size;
        xfer.xd_targets[i].xt_objid = xstrdup(str);

        xfer.xd_params.put.family = PHO_RSC_TAPE;
    }

    rc = phobos_put(&xfer, 1, NULL, NULL);
    assert_return_code(rc, -rc);

    for (i = 0; i < object_count; i++) {
        close(xfer.xd_targets[i].xt_fd);
        free(xfer.xd_targets[i].xt_objid);
        free(xfer.xd_targets[i].xt_objuuid);
    }

    free(xfer.xd_targets);
}

int main(int argc, char **argv)
{
    test_env_initialize();

    if (argc != 2) {
        pho_error(EINVAL, "Missing number of object for testing\n");
        exit(EINVAL);
    }

    object_count = str2int64(argv[1]);

    const struct CMUnitTest mput_sync_with_error[] = {
        cmocka_unit_test_setup_teardown(sync_with_error, NULL, NULL),
    };

    return cmocka_run_group_tests(mput_sync_with_error, NULL, NULL);
}
