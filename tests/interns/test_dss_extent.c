/*
 *  All rights reserved (c) 2014-2023 CEA/DAM.
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
 * \brief  Tests for dss_extent_update function
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

static struct extent EXT = {
    .state = PHO_EXT_ST_PENDING,
    .media.family = PHO_RSC_DIR,
    .media.name = "/mnt/source",
    .media.library = "legacy",
    .address.buff = "blablabla",
    .with_xxh128 = false,
    .with_md5 = false,
    .creation_time.tv_sec = 0,
    .creation_time.tv_usec = 0,
};

static int de_simple_setup(void **state)
{
    struct dss_handle *handle = (struct dss_handle *)*state;
    int rc;

    /* insert the extent */
    rc = dss_extent_insert(handle, &EXT, 1, DSS_SET_INSERT);
    if (rc)
        return -1;

    return 0;
}

static void de_simple_ok(void **state)
{
    struct dss_handle *handle = (struct dss_handle *)*state;
    const char *check_media_name = "/mnt/source2";
    const char *check_address = "clablabla";
    struct extent *ext_res;
    int ext_cnt;
    int rc;

    /* retrieve the extent */
    rc = dss_extent_get(handle, NULL, &ext_res, &ext_cnt, NULL);
    assert_return_code(rc, -rc);
    assert_int_equal(ext_cnt, 1);
    assert_int_equal(ext_res->state, PHO_EXT_ST_PENDING);
    assert_int_equal(ext_res->media.family, PHO_RSC_DIR);
    assert_memory_equal(ext_res->media.name, EXT.media.name,
                        strlen(EXT.media.name));
    assert_memory_equal(ext_res->media.library, EXT.media.library,
                        strlen(EXT.media.library));
    assert_memory_equal(ext_res->address.buff, EXT.address.buff,
                        strlen(EXT.address.buff));

    /* modify and update extent information */
    ext_res->state = PHO_EXT_ST_SYNC;
    ext_res->media.family = PHO_RSC_TAPE;
    /* media.name is a buffer larger than the size of check_media_name */
    ext_res->media.name[strlen(ext_res->media.name)] = '2';
    ext_res->address.buff[0] = 'c';
    rc = dss_extent_update(handle, ext_res, ext_res, ext_cnt);
    assert_return_code(rc, -rc);
    dss_res_free(ext_res, ext_cnt);

    /* retrieve and verify the information */
    rc = dss_extent_get(handle, NULL, &ext_res, &ext_cnt, NULL);
    assert_return_code(rc, -rc);
    assert_int_equal(ext_cnt, 1);
    assert_int_equal(ext_res->state, PHO_EXT_ST_SYNC);
    assert_int_equal(ext_res->media.family, PHO_RSC_TAPE);
    assert_memory_equal(ext_res->media.name, check_media_name,
                        strlen(check_media_name) + 1);
    assert_memory_equal(ext_res->address.buff, check_address,
                        strlen(check_address) + 1);
    dss_res_free(ext_res, ext_cnt);
}

int main(void)
{
    const struct CMUnitTest dss_extent_cases[] = {
        cmocka_unit_test_setup_teardown(de_simple_ok, de_simple_setup, NULL),
    };

    pho_context_init();
    atexit(pho_context_fini);

    return cmocka_run_group_tests(dss_extent_cases,
                                  global_setup_dss_with_dbinit,
                                  global_teardown_dss_with_dbdrop);
}

/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
