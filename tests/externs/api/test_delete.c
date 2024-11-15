/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 * All rights reserved (c) 2014-2022 CEA/DAM.
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
 * \brief Test delete API call
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

static bool test_delete_null_list(void)
{
    int rc;

    rc = phobos_delete(NULL, 0);
    if (rc)
        return false;

    return true;
}

static bool test_delete_success(void)
{
    struct pho_xfer_target targets[] = {
        { .xt_objid = "test-oid1" },
        { .xt_objid = "test-oid2" },
        { .xt_objid = "test-oid3" }
    };
    struct pho_xfer_desc xfers[] = {
        { .xd_ntargets = 1,
          .xd_targets = &targets[0],
        },
        { .xd_ntargets = 1,
          .xd_targets = &targets[1],
        },
        { .xd_ntargets = 1,
          .xd_targets = &targets[2],
        }
    };
    int rc;

    /* process the first xfer element */
    rc = phobos_delete(xfers, 1);
    if (rc)
        return false;

    free(xfers[0].xd_targets->xt_objuuid);

    /* process the other xfer elements */
    rc = phobos_delete(xfers + 1, 2);
    if (rc)
        return false;

    free(xfers[1].xd_targets->xt_objuuid);
    free(xfers[2].xd_targets->xt_objuuid);

    return true;
}

static bool test_delete_failure(void)
{
    struct pho_xfer_target target = { .xt_objid = "not-an-object" };
    struct pho_xfer_desc xfer = { .xd_ntargets = 1, .xd_targets = &target };
    int rc;

    rc = phobos_delete(&xfer, 1);
    if (rc != -ENOENT)
        return false;

    return true;
}

int main(void)
{
    bool (*test_function[])(void) = {
        test_delete_null_list,
        test_delete_success,
        test_delete_failure,
    };
    struct dss_handle dss_handle;
    bool test_res = true;
    int rc;
    int i;

    test_env_initialize();

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
