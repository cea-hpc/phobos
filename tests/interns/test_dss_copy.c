/*
 *  All rights reserved (c) 2014-2024 CEA/DAM.
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
 * \brief  Tests for dss_copy functions
 */

#include "test_setup.h"
#include "pho_dss.h"
#include "pho_types.h"

#include <stdlib.h>
#include <unistd.h>

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <cmocka.h>

static struct copy_info COPY = {
    .object_uuid = "123456789aaaabbbbccccdddd",
    .version = 1,
    .copy_name = "source",
    .copy_status = PHO_COPY_STATUS_COMPLETE,
};

static int dc_setup(void **state)
{
    struct dss_handle *handle = (struct dss_handle *)*state;
    int rc;

    /* insert the copy */
    rc = dss_copy_insert(handle, &COPY, 1);
    if (rc)
        return -1;

    return 0;
}

static void dc_get_ok(void **state)
{
    struct dss_handle *handle = (struct dss_handle *)*state;
    struct copy_info *copy_res;
    int copy_cnt;
    int rc;

    /* retrieve the copy */
    rc = dss_copy_get(handle, NULL, &copy_res, &copy_cnt, NULL);
    assert_return_code(rc, -rc);
    assert_int_equal(copy_cnt, 1);
    assert_int_equal(copy_res->version, 1);
    assert_memory_equal(copy_res->object_uuid, COPY.object_uuid,
                        strlen(COPY.object_uuid));
    assert_memory_equal(copy_res->copy_name, COPY.copy_name,
                        strlen(COPY.copy_name));
    assert_int_equal(copy_res->copy_status, PHO_COPY_STATUS_COMPLETE);
    assert_int_not_equal(copy_res->access_time.tv_sec, 0);
    assert_int_not_equal(copy_res->access_time.tv_usec, 0);
    assert_int_not_equal(copy_res->creation_time.tv_sec, 0);
    assert_int_not_equal(copy_res->creation_time.tv_usec, 0);
    assert_int_equal(copy_res->access_time.tv_sec,
                     copy_res->creation_time.tv_sec);
    assert_int_equal(copy_res->access_time.tv_usec,
                     copy_res->creation_time.tv_usec);

    dss_res_free(copy_res, copy_cnt);
}

static void dc_delete_ok(void **state)
{
    struct dss_handle *handle = (struct dss_handle *)*state;
    struct copy_info *copy_res;
    int copy_cnt;
    int rc;

    /* delete the copy */
    rc = dss_copy_delete(handle, &COPY, 1);
    assert_return_code(rc, -rc);

    /* try to get the copy */
    rc = dss_copy_get(handle, NULL, &copy_res, &copy_cnt, NULL);
    assert_return_code(rc, -rc);
    assert_int_equal(copy_cnt, 0);
    dss_res_free(copy_res, copy_cnt);
}

static void dc_update_ok(void **state)
{
    struct dss_handle *handle = (struct dss_handle *)*state;
    struct copy_info *copy_res;
    int copy_cnt;
    int rc;

    /* retrieve the copy */
    rc = dss_copy_get(handle, NULL, &copy_res, &copy_cnt, NULL);
    assert_return_code(rc, -rc);
    assert_int_equal(copy_cnt, 1);

    /* modify and update the copy information */
    copy_res->copy_status = PHO_COPY_STATUS_INCOMPLETE;
    copy_res->access_time.tv_sec = 50;
    copy_res->access_time.tv_usec = 10;

    rc = dss_copy_update(handle, copy_res, copy_res, 1,
                         DSS_COPY_UPDATE_ACCESS_TIME |
                         DSS_COPY_UPDATE_COPY_STATUS);
    assert_return_code(rc, -rc);
    dss_res_free(copy_res, copy_cnt);

    /* retrieve and verify the information */
    rc = dss_copy_get(handle, NULL, &copy_res, &copy_cnt, NULL);
    assert_return_code(rc, -rc);
    assert_int_equal(copy_cnt, 1);
    assert_int_equal(copy_res->copy_status, PHO_COPY_STATUS_INCOMPLETE);
    assert_int_equal(copy_res->access_time.tv_sec, 50);
    assert_int_equal(copy_res->access_time.tv_usec, 10);

    dss_res_free(copy_res, copy_cnt);
}

int main(void)
{
    const struct CMUnitTest dss_copy_cases[] = {
        cmocka_unit_test_setup_teardown(dc_get_ok, dc_setup, NULL),
        cmocka_unit_test(dc_update_ok),
        cmocka_unit_test(dc_delete_ok),
    };

    pho_context_init();
    atexit(pho_context_fini);

    return cmocka_run_group_tests(dss_copy_cases,
                                  global_setup_dss_with_dbinit,
                                  global_teardown_dss_with_dbdrop);
}
