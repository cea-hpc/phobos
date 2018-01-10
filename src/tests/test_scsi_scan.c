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
#define _GNU_SOURCE
#include "pho_test_utils.h"
#include "../ldm/scsi_api.h"
#include "pho_ldm.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

/** tests of the lib adapter API */
static int msi_helper_print(void *arg, char *msg)
{
    printf("PRINT HELPER: %s | %s\n", (char *)arg, msg);
    return 0;
}

static int msi_helper_print_pho(void *arg, char *msg)
{
    pho_info("%s", msg);
    return 0;
}

static int msi_helper_gstr(void *arg, char *msg)
{
    GString *gstr;

    if (!arg)
        return 0;

    gstr = (GString *)arg;

    g_string_append_printf(gstr, "%s\n", msg);
    return 0;
}

static void test_lib_scan(void)
{
    int rc;
    struct lib_adapter lib = {0};
    GString *gstr = g_string_new("");

    rc = get_lib_adapter(PHO_LIB_SCSI, &lib);
    if (rc)
        exit(EXIT_FAILURE);

    rc = ldm_lib_open(&lib, "/dev/changer");
    if (rc)
        exit(EXIT_FAILURE);

    rc = ldm_lib_scan(&lib, msi_helper_print, "test");
    if (rc)
        exit(EXIT_FAILURE);

    rc = ldm_lib_scan(&lib, msi_helper_print_pho, NULL);
    if (rc)
        exit(EXIT_FAILURE);

    rc = ldm_lib_scan(&lib, msi_helper_gstr, gstr);
    if (rc)
        exit(EXIT_FAILURE);
    printf("GSTR: %s\n", gstr->str);
    g_string_free(gstr, TRUE);

    ldm_lib_close(&lib);
}


int main(int argc, char **argv)
{
    int fd;

    test_env_initialize();

    /* tests of retry loop */

    fd = open("/dev/changer", O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        pho_error(errno, "Cannot open /dev/changer");
        exit(EXIT_FAILURE);
    }

    /* same test with PHO_CFG_LIB_SCSI_sep_sn_query=1 */
    if (setenv("PHOBOS_LIB_SCSI_sep_sn_query", "1", 1)) {
        pho_error(errno, "setenv failed");
        exit(EXIT_FAILURE);
    }

    test_lib_scan();

    exit(EXIT_SUCCESS);
}
