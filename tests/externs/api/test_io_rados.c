/*
 *  All rights reserved (c) 2014-2022 CEA/DAM.
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
 * \brief  Tests for RADOS I/O adapter API call tests
 *         (Executed only when RADOS is enabled)
 */

#define _GNU_SOURCE

#include "pho_io.h"
#include "pho_ldm.h"

#include <setjmp.h>
#include <cmocka.h>
#include <errno.h>
#include <fcntl.h>
#include <rados/librados.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

struct pho_rados_io_ctx {
    rados_ioctx_t pool_io_ctx;
    struct lib_handle lib_hdl;
};

static int ior_setup(void **state)
{
    struct pho_ext_loc *iod_loc;
    struct pho_io_descr *iod;
    struct extent *extent;

    iod = calloc(1, sizeof(*iod));
    if (iod == NULL)
        return -1;

    iod_loc = malloc(sizeof(*iod_loc));
    if (iod_loc == NULL)
        return -1;

    extent = malloc(sizeof(*extent));
    if (extent == NULL)
        return -1;

    extent->layout_idx = 1;
    extent->size = 2;
    extent->media.family = PHO_RSC_RADOS_POOL;
    strcpy(extent->media.name, "pho_io");
    extent->address.buff = "pho_io.obj";
    extent->address.size = 10;

    iod_loc->extent = extent;
    iod_loc->root_path = "pho_io";
    iod_loc->addr_type = PHO_ADDR_PATH;
    iod->iod_loc = iod_loc;

    *state = iod;

    return 0;
}

static int ior_teardown(void **state)
{
    struct pho_io_descr *iod = *state;

    free(iod->iod_loc->extent);
    free(iod->iod_loc);
    free(iod);
    return 0;
}

static void ior_io_adapter_open_close(struct io_adapter_module *ioa,
                                      bool is_put, void **state, int rc_goal)
{
    struct pho_io_descr *iod = (struct pho_io_descr *) *state;
    int rc;

    rc = get_io_adapter(PHO_FS_RADOS, &ioa);
    assert_int_equal(rc, -rc);

    /* Opening I/O adapter with pool "pho_io", extent key "obj" and extent
     * description "pho_io"
     */
    rc = ioa_open(ioa, "obj", "pho_io", iod, is_put);
    assert_int_equal(rc, rc_goal);

    rc = ioa_close(ioa, iod);
    assert_int_equal(rc, -rc);

    free(iod->iod_ctx);
}

static void ior_test_io_adapter_open_close(void **state)
{
    struct io_adapter_module ioa;
    int rc;

    ior_io_adapter_open_close(&ioa, false, state, 0);
}

/* To check if there is no concurrency issue when using an io adapter with a
 * library adapter already opened
 */
static void ior_test_io_adapter_open_close_with_lib_adapter_opened(void **state)
{
    struct io_adapter_module ioa;
    struct lib_handle lib_hdl;
    int rc;

    rc = get_lib_adapter(PHO_LIB_RADOS, &lib_hdl.ld_module);
    assert_int_equal(rc, -rc);

    rc = ldm_lib_open(&lib_hdl, "");
    assert_int_equal(rc, -rc);

    ior_io_adapter_open_close(&ioa, false, state, 0);

    rc = ldm_lib_close(&lib_hdl);
    assert_int_equal(rc, -rc);
}

static void ior_test_set_new_xattr(void **state)
{
    struct pho_io_descr *iod = (struct pho_io_descr *) *state;
    struct io_adapter_module ioa;
    int rc;

    pho_attrs_free(&iod->iod_attrs);

    rc = pho_attr_set(&iod->iod_attrs, "pho_io_new_xattr", "pho_io");
    assert_int_equal(rc, -rc);

    iod->iod_flags = PHO_IO_MD_ONLY;

    ior_io_adapter_open_close(&ioa, true, state, 0);

    rc = pho_attr_set(&iod->iod_attrs, "pho_io_new_xattr", "invalid");
    assert_int_equal(rc, -rc);
    assert_string_equal("invalid",
                        pho_attr_get(&iod->iod_attrs, "pho_io_new_xattr"));

    ior_io_adapter_open_close(&ioa, false, state, 0);

    assert_string_equal("pho_io",
                        pho_attr_get(&iod->iod_attrs, "pho_io_new_xattr"));
}

static void ior_test_replace_xattr(void **state)
{
    struct pho_io_descr *iod = (struct pho_io_descr *) *state;
    struct io_adapter_module ioa;
    int rc;

    pho_attrs_free(&iod->iod_attrs);

    iod->iod_flags = PHO_IO_MD_ONLY;

    rc = pho_attr_set(&iod->iod_attrs, "pho_io_replace_xattr", "pho_io_first");
    assert_int_equal(rc, -rc);

    ior_io_adapter_open_close(&ioa, true, state, 0);

    iod->iod_flags = PHO_IO_REPLACE;

    rc = pho_attr_set(&iod->iod_attrs, "pho_io_replace_xattr", "pho_io_second");
    assert_int_equal(rc, -rc);

    ior_io_adapter_open_close(&ioa, true, state, 0);

    rc = pho_attr_set(&iod->iod_attrs, "pho_io_replace_xattr", "invalid");
    assert_int_equal(rc, -rc);

    ior_io_adapter_open_close(&ioa, false, state, 0);

    assert_string_equal("pho_io_second",
                        pho_attr_get(&iod->iod_attrs, "pho_io_replace_xattr"));
}

static void ior_test_set_new_xattr_with_existing_xattr(void **state)
{
    struct pho_io_descr *iod = (struct pho_io_descr *) *state;
    struct io_adapter_module ioa;
    int rc;

    pho_attrs_free(&iod->iod_attrs);

    iod->iod_flags = PHO_IO_MD_ONLY;
    rc = pho_attr_set(&iod->iod_attrs, "pho_io_exist_xattr", "pho_io");
    assert_int_equal(rc, -rc);

    ior_io_adapter_open_close(&ioa, true, state, 0);

    rc = pho_attr_set(&iod->iod_attrs, "pho_io_exist_xattr", "pho_io");
    assert_int_equal(rc, -rc);

    ior_io_adapter_open_close(&ioa, true, state, -EEXIST);
}

static void ior_test_remove_xattr(void **state)
{
    struct pho_io_descr *iod = (struct pho_io_descr *) *state;
    struct io_adapter_module ioa;
    int rc;

    pho_attrs_free(&iod->iod_attrs);

    iod->iod_flags = PHO_IO_REPLACE;

    rc = pho_attr_set(&iod->iod_attrs, "pho_io_remove_xattr", "pho_io");
    assert_int_equal(rc, -rc);

    ior_io_adapter_open_close(&ioa, true, state, 0);

    pho_attr_set(&iod->iod_attrs, "pho_io_remove_xattr", NULL);

    ior_io_adapter_open_close(&ioa, true, state, 0);

    ior_io_adapter_open_close(&ioa, false, state, 0);

    assert_null(pho_attr_get(&iod->iod_attrs, "pho_io_remove_xattr"));
}

int main(void)
{
    const struct CMUnitTest rados_io_tests_open_close[] = {
        cmocka_unit_test(ior_test_io_adapter_open_close),
        cmocka_unit_test(
                        ior_test_io_adapter_open_close_with_lib_adapter_opened),
        cmocka_unit_test(ior_test_set_new_xattr),
        cmocka_unit_test(ior_test_replace_xattr),
        cmocka_unit_test(ior_test_set_new_xattr_with_existing_xattr),
        cmocka_unit_test(ior_test_remove_xattr),
    };

    return cmocka_run_group_tests(rados_io_tests_open_close, ior_setup,
                                  ior_teardown);
}
