/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2021 CEA/DAM.
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
 * \brief  Tests for DSS generic lock feature
 */

#include "pho_dss.h"

#include <assert.h>
#include <errno.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cmocka.h>

static const char *GOOD_LOCK_OWNER = "dummy_owner";

static int setup(void **state)
{
    struct dss_handle *handle;
    int rc;

    handle = malloc(sizeof(*handle));
    if (handle == NULL)
        return -1;

    setenv("PHOBOS_DSS_connect_string", "dbname=phobos host=localhost "
                                        "user=phobos password=phobos", 1);

    if (!fork()) {
        rc = execl("../setup_db.sh", "setup_db.sh", "setup_tables", NULL);
        if (rc)
            exit(EXIT_FAILURE);
    }

    wait(&rc);
    if (rc)
        return -1;

    rc = dss_init(handle);
    if (rc)
        return -1;

    *state = handle;

    return 0;
}

static int teardown(void **state)
{
    int rc;

    if (*state != NULL) {
        dss_fini(*state);
        free(*state);
    }

    if (!fork()) {
        rc = execl("../setup_db.sh", "setup_db.sh", "drop_tables", NULL);
        if (rc)
            exit(EXIT_FAILURE);
    }

    wait(&rc);
    if (rc)
        return -1;

    unsetenv("PHOBOS_DSS_connect_string");

    return 0;
}

static void dss_lock_ok(void **state)
{
    struct dss_handle *handle = (struct dss_handle *)*state;
    static const char *OTHER_LOCK_ID = "oid_dummy2";
    static const char *GOOD_LOCK_ID = "oid_dummy";
    int rc;

    rc = dss_lock(handle, GOOD_LOCK_ID, GOOD_LOCK_OWNER);
    assert_return_code(rc, -rc);

    rc = dss_lock(handle, OTHER_LOCK_ID, GOOD_LOCK_OWNER);
    assert_return_code(rc, -rc);
}

static void dss_lock_exists(void **state)
{
    struct dss_handle *handle = (struct dss_handle *)*state;
    static const char *OTHER_LOCK_OWNER = "dummy_owner2";
    static const char *GOOD_LOCK_ID = "oid_dummy3";
    int rc;

    rc = dss_lock(handle, GOOD_LOCK_ID, GOOD_LOCK_OWNER);
    assert_return_code(rc, -rc);

    rc = dss_lock(handle, GOOD_LOCK_ID, GOOD_LOCK_OWNER);
    assert_int_equal(rc, -EEXIST);

    rc = dss_lock(handle, GOOD_LOCK_ID, OTHER_LOCK_OWNER);
    assert_int_equal(rc, -EEXIST);
}

int main(void)
{
    const struct CMUnitTest dss_lock_test_cases[] = {
        cmocka_unit_test(dss_lock_ok),
        cmocka_unit_test(dss_lock_exists),
    };

    return cmocka_run_group_tests(dss_lock_test_cases, setup, teardown);
}
