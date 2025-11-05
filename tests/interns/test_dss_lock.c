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
 * \brief  Tests for DSS generic lock feature
 */

#include "test_setup.h"
#include "dss_lock.h"
#include "pho_dss.h"
#include "pho_type_utils.h"

#include <assert.h>
#include <errno.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>

#include <cmocka.h>

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

    rc = dss_lock(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1);
    assert_return_code(rc, -rc);

    rc = dss_lock(handle, DSS_OBJECT, &GOOD_LOCKS[1], 1);
    assert_return_code(rc, -rc);

    rc = dss_unlock(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1, false);
    assert_return_code(rc, -rc);

    rc = dss_unlock(handle, DSS_OBJECT, &GOOD_LOCKS[1], 1, true);
    assert_return_code(rc, -rc);
}

static void dss_lock_exists(void **state)
{
    struct dss_handle *handle = (struct dss_handle *)*state;
    static const char *OTHER_LOCK_OWNER = "dummy_owner2";
    int rc;

    rc = dss_lock(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1);
    assert_return_code(rc, -rc);

    rc = dss_lock(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1);
    assert_int_equal(rc, -EEXIST);

    rc = _dss_lock(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1, OTHER_LOCK_OWNER,
                   1337, false, NULL);
    assert_int_equal(rc, -EEXIST);

    assert(dss_unlock(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1, true) == 0);
}

static void dss_multiple_lock_unlock_ok(void **state)
{
    struct dss_handle *handle = (struct dss_handle *)*state;
    int rc;

    rc = dss_lock(handle, DSS_OBJECT, GOOD_LOCKS, 3);
    assert_return_code(rc, -rc);

    rc = dss_unlock(handle, DSS_OBJECT, GOOD_LOCKS, 3, false);
    assert_return_code(rc, -rc);
}

static void dss_multiple_lock_exists(void **state)
{
    struct dss_handle *handle = (struct dss_handle *)*state;
    int rc;

    rc = dss_lock(handle, DSS_OBJECT, &GOOD_LOCKS[1], 1);
    assert_return_code(rc, -rc);

    rc = dss_lock(handle, DSS_OBJECT, GOOD_LOCKS, 3);
    assert_int_equal(rc, -EEXIST);

    rc = dss_unlock(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1, false);
    assert_int_equal(rc, -ENOLCK);

    rc = dss_unlock(handle, DSS_OBJECT, &GOOD_LOCKS[2], 1, false);
    assert_int_equal(rc, -ENOLCK);

    rc = dss_unlock(handle, DSS_OBJECT, &GOOD_LOCKS[1], 1, false);
    assert_return_code(rc, -rc);
}

static void dss_refresh_ok(void **state)
{
    struct dss_handle *handle = (struct dss_handle *)*state;
    struct pho_lock old_lock;
    struct pho_lock new_lock;
    int rc;

    assert(dss_lock(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1) == 0);

    rc = dss_lock_status(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1, &old_lock);
    assert_int_equal(rc, -rc);

    rc = dss_lock_refresh(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1, false);
    assert_int_equal(rc, -rc);

    rc = dss_lock_status(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1, &new_lock);
    assert_int_equal(rc, -rc);

    assert_true(check_newer(old_lock.timestamp, new_lock.timestamp));

    assert(dss_unlock(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1, true) == 0);
    pho_lock_clean(&old_lock);
    pho_lock_clean(&new_lock);
}

static void dss_refresh_not_exists(void **state)
{
    struct dss_handle *handle = (struct dss_handle *)*state;
    static struct object_info BAD_LOCK = { .oid = "not_exists" };
    int rc;

    assert(dss_lock(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1) == 0);

    rc = dss_lock_refresh(handle, DSS_OBJECT, &BAD_LOCK, 1, false);
    assert_int_equal(rc, -ENOLCK);

    assert(dss_unlock(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1, true) == 0);
}

static void dss_refresh_bad_owner(void **state)
{
    struct dss_handle *handle = (struct dss_handle *)*state;
    char *BAD_LOCK_OWNER = "not_an_owner";
    int rc;

    assert(dss_lock(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1) == 0);

    rc = _dss_lock_refresh(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1,
                           BAD_LOCK_OWNER, 1337, false);
    assert_int_equal(rc, -EACCES);

    assert(dss_unlock(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1, true) == 0);
}

static void dss_refresh_early_other_pid(void **state)
{
    struct dss_handle *handle = (struct dss_handle *)*state;
    const char *hostname;
    int rc;

    hostname = get_hostname();
    assert(hostname != NULL);

    assert(_dss_lock(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1,
                     hostname, 0, true, NULL) == 0);
    assert(_dss_lock(handle, DSS_OBJECT, &GOOD_LOCKS[1], 1,
                     hostname, 0, false, NULL) == 0);

    assert(dss_lock_refresh(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1, false) == 0);
    rc = dss_lock_refresh(handle, DSS_OBJECT, &GOOD_LOCKS[1], 1, false);
    assert_int_equal(rc, -EACCES);

    assert(dss_unlock(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1, false) == 0);
    assert(dss_unlock(handle, DSS_OBJECT, &GOOD_LOCKS[1], 1, true) == 0);
}

static void dss_unlock_not_exists(void **state)
{
    struct dss_handle *handle = (struct dss_handle *)*state;
    struct object_info BAD_LOCK = { .oid = "not_exists" };
    int rc;

    rc = dss_unlock(handle, DSS_OBJECT, &BAD_LOCK, 1, true);
    assert_int_equal(rc, -ENOLCK);

    rc = dss_unlock(handle, DSS_OBJECT, &BAD_LOCK, 1, false);
    assert_int_equal(rc, -ENOLCK);
}

static void dss_unlock_bad_owner(void **state)
{
    struct dss_handle *handle = (struct dss_handle *)*state;
    char *BAD_LOCK_OWNER = "not_an_owner";
    int rc;

    assert(dss_lock(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1) == 0);

    rc = _dss_unlock(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1, BAD_LOCK_OWNER,
                     1337);
    assert_int_equal(rc, -EACCES);

    assert(dss_unlock(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1, true) == 0);
}

static void dss_unlock_early_other_pid(void **state)
{
    struct dss_handle *handle = (struct dss_handle *)*state;
    const char *hostname;
    int rc;

    hostname = get_hostname();
    assert(hostname != NULL);

    assert(_dss_lock(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1,
                     hostname, 0, true, NULL) == 0);
    assert(_dss_lock(handle, DSS_OBJECT, &GOOD_LOCKS[1], 1,
                     hostname, 0, false, NULL) == 0);

    assert(dss_unlock(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1, false) == 0);
    rc = dss_unlock(handle, DSS_OBJECT, &GOOD_LOCKS[1], 1, false);
    assert_int_equal(rc, -EACCES);

    assert(dss_unlock(handle, DSS_OBJECT, &GOOD_LOCKS[1], 1, true) == 0);
}


static void dss_multiple_unlock_not_exists(void **state)
{
    struct dss_handle *handle = (struct dss_handle *)*state;
    int rc;

    rc = dss_lock(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1);
    assert_return_code(rc, -rc);

    rc = dss_lock(handle, DSS_OBJECT, &GOOD_LOCKS[2], 1);
    assert_return_code(rc, -rc);

    rc = dss_unlock(handle, DSS_OBJECT, GOOD_LOCKS, 3, false);
    assert_int_equal(rc, -ENOLCK);

    rc = dss_unlock(handle, DSS_OBJECT, &GOOD_LOCKS[2], 1, false);
    assert_int_equal(rc, -ENOLCK);

    rc = dss_unlock(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1, false);
    assert_int_equal(rc, -ENOLCK);
}

static void dss_status_ok(void **state)
{
    struct dss_handle *handle = (struct dss_handle *)*state;
    const char *lock_hostname = get_hostname();
    const int lock_owner = getpid();
    struct pho_lock lock;
    int rc;

    assert(dss_lock(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1) == 0);

    rc = dss_lock_status(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1, NULL);
    assert_int_equal(rc, -rc);

    rc = dss_lock_status(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1, &lock);
    assert_int_equal(rc, -rc);
    assert_string_equal(lock.hostname, lock_hostname);
    assert_int_equal(lock.owner, lock_owner);
    free(lock.hostname);

    assert_int_not_equal(lock.timestamp.tv_sec, 0);

    lock.timestamp.tv_sec = 0;
    lock.timestamp.tv_usec = 0;

    rc = dss_lock_status(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1, &lock);
    assert_int_equal(rc, -rc);
    assert_int_not_equal(lock.timestamp.tv_sec, 0);
    assert_int_not_equal(lock.timestamp.tv_usec, 0);
    assert_string_equal(lock.hostname, lock_hostname);
    assert_int_equal(lock.owner, lock_owner);

    assert(dss_unlock(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1, true) == 0);
    pho_lock_clean(&lock);
}

static void dss_multiple_status_ok(void **state)
{
    struct dss_handle *handle = (struct dss_handle *)*state;
    const char *lock_hostname = get_hostname();
    const int lock_owner = getpid();
    struct pho_lock lock[3];
    int rc;
    int i;

    assert(dss_lock(handle, DSS_OBJECT, GOOD_LOCKS, 3) == 0);

    rc = dss_lock_status(handle, DSS_OBJECT, GOOD_LOCKS, 3, NULL);
    assert_return_code(rc, -rc);

    rc = dss_lock_status(handle, DSS_OBJECT, GOOD_LOCKS, 3,
                         (struct pho_lock *) &lock);
    assert_return_code(rc, -rc);
    for (i = 0; i < 3; ++i) {
        assert_string_equal(lock[i].hostname, lock_hostname);
        assert_int_equal(lock[i].owner, lock_owner);
        assert_int_not_equal(lock[i].timestamp.tv_sec, 0);
        pho_lock_clean(lock + i);
    }

    assert(dss_unlock(handle, DSS_OBJECT, GOOD_LOCKS, 3, true) == 0);
}

static void dss_multiple_status_not_exists(void **state)
{
    struct dss_handle *handle = (struct dss_handle *)*state;
    static const struct object_info BAD_LOCKS[] = {
        { .oid = "object_0"},
        { .oid = "object_3"},
        { .oid = "object_2"}
    };
    const char *lock_hostname = get_hostname();
    const int lock_owner = getpid();
    struct pho_lock lock[3];
    int rc;

    assert(dss_lock(handle, DSS_OBJECT, GOOD_LOCKS, 3) == 0);

    rc = dss_lock_status(handle, DSS_OBJECT, BAD_LOCKS, 3,
                         (struct pho_lock *) &lock);
    assert_int_equal(rc, -ENOLCK);

    assert_string_equal(lock[0].hostname, lock_hostname);
    assert_int_equal(lock[0].owner, lock_owner);
    assert_string_equal(lock[2].hostname, lock_hostname);
    assert_int_equal(lock[2].owner, lock_owner);

    assert_null(lock[1].owner);
    assert_int_equal(lock[1].owner, 0);

    pho_lock_clean(lock);
    pho_lock_clean(lock + 2);

    assert(dss_unlock(handle, DSS_OBJECT, GOOD_LOCKS, 3, true) == 0);
}

static void dss_multiple_refresh_ok(void **state)
{
    struct dss_handle *handle = (struct dss_handle *)*state;
    struct pho_lock old_lock[3];
    struct pho_lock new_lock[3];
    int rc;
    int i;

    assert(dss_lock(handle, DSS_OBJECT, GOOD_LOCKS, 3) == 0);

    rc = dss_lock_status(handle, DSS_OBJECT, GOOD_LOCKS, 3,
                         (struct pho_lock *) &old_lock);
    assert_return_code(rc, -rc);

    rc = dss_lock_refresh(handle, DSS_OBJECT, GOOD_LOCKS, 3, false);
    assert_return_code(rc, -rc);

    rc = dss_lock_status(handle, DSS_OBJECT, GOOD_LOCKS, 3,
                         (struct pho_lock *) &new_lock);
    assert_return_code(rc, -rc);

    assert_true(check_newer(old_lock[0].timestamp, new_lock[0].timestamp));
    assert_true(check_newer(old_lock[1].timestamp, new_lock[1].timestamp));
    assert_true(check_newer(old_lock[2].timestamp, new_lock[2].timestamp));

    assert(dss_unlock(handle, DSS_OBJECT, GOOD_LOCKS, 3, true) == 0);

    for (i = 0; i < 3; ++i) {
        pho_lock_clean(old_lock + i);
        pho_lock_clean(new_lock + i);
    }
}

static void dss_multiple_refresh_not_exists(void **state)
{
    struct dss_handle *handle = (struct dss_handle *)*state;
    static const struct object_info BAD_LOCKS[] = {
        { .oid = "object_0"},
        { .oid = "object_3"},
        { .oid = "object_2"}
    };
    struct pho_lock old_lock[3];
    struct pho_lock new_lock[3];
    int rc;
    int i;

    assert(dss_lock(handle, DSS_OBJECT, GOOD_LOCKS, 3) == 0);

    rc = dss_lock_status(handle, DSS_OBJECT, GOOD_LOCKS, 3,
                         (struct pho_lock *) &old_lock);
    assert_return_code(rc, -rc);

    rc = dss_lock_refresh(handle, DSS_OBJECT, BAD_LOCKS, 3, false);
    assert_int_equal(rc, -ENOLCK);

    rc = dss_lock_status(handle, DSS_OBJECT, GOOD_LOCKS, 3,
                         (struct pho_lock *) &new_lock);
    assert_return_code(rc, -rc);

    assert_true(check_newer(old_lock[0].timestamp, new_lock[0].timestamp));
    assert_true(check_newer(old_lock[2].timestamp, new_lock[2].timestamp));

    assert_false(check_newer(old_lock[1].timestamp, new_lock[1].timestamp));

    assert(dss_unlock(handle, DSS_OBJECT, GOOD_LOCKS, 3, true) == 0);
    for (i = 0; i < 3; ++i) {
        pho_lock_clean(old_lock + i);
        pho_lock_clean(new_lock + i);
    }
}

static void dss_lock_hostname_unlock_ok(void **state)
{
    struct dss_handle *handle = (struct dss_handle *)*state;
    const char *lock_hostname = "A_TRUE_HOSTNAME";
    const int lock_owner = getpid();
    struct pho_lock lock;
    int rc;

    rc = dss_lock_hostname(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1,
                           lock_hostname);
    assert_return_code(rc, -rc);

    rc = dss_lock_status(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1, &lock);
    assert_int_equal(rc, -rc);
    assert_string_equal(lock.hostname, lock_hostname);
    assert_int_equal(lock.owner, lock_owner);
    assert_int_not_equal(lock.timestamp.tv_sec, 0);

    assert(dss_unlock(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1, true) == 0);
    pho_lock_clean(&lock);
}

static void dss_lock_last_locate(void **state)
{
    struct dss_handle *handle = (struct dss_handle *)*state;
    const char *lock_hostname = "A_TRUE_HOSTNAME";
    struct pho_lock lock;
    int rc;

    rc = dss_lock_hostname(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1,
                           lock_hostname);
    assert_return_code(rc, -rc);

    rc = dss_lock_status(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1, &lock);
    assert_return_code(rc, -rc);
    assert_int_not_equal(lock.last_locate.tv_sec, 0);

    assert(dss_unlock(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1, true) == 0);
    pho_lock_clean(&lock);

    assert(dss_lock(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1) == 0);

    rc = dss_lock_status(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1, &lock);
    assert_return_code(rc, -rc);
    assert_int_equal(lock.last_locate.tv_sec, 0);
    assert_int_equal(lock.last_locate.tv_usec, 0);
    assert(dss_unlock(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1, true) == 0);
    pho_lock_clean(&lock);
}

static void dss_lock_update_last_locate(void **state)
{
    struct dss_handle *handle = (struct dss_handle *)*state;
    const char *lock_hostname = "A_TRUE_HOSTNAME";
    struct pho_lock lock;
    struct timeval tv;
    int rc;

    rc = dss_lock_hostname(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1,
                           lock_hostname);
    assert_return_code(rc, -rc);

    assert(dss_lock_status(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1, &lock) == 0);
    tv = lock.last_locate;
    pho_lock_clean(&lock);

    rc = dss_lock_refresh(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1, true);
    assert_return_code(rc, -rc);

    rc = dss_lock_status(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1, &lock);
    assert_return_code(rc, -rc);

    assert_true(tv.tv_sec < lock.last_locate.tv_sec ||
                (tv.tv_sec == lock.last_locate.tv_sec &&
                 tv.tv_usec < lock.last_locate.tv_usec));

    rc = dss_unlock(handle, DSS_OBJECT, &GOOD_LOCKS[0], 1, true);
    assert_return_code(rc, -rc);

    pho_lock_clean(&lock);
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
        cmocka_unit_test(dss_refresh_early_other_pid),
        cmocka_unit_test(dss_unlock_not_exists),
        cmocka_unit_test(dss_unlock_bad_owner),
        cmocka_unit_test(dss_unlock_early_other_pid),
        cmocka_unit_test(dss_multiple_unlock_not_exists),
        cmocka_unit_test(dss_status_ok),
        cmocka_unit_test(dss_multiple_status_ok),
        cmocka_unit_test(dss_multiple_status_not_exists),
        cmocka_unit_test(dss_multiple_refresh_ok),
        cmocka_unit_test(dss_multiple_refresh_not_exists),
        cmocka_unit_test(dss_lock_hostname_unlock_ok),
        cmocka_unit_test(dss_lock_last_locate),
        cmocka_unit_test(dss_lock_update_last_locate),
    };

    pho_context_init();
    atexit(pho_context_fini);

    return cmocka_run_group_tests(dss_lock_test_cases,
                                  global_setup_dss_with_dbinit,
                                  global_teardown_dss_with_dbdrop);
}
