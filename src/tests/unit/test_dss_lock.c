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
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cmocka.h>

static struct object_info GOOD_LOCK = {
    .oid = "oid_dummy"
};
static const char *FORMATED_GOOD_LOCK_ID = "object_oid_dummy";
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

static void dss_lock_unlock_ok(void **state)
{
    struct dss_handle *handle = (struct dss_handle *)*state;
    static struct object_info OTHER_LOCK_ID = { .oid = "oid_dummy2" };
    int rc;

    rc = dss_lock(handle, DSS_OBJECT, &GOOD_LOCK, 1, GOOD_LOCK_OWNER);
    assert_return_code(rc, -rc);

    rc = dss_lock(handle, DSS_OBJECT, &OTHER_LOCK_ID, 1, GOOD_LOCK_OWNER);
    assert_return_code(rc, -rc);

    rc = dss_unlock(handle, DSS_OBJECT, &GOOD_LOCK, 1, GOOD_LOCK_OWNER);
    assert_return_code(rc, -rc);

    rc = dss_unlock(handle, DSS_OBJECT, &OTHER_LOCK_ID, 1, NULL);
    assert_return_code(rc, -rc);
}

static void dss_lock_exists(void **state)
{
    struct dss_handle *handle = (struct dss_handle *)*state;
    static const char *OTHER_LOCK_OWNER = "dummy_owner2";
    int rc;

    rc = dss_lock(handle, DSS_OBJECT, &GOOD_LOCK, 1, GOOD_LOCK_OWNER);
    assert_return_code(rc, -rc);

    rc = dss_lock(handle, DSS_OBJECT, &GOOD_LOCK, 1, GOOD_LOCK_OWNER);
    assert_int_equal(rc, -EEXIST);

    rc = dss_lock(handle, DSS_OBJECT, &GOOD_LOCK, 1, OTHER_LOCK_OWNER);
    assert_int_equal(rc, -EEXIST);

    assert(dss_unlock(handle, DSS_OBJECT, &GOOD_LOCK, 1, NULL) == 0);
}

static void dss_refresh_ok(void **state)
{
    struct dss_handle *handle = (struct dss_handle *)*state;
    struct timeval old_ts = { .tv_sec = 0, .tv_usec = 0 };
    struct timeval new_ts = { .tv_sec = 0, .tv_usec = 0 };
    int rc;

    assert(dss_lock(handle, DSS_OBJECT, &GOOD_LOCK, 1, GOOD_LOCK_OWNER) == 0);

    rc = dss_lock_status(handle, FORMATED_GOOD_LOCK_ID, NULL, &old_ts);
    assert_int_equal(rc, -rc);

    rc = dss_lock_refresh(handle, FORMATED_GOOD_LOCK_ID, GOOD_LOCK_OWNER);
    assert_int_equal(rc, -rc);

    rc = dss_lock_status(handle, FORMATED_GOOD_LOCK_ID, NULL, &new_ts);
    assert_int_equal(rc, -rc);

    assert_memory_not_equal(&old_ts, &new_ts, sizeof(old_ts));

    assert(dss_unlock(handle, DSS_OBJECT, &GOOD_LOCK, 1, GOOD_LOCK_OWNER) == 0);
}

static void dss_refresh_not_exists(void **state)
{
    struct dss_handle *handle = (struct dss_handle *)*state;
    char *BAD_LOCK_ID = "not_exists";
    int rc;

    assert(dss_lock(handle, DSS_OBJECT, &GOOD_LOCK, 1, GOOD_LOCK_OWNER) == 0);

    rc = dss_lock_refresh(handle, BAD_LOCK_ID, GOOD_LOCK_OWNER);
    assert_int_equal(rc, -ENOLCK);

    assert(dss_unlock(handle, DSS_OBJECT, &GOOD_LOCK, 1, GOOD_LOCK_OWNER) == 0);
}

static void dss_refresh_bad_owner(void **state)
{
    struct dss_handle *handle = (struct dss_handle *)*state;
    char *BAD_LOCK_OWNER = "not_an_owner";
    int rc;

    assert(dss_lock(handle, DSS_OBJECT, &GOOD_LOCK, 1, GOOD_LOCK_OWNER) == 0);

    rc = dss_lock_refresh(handle, FORMATED_GOOD_LOCK_ID, BAD_LOCK_OWNER);
    assert_int_equal(rc, -EACCES);

    assert(dss_unlock(handle, DSS_OBJECT, &GOOD_LOCK, 1, GOOD_LOCK_OWNER) == 0);
}

static void dss_unlock_not_exists(void **state)
{
    struct dss_handle *handle = (struct dss_handle *)*state;
    struct object_info BAD_LOCK = { .oid = "not_exists" };
    int rc;

    rc = dss_unlock(handle, DSS_OBJECT, &BAD_LOCK, 1, NULL);
    assert_int_equal(rc, -ENOLCK);

    rc = dss_unlock(handle, DSS_OBJECT, &BAD_LOCK, 1, GOOD_LOCK_OWNER);
    assert_int_equal(rc, -ENOLCK);
}

static void dss_unlock_bad_owner(void **state)
{
    struct dss_handle *handle = (struct dss_handle *)*state;
    char *BAD_LOCK_OWNER = "not_an_owner";
    int rc;

    assert(dss_lock(handle, DSS_OBJECT, &GOOD_LOCK, 1, GOOD_LOCK_OWNER) == 0);

    rc = dss_unlock(handle, DSS_OBJECT, &GOOD_LOCK, 1, BAD_LOCK_OWNER);
    assert_int_equal(rc, -EACCES);

    assert(dss_unlock(handle, DSS_OBJECT, &GOOD_LOCK, 1, GOOD_LOCK_OWNER) == 0);
}

static void dss_status_ok(void **state)
{
    struct dss_handle *handle = (struct dss_handle *)*state;
    struct timeval timestamp = { .tv_sec = 0, .tv_usec = 0 };
    char *lock_owner;
    int rc;

    assert(dss_lock(handle, DSS_OBJECT, &GOOD_LOCK, 1, GOOD_LOCK_OWNER) == 0);

    rc = dss_lock_status(handle, FORMATED_GOOD_LOCK_ID, NULL, NULL);
    assert_int_equal(rc, -rc);

    rc = dss_lock_status(handle, FORMATED_GOOD_LOCK_ID, &lock_owner, NULL);
    assert_int_equal(rc, -rc);
    assert_string_equal(lock_owner, GOOD_LOCK_OWNER);

    rc = dss_lock_status(handle, FORMATED_GOOD_LOCK_ID, NULL, &timestamp);
    assert_int_equal(rc, -rc);
    assert_int_not_equal(timestamp.tv_sec, 0);
    assert_int_not_equal(timestamp.tv_usec, 0);

    timestamp.tv_sec = 0;
    timestamp.tv_usec = 0;

    rc = dss_lock_status(handle, FORMATED_GOOD_LOCK_ID, &lock_owner,
                         &timestamp);
    assert_int_equal(rc, -rc);
    assert_int_not_equal(timestamp.tv_sec, 0);
    assert_int_not_equal(timestamp.tv_usec, 0);
    assert_string_equal(lock_owner, GOOD_LOCK_OWNER);

    assert(dss_unlock(handle, DSS_OBJECT, &GOOD_LOCK, 1, GOOD_LOCK_OWNER) == 0);
}

int main(void)
{
    const struct CMUnitTest dss_lock_test_cases[] = {
        cmocka_unit_test(dss_lock_unlock_ok),
        cmocka_unit_test(dss_lock_exists),
        cmocka_unit_test(dss_refresh_ok),
        cmocka_unit_test(dss_refresh_not_exists),
        cmocka_unit_test(dss_refresh_bad_owner),
        cmocka_unit_test(dss_unlock_not_exists),
        cmocka_unit_test(dss_unlock_bad_owner),
        cmocka_unit_test(dss_status_ok),
    };

    return cmocka_run_group_tests(dss_lock_test_cases, setup, teardown);
}
