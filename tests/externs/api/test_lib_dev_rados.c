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
 * \brief  Tests for RADOS library adapter API call tests
 *         (Executed only when RADOS is enabled)
 */

#define _GNU_SOURCE
#include "pho_ldm.h"
#include "phobos_admin.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include <cmocka.h>
#include <string.h>

static int ldr_test_dev_adapter_add_pool(void **state)
{
    struct admin_handle adm;
    struct pho_id dev_ids;
    int rc;

    rc = phobos_admin_init(&adm, false);
    assert_int_equal(rc, -rc);

    dev_ids.family = PHO_RSC_RADOS_POOL;
    strcpy(dev_ids.name, "pho_pool_valid");

    rc = phobos_admin_device_add(&adm, &dev_ids, 1, false);

    phobos_admin_fini(&adm);

    return rc;
}

static void ldr_test_dev_adapter_add_pool_with_conf(void **state)
{
    int rc = ldr_test_dev_adapter_add_pool(state);

    assert_int_equal(rc, -rc);
}

static void ldr_test_lib_adapter_with_conf(void **state)
{
    struct lib_item_addr med_addr;
    struct lib_drv_info drv_info;
    struct lib_handle lib_hdl;
    int rc;

    rc = get_lib_adapter(PHO_LIB_RADOS, &lib_hdl.ld_module);
    assert_int_equal(rc, -rc);

    rc = ldm_lib_open(&lib_hdl, "", NULL);
    assert_int_equal(rc, -rc);

    rc = ldm_lib_drive_lookup(&lib_hdl, "host:pho_pool_valid", &drv_info);
    assert_int_equal(rc, -rc);

    rc = ldm_lib_media_lookup(&lib_hdl, "pho_pool_valid", &med_addr, NULL);
    assert_int_equal(rc, -rc);

    rc = ldm_lib_drive_lookup(&lib_hdl, "host:pho_pool_invalid", &drv_info);
    assert_int_equal(rc, -ENODEV);

    rc = ldm_lib_media_lookup(&lib_hdl, "pho_pool_invalid", &med_addr, NULL);
    assert_int_equal(rc, -ENODEV);

    rc = ldm_lib_close(&lib_hdl);
    assert_int_equal(rc, -rc);
}

static int ldr_setup_without_conf(void **state)
{
    return rename("/etc/ceph/ceph.conf", "/etc/ceph/ceph.conf.old");
}

static int ldr_teardown_without_conf(void **state)
{
    return rename("/etc/ceph/ceph.conf.old", "/etc/ceph/ceph.conf");
}

static void ldr_test_dev_adapter_add_pool_without_conf(void **state)
{
    int rc = ldr_test_dev_adapter_add_pool(state);

    assert_int_equal(rc, -ENOENT);
}

static void ldr_test_lib_adapter_without_conf(void **state)
{
    struct lib_item_addr med_addr;
    struct lib_drv_info drv_info;
    struct lib_handle lib_hdl;
    int rc;

    rc = get_lib_adapter(PHO_LIB_RADOS, &lib_hdl.ld_module);
    assert_int_equal(rc, -rc);

    rc = ldm_lib_open(&lib_hdl, "", NULL);
    assert_int_equal(rc, -ENOENT);

    rc = ldm_lib_drive_lookup(&lib_hdl, "host:pho_pool_valid", &drv_info);
    assert_int_equal(rc, -EBADF);

    rc = ldm_lib_media_lookup(&lib_hdl, "pho_pool_valid", &med_addr, NULL);
    assert_int_equal(rc, -EBADF);

    rc = ldm_lib_close(&lib_hdl);
    assert_int_equal(rc, -EBADF);
}

int main(void)
{
    const struct CMUnitTest lib_dev_rados_test_with_conf[] = {
        cmocka_unit_test(ldr_test_dev_adapter_add_pool_with_conf),
        cmocka_unit_test(ldr_test_lib_adapter_with_conf),
    };

    const struct CMUnitTest lib_dev_rados_test_without_conf[] = {
        cmocka_unit_test(ldr_test_dev_adapter_add_pool_without_conf),
        cmocka_unit_test(ldr_test_lib_adapter_without_conf),
    };
    pho_context_init();
    atexit(pho_context_fini);

    return cmocka_run_group_tests(lib_dev_rados_test_with_conf, NULL, NULL) +
           cmocka_run_group_tests(lib_dev_rados_test_without_conf,
                                  ldr_setup_without_conf,
                                  ldr_teardown_without_conf);
}
