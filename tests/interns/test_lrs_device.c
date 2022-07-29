/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
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
 * \brief  Test LRS device
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>

#include "pho_common.h"
#include "pho_dss.h"
#include "pho_layout.h"

#include <cmocka.h>

#include "test_setup.h"

#include "lrs_device.h"
#include "lrs_sched.h"

static struct dss_handle *dss;
static struct lrs_sched scheduler;

/* running is defined in lrs.c but cannot be linked with this file due to the
 * definition of main in both
 */
bool running;

static int setup(void **data)
{
    int rc;

    rc = global_setup_dss((void **)&dss);
    if (rc)
        return rc;

    scheduler.sched_thread.dss = *dss;
    scheduler.family = PHO_RSC_DIR;
    rc = lock_handle_init(&scheduler.lock_handle, dss);

    return rc;
}

static int teardown(void **data)
{
    return global_teardown_dss((void **)&dss);
}

/* Convert \p value into string up to UINT_MAX = 4,294,967,295 */
static char *uint2str(unsigned int value)
{
    char str[11]; /* 4294967295\0 */

    assert_in_range(value, 0, UINT_MAX);

    snprintf(str, sizeof(str), "%u", value);

    return strdup(str);
}

static char *make_sync_value(unsigned int value)
{
    size_t len;
    char *val;
    char *res;

    val = uint2str(value);
    assert_non_null(val);
    len = strlen(val);

    /* dir= + len + '\0' */
    res = calloc(4 + len + 1, sizeof(char));
    assert_non_null(res);

    strcpy(res, "dir=");
    strcat(res, val);
    res[4+len] = '\0';

    free(val);

    return res;
}

static void set_sync_param(char *name, unsigned int value)
{
    char *val = make_sync_value(value);
    int rc;

    rc = setenv(name, val, 1);
    assert_return_code(rc, errno);

    free(val);
}

static void set_sync_params(unsigned int time,
                            unsigned int number,
                            unsigned int size)
{
    set_sync_param("PHOBOS_LRS_sync_time_ms", time);
    set_sync_param("PHOBOS_LRS_sync_nb_req", number);
    set_sync_param("PHOBOS_LRS_sync_wsize_kb", size);
}

static void test_dev_init(void **data)
{
    struct lrs_dev_hdl dev_handle;
    int rc;

    set_sync_params(1001, 3, 20);

    rc = lrs_dev_hdl_init(&dev_handle, PHO_RSC_DIR);
    assert_return_code(rc, -rc);

    assert_non_null(dev_handle.ldh_devices);
    assert_int_equal(dev_handle.ldh_devices->len, 0);

    assert_int_equal(dev_handle.sync_time_ms.tv_sec, 1);
    assert_int_equal(dev_handle.sync_time_ms.tv_nsec, 1000000);

    assert_int_equal(dev_handle.sync_nb_req, 3);
    assert_int_equal(dev_handle.sync_wsize_kb, 20 * 1024);

    lrs_dev_hdl_fini(&dev_handle);
}

static int remove_device(struct dss_handle *dss, char *device)
{
    struct dev_info dev = {
        .rsc = {
            .id = {
                .family = PHO_RSC_DIR,
            },
            .model = "",
            .adm_status = PHO_RSC_ADM_ST_UNLOCKED,
        },
        .path = device,
        .host = "hostname",
    };
    int rc;

    strcpy(dev.rsc.id.name, device);

    rc = dss_device_delete(dss, &dev, 1);

    return rc;
}

static int insert_device(struct dss_handle *dss, char *device)
{
    struct dev_info dev = {
        .rsc = {
            .id = {
                .family = PHO_RSC_DIR,
            },
            .model = "",
            .adm_status = PHO_RSC_ADM_ST_UNLOCKED,
        },
        .path = device,
    };
    int rc;

    strcpy(dev.rsc.id.name, device);
    dev.host = strdup(get_hostname());

    rc = dss_device_insert(dss, &dev, 1);

    free(dev.host);

    return rc;
}

static int test_setup_one_device(void **data)
{
    struct lrs_dev_hdl *handle;
    int rc;

    handle = malloc(sizeof(*handle));
    if (!handle)
        return errno;

    set_sync_params(1000, 3, 20);

    rc = lrs_dev_hdl_init(handle, PHO_RSC_DIR);
    if (rc)
        return rc;

    rc = insert_device(&scheduler.sched_thread.dss, "test");
    if (rc)
        return rc;

    *data = handle;

    return 0;
}

static void test_ldh_add_one_device(void **data)
{
    struct lrs_dev_hdl *handle = (struct lrs_dev_hdl *)*data;
    struct lrs_dev *dev;
    int rc;

    rc = lrs_dev_hdl_add(&scheduler, handle, "test");
    assert_return_code(rc, -rc);
    assert_int_equal(handle->ldh_devices->len, 1);

    dev = (struct lrs_dev *)g_ptr_array_index(handle->ldh_devices, 0);
    assert_non_null(dev);
    assert_string_equal(dev->ld_dss_dev_info->rsc.id.name, "test");

    rc = lrs_dev_hdl_del(handle, 0, 0);
    assert_return_code(rc, -rc);
    assert_int_equal(handle->ldh_devices->len, 0);
}

static int test_teardown_one_device(void **data)
{
    struct lrs_dev_hdl *handle = (struct lrs_dev_hdl *)*data;
    int rc;

    lrs_dev_hdl_fini(handle);
    free(handle);

    rc = remove_device(&scheduler.sched_thread.dss, "test");

    return rc;
}

static int test_setup_three_devices(void **data)
{
    struct lrs_dev_hdl *handle;
    int rc;

    handle = malloc(sizeof(*handle));
    if (!handle)
        return errno;

    set_sync_params(1000, 3, 20);

    rc = lrs_dev_hdl_init(handle, PHO_RSC_DIR);
    if (rc)
        return rc;

    rc = insert_device(&scheduler.sched_thread.dss, "test1");
    if (rc)
        return rc;

    rc = insert_device(&scheduler.sched_thread.dss, "test2");
    if (rc)
        return rc;

    rc = insert_device(&scheduler.sched_thread.dss, "test3");
    if (rc)
        return rc;

    *data = handle;

    return 0;
}

static void test_ldh_add_three_devices(void **data)
{
    struct lrs_dev_hdl *handle = (struct lrs_dev_hdl *)*data;
    static const char * const names[] = {
        "test1",
        "test2",
        "test3",
    };
    int rc;
    int i;

    rc = lrs_dev_hdl_load(&scheduler, handle);
    assert_return_code(rc, -rc);
    assert_int_equal(handle->ldh_devices->len, 3);

    for (i = 0; i < handle->ldh_devices->len; i++) {
        struct lrs_dev *dev;

        dev = (struct lrs_dev *)g_ptr_array_index(handle->ldh_devices, i);

        assert_non_null(dev);
        assert_string_equal(dev->ld_dss_dev_info->rsc.id.name, names[i]);
    }

    lrs_dev_hdl_clear(handle);
    assert_int_equal(handle->ldh_devices->len, 0);
}

static int test_teardown_three_devices(void **data)
{
    struct lrs_dev_hdl *handle = (struct lrs_dev_hdl *)*data;
    int rc;

    lrs_dev_hdl_fini(handle);
    free(handle);

    rc = remove_device(&scheduler.sched_thread.dss, "test1");
    if (rc)
        return rc;

    rc = remove_device(&scheduler.sched_thread.dss, "test2");
    if (rc)
        return rc;

    rc = remove_device(&scheduler.sched_thread.dss, "test3");
    if (rc)
        return rc;


    return 0;
}

int main(void)
{
    const struct CMUnitTest lrs_device_tests[] = {
        cmocka_unit_test(test_dev_init),
        cmocka_unit_test_setup_teardown(test_ldh_add_one_device,
                                        test_setup_one_device,
                                        test_teardown_one_device),
        cmocka_unit_test_setup_teardown(test_ldh_add_three_devices,
                                        test_setup_three_devices,
                                        test_teardown_three_devices),
    };

    return cmocka_run_group_tests(lrs_device_tests, setup, teardown);
}
