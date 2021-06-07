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

#include "test_setup.h"
#include "pho_dss.h"

#include <assert.h>
#include <errno.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>

#include <cmocka.h>

static const char *GOOD_LOCK_OWNER = "dummy_owner";

static const struct object_info GOOD_LOCKS[] = {
    { .oid = "object_0"},
    { .oid = "object_1"},
    { .oid = "object_2"}
};

static bool check_newer(struct timeval old_ts, struct timeval new_ts)
{
    if (old_ts.tv_sec == new_ts.tv_sec)
        return (old_ts.tv_usec < new_ts.tv_usec);

    return (old_ts.tv_sec < new_ts.tv_sec);
}

static void dss_lock_unlock_ok(void **state)
{
    struct dss_handle *handle = (struct dss_handle *)*state;
    int rc;

    rc = dss_lock(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1, GOOD_LOCK_OWNER);
    assert_return_code(rc, -rc);

    rc = dss_lock(handle, DSS_OBJECT, &GOOD_LOCKS[1], 1, GOOD_LOCK_OWNER);
    assert_return_code(rc, -rc);

    rc = dss_unlock(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1, GOOD_LOCK_OWNER);
    assert_return_code(rc, -rc);

    rc = dss_unlock(handle, DSS_OBJECT, &GOOD_LOCKS[1], 1, NULL);
    assert_return_code(rc, -rc);
}

static void dss_lock_exists(void **state)
{
    struct dss_handle *handle = (struct dss_handle *)*state;
    static const char *OTHER_LOCK_OWNER = "dummy_owner2";
    int rc;

    rc = dss_lock(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1, GOOD_LOCK_OWNER);
    assert_return_code(rc, -rc);

    rc = dss_lock(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1, GOOD_LOCK_OWNER);
    assert_int_equal(rc, -EEXIST);

    rc = dss_lock(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1, OTHER_LOCK_OWNER);
    assert_int_equal(rc, -EEXIST);

    assert(dss_unlock(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1, NULL) == 0);
}

static void dss_multiple_lock_unlock_ok(void **state)
{
    struct dss_handle *handle = (struct dss_handle *)*state;
    int rc;

    rc = dss_lock(handle, DSS_OBJECT, GOOD_LOCKS, 3, GOOD_LOCK_OWNER);
    assert_return_code(rc, -rc);

    rc = dss_unlock(handle, DSS_OBJECT, GOOD_LOCKS, 3, GOOD_LOCK_OWNER);
    assert_return_code(rc, -rc);
}

static void dss_multiple_lock_exists(void **state)
{
    struct dss_handle *handle = (struct dss_handle *)*state;
    int rc;

    rc = dss_lock(handle, DSS_OBJECT, &GOOD_LOCKS[1], 1, GOOD_LOCK_OWNER);
    assert_return_code(rc, -rc);

    rc = dss_lock(handle, DSS_OBJECT, GOOD_LOCKS, 3, GOOD_LOCK_OWNER);
    assert_int_equal(rc, -EEXIST);

    rc = dss_unlock(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1, GOOD_LOCK_OWNER);
    assert_int_equal(rc, -ENOLCK);

    rc = dss_unlock(handle, DSS_OBJECT, &GOOD_LOCKS[2], 1, GOOD_LOCK_OWNER);
    assert_int_equal(rc, -ENOLCK);

    rc = dss_unlock(handle, DSS_OBJECT, &GOOD_LOCKS[1], 1, GOOD_LOCK_OWNER);
    assert_return_code(rc, -rc);
}

static void dss_refresh_ok(void **state)
{
    struct dss_handle *handle = (struct dss_handle *)*state;
    struct timeval old_ts = { .tv_sec = 0, .tv_usec = 0 };
    struct timeval new_ts = { .tv_sec = 0, .tv_usec = 0 };
    int rc;

    assert(dss_lock(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1,
                    GOOD_LOCK_OWNER) == 0);

    rc = dss_lock_status(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1, NULL, &old_ts);
    assert_int_equal(rc, -rc);

    rc = dss_lock_refresh(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1,
                          GOOD_LOCK_OWNER);
    assert_int_equal(rc, -rc);

    rc = dss_lock_status(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1, NULL, &new_ts);
    assert_int_equal(rc, -rc);

    assert_true(check_newer(old_ts, new_ts));

    assert(dss_unlock(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1,
                      GOOD_LOCK_OWNER) == 0);
}

static void dss_refresh_not_exists(void **state)
{
    struct dss_handle *handle = (struct dss_handle *)*state;
    static struct object_info BAD_LOCK = { .oid = "not_exists" };
    int rc;

    assert(dss_lock(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1,
                    GOOD_LOCK_OWNER) == 0);

    rc = dss_lock_refresh(handle, DSS_OBJECT, &BAD_LOCK, 1, GOOD_LOCK_OWNER);
    assert_int_equal(rc, -ENOLCK);

    assert(dss_unlock(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1,
                      GOOD_LOCK_OWNER) == 0);
}

static void dss_refresh_bad_owner(void **state)
{
    struct dss_handle *handle = (struct dss_handle *)*state;
    char *BAD_LOCK_OWNER = "not_an_owner";
    int rc;

    assert(dss_lock(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1,
                    GOOD_LOCK_OWNER) == 0);

    rc = dss_lock_refresh(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1,
                          BAD_LOCK_OWNER);
    assert_int_equal(rc, -EACCES);

    assert(dss_unlock(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1,
                      GOOD_LOCK_OWNER) == 0);
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

    assert(dss_lock(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1,
                    GOOD_LOCK_OWNER) == 0);

    rc = dss_unlock(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1, BAD_LOCK_OWNER);
    assert_int_equal(rc, -EACCES);

    assert(dss_unlock(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1,
                      GOOD_LOCK_OWNER) == 0);
}

static void dss_multiple_unlock_not_exists(void **state)
{
    struct dss_handle *handle = (struct dss_handle *)*state;
    int rc;

    rc = dss_lock(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1, GOOD_LOCK_OWNER);
    assert_return_code(rc, -rc);

    rc = dss_lock(handle, DSS_OBJECT, &GOOD_LOCKS[2], 1, GOOD_LOCK_OWNER);
    assert_return_code(rc, -rc);

    rc = dss_unlock(handle, DSS_OBJECT, GOOD_LOCKS, 3, GOOD_LOCK_OWNER);
    assert_int_equal(rc, -ENOLCK);

    rc = dss_unlock(handle, DSS_OBJECT, &GOOD_LOCKS[2], 1, GOOD_LOCK_OWNER);
    assert_int_equal(rc, -ENOLCK);

    rc = dss_unlock(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1, GOOD_LOCK_OWNER);
    assert_int_equal(rc, -ENOLCK);
}

static void dss_status_ok(void **state)
{
    struct dss_handle *handle = (struct dss_handle *)*state;
    struct timeval timestamp = { .tv_sec = 0, .tv_usec = 0 };
    char *lock_owner = NULL;
    int rc;

    assert(dss_lock(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1,
                    GOOD_LOCK_OWNER) == 0);

    rc = dss_lock_status(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1, NULL, NULL);
    assert_int_equal(rc, -rc);

    rc = dss_lock_status(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1, &lock_owner,
                         NULL);
    assert_int_equal(rc, -rc);
    assert_string_equal(lock_owner, GOOD_LOCK_OWNER);
    free(lock_owner);

    rc = dss_lock_status(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1, NULL,
                         &timestamp);
    assert_int_equal(rc, -rc);
    assert_int_not_equal(timestamp.tv_sec, 0);
    assert_int_not_equal(timestamp.tv_usec, 0);

    timestamp.tv_sec = 0;
    timestamp.tv_usec = 0;

    rc = dss_lock_status(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1, &lock_owner,
                         &timestamp);
    assert_int_equal(rc, -rc);
    assert_int_not_equal(timestamp.tv_sec, 0);
    assert_int_not_equal(timestamp.tv_usec, 0);
    assert_string_equal(lock_owner, GOOD_LOCK_OWNER);
    free(lock_owner);

    assert(dss_unlock(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1,
                      GOOD_LOCK_OWNER) == 0);
}

static void dss_multiple_status_ok(void **state)
{
    struct dss_handle *handle = (struct dss_handle *)*state;
    struct timeval timestamp[] = {
        { .tv_sec = 0, .tv_usec = 0 },
        { .tv_sec = 0, .tv_usec = 0 },
        { .tv_sec = 0, .tv_usec = 0 }
    };
    char *lock_owner[3] = { NULL, NULL, NULL };
    int rc;
    int i;

    assert(dss_lock(handle, DSS_OBJECT, GOOD_LOCKS, 3, GOOD_LOCK_OWNER) == 0);

    rc = dss_lock_status(handle, DSS_OBJECT, GOOD_LOCKS, 3, NULL, NULL);
    assert_return_code(rc, -rc);

    rc = dss_lock_status(handle, DSS_OBJECT, GOOD_LOCKS, 3, lock_owner, NULL);
    assert_return_code(rc, -rc);
    for (i = 0; i < 3; ++i) {
        assert_string_equal(lock_owner[i], GOOD_LOCK_OWNER);
        free(lock_owner[i]);
    }

    rc = dss_lock_status(handle, DSS_OBJECT, GOOD_LOCKS, 3, NULL, timestamp);
    assert_return_code(rc, -rc);
    for (i = 0; i < 3; ++i) {
        assert_int_not_equal(timestamp[i].tv_sec, 0);
        timestamp[i].tv_sec = 0;
    }

    rc = dss_lock_status(handle, DSS_OBJECT, GOOD_LOCKS, 3, lock_owner,
                         timestamp);
    assert_return_code(rc, -rc);
    for (i = 0; i < 3; ++i) {
        assert_int_not_equal(timestamp[i].tv_sec, 0);
        assert_string_equal(lock_owner[i], GOOD_LOCK_OWNER);
        free(lock_owner[i]);
    }

    assert(dss_unlock(handle, DSS_OBJECT, GOOD_LOCKS, 3,
                      GOOD_LOCK_OWNER) == 0);
}

static void dss_multiple_status_not_exists(void **state)
{
    struct dss_handle *handle = (struct dss_handle *)*state;
    static const struct object_info BAD_LOCKS[] = {
        { .oid = "object_0"},
        { .oid = "object_3"},
        { .oid = "object_2"}
    };
    char *lock_owner[3] = { NULL, NULL, NULL };
    int rc;

    assert(dss_lock(handle, DSS_OBJECT, GOOD_LOCKS, 3, GOOD_LOCK_OWNER) == 0);

    rc = dss_lock_status(handle, DSS_OBJECT, BAD_LOCKS, 3, lock_owner, NULL);
    assert_int_equal(rc, -ENOLCK);

    assert_string_equal(lock_owner[0], GOOD_LOCK_OWNER);
    assert_string_equal(lock_owner[2], GOOD_LOCK_OWNER);

    assert_null(lock_owner[1]);

    free(lock_owner[0]);
    free(lock_owner[2]);

    assert(dss_unlock(handle, DSS_OBJECT, GOOD_LOCKS, 3,
                      GOOD_LOCK_OWNER) == 0);
}

static void dss_multiple_refresh_ok(void **state)
{
    struct dss_handle *handle = (struct dss_handle *)*state;
    struct timeval old_ts[] = {
        { .tv_sec = 0, .tv_usec = 0 },
        { .tv_sec = 0, .tv_usec = 0 },
        { .tv_sec = 0, .tv_usec = 0 }
    };
    struct timeval new_ts[] = {
        { .tv_sec = 0, .tv_usec = 0 },
        { .tv_sec = 0, .tv_usec = 0 },
        { .tv_sec = 0, .tv_usec = 0 }
    };
    int rc;

    assert(dss_lock(handle, DSS_OBJECT, GOOD_LOCKS, 3, GOOD_LOCK_OWNER) == 0);

    rc = dss_lock_status(handle, DSS_OBJECT, GOOD_LOCKS, 3, NULL, old_ts);
    assert_return_code(rc, -rc);

    rc = dss_lock_refresh(handle, DSS_OBJECT, GOOD_LOCKS, 3, GOOD_LOCK_OWNER);
    assert_return_code(rc, -rc);

    rc = dss_lock_status(handle, DSS_OBJECT, GOOD_LOCKS, 3, NULL, new_ts);
    assert_return_code(rc, -rc);

    assert_true(check_newer(old_ts[0], new_ts[0]));
    assert_true(check_newer(old_ts[1], new_ts[1]));
    assert_true(check_newer(old_ts[2], new_ts[2]));

    assert(dss_unlock(handle, DSS_OBJECT, GOOD_LOCKS, 3,
                      GOOD_LOCK_OWNER) == 0);
}

static void dss_multiple_refresh_not_exists(void **state)
{
    struct dss_handle *handle = (struct dss_handle *)*state;
    static const struct object_info BAD_LOCKS[] = {
        { .oid = "object_0"},
        { .oid = "object_3"},
        { .oid = "object_2"}
    };
    struct timeval old_ts[] = {
        { .tv_sec = 0, .tv_usec = 0 },
        { .tv_sec = 0, .tv_usec = 0 },
        { .tv_sec = 0, .tv_usec = 0 }
    };
    struct timeval new_ts[] = {
        { .tv_sec = 0, .tv_usec = 0 },
        { .tv_sec = 0, .tv_usec = 0 },
        { .tv_sec = 0, .tv_usec = 0 }
    };
    int rc;

    assert(dss_lock(handle, DSS_OBJECT, GOOD_LOCKS, 3, GOOD_LOCK_OWNER) == 0);

    rc = dss_lock_status(handle, DSS_OBJECT, GOOD_LOCKS, 3, NULL, old_ts);
    assert_return_code(rc, -rc);

    rc = dss_lock_refresh(handle, DSS_OBJECT, BAD_LOCKS, 3, GOOD_LOCK_OWNER);
    assert_int_equal(rc, -ENOLCK);

    rc = dss_lock_status(handle, DSS_OBJECT, GOOD_LOCKS, 3, NULL, new_ts);
    assert_return_code(rc, -rc);

    assert_true(check_newer(old_ts[0], new_ts[0]));
    assert_true(check_newer(old_ts[2], new_ts[2]));

    assert_false(check_newer(old_ts[1], new_ts[1]));

    assert(dss_unlock(handle, DSS_OBJECT, GOOD_LOCKS, 3, GOOD_LOCK_OWNER) == 0);
}

/********************************/
/* dss_hostname_from_lock_owner */
/********************************/

/* dhflo_ok */
#define HOSTNAME_MODEL "hostname"
static const char *HOST_LOCKOWNER = HOSTNAME_MODEL ":owner_queue";

static void dhflo_ok(void **state)
{
    char *hostname;
    int rc;

    (void)state;

    rc = dss_hostname_from_lock_owner(HOST_LOCKOWNER, &hostname);
    assert_return_code(rc, -rc);
    assert_string_equal(hostname, HOSTNAME_MODEL);
    free(hostname);
}

/* dhflo_lock_without_host */
static const char *NO_HOST_LOCKOWNER = "owner";

static void dhflo_lock_without_host(void **state)
{
    char *hostname = "preexisting string";
    int rc;

    (void) state;

    rc = dss_hostname_from_lock_owner(NO_HOST_LOCKOWNER, &hostname);
    assert_int_equal(rc, -EBADF);
    assert_null(hostname);
}

int main(void)
{
    const struct CMUnitTest dss_lock_test_cases[] = {
        cmocka_unit_test(dss_lock_unlock_ok),
        cmocka_unit_test(dss_multiple_lock_unlock_ok),
        cmocka_unit_test(dss_lock_exists),
        cmocka_unit_test(dss_multiple_lock_exists),
        cmocka_unit_test(dss_refresh_ok),
        cmocka_unit_test(dss_refresh_not_exists),
        cmocka_unit_test(dss_refresh_bad_owner),
        cmocka_unit_test(dss_unlock_not_exists),
        cmocka_unit_test(dss_unlock_bad_owner),
        cmocka_unit_test(dss_multiple_unlock_not_exists),
        cmocka_unit_test(dss_status_ok),
        cmocka_unit_test(dss_multiple_status_ok),
        cmocka_unit_test(dss_multiple_status_not_exists),
        cmocka_unit_test(dss_multiple_refresh_ok),
        cmocka_unit_test(dss_multiple_refresh_not_exists),
        cmocka_unit_test(dhflo_ok),
        cmocka_unit_test(dhflo_lock_without_host),
    };

    return cmocka_run_group_tests(dss_lock_test_cases, global_setup_dss,
                                  global_teardown_dss);
}
