/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 * All rights reserved (c) 2014-2021 CEA/DAM.
 *
 * This file is part of Phobos.
 *
 * Phobos is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * Phobos is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with Phobos. If not, see <http://www.gnu.org/licenses/>.
 */
/**
 * \brief Test undelete API call
 */
#include <stdlib.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "pho_cfg.h"
#include "pho_dss.h"
#include "pho_test_utils.h"
#include "phobos_store.h"

#define ARRAYSIZE(a) (sizeof(a) / sizeof((a)[0]))

static bool test_undelete_null_list(void)
{
    int rc;

    pho_info("Try to undelete with a NULL xfer input list");
    rc = phobos_undelete(NULL, 0);
    if (rc)
        return false;

    return true;
}

static bool test_undelete(void)
{
    struct pho_xfer_desc xfers[2];
    int rc;

    /* test-oid1 */
    xfers[0].xd_params.undel.uuid = "00112233445566778899aabbccddeef1";
    /* test-oid2 */
    xfers[1].xd_params.undel.uuid = "00112233445566778899aabbccddeef2";

    /* undelete */
    pho_info("Try to undelete two xfers");
    rc = phobos_undelete(xfers, 2);
    if (rc)
        return false;

    /* undelete again test-oid1 */
    pho_info("Try to undelete an already existing object");
    rc = phobos_undelete(xfers, 1);
    if (rc != -EEXIST) {
        pho_info("rc is %d instead of %d / -EEXIST", rc, -EEXIST);
        return false;
    }

    return true;
}

int main(void)
{
    bool (*test_function[])(void) = {
        test_undelete_null_list,
        test_undelete,
    };
    struct dss_handle dss_handle;
    bool test_res = true;
    int rc;
    int i;

    test_env_initialize();
    pho_cfg_init_local(NULL);

    rc = dss_init(&dss_handle);
    if (rc) {
        pho_error(rc, "dss_init failed");
        exit(EXIT_FAILURE);
    }

    for (i = 0; i < ARRAYSIZE(test_function); ++i) {
        pho_info("Test %d", i);
        test_res = !test_res ? test_res : test_function[i]();
    }

    dss_fini(&dss_handle);
    exit(test_res ? EXIT_SUCCESS : EXIT_FAILURE);
}
