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
 * \brief  Tests for RADOS file system adapter API call tests
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
#include <rados/librados.h>
#include <string.h>

#define RADOS_LABEL_PATH ".phobos_rados_pool_label"
#define POOLNAME "pho_fs"
#define POOLNAME_SIZE 6
#define RADOS_LABEL "RADOS"
#define LABEL_SIZE 5

struct fsr_data {
    struct fs_adapter_module *fsa;
    struct lib_handle lib_hdl;
    rados_ioctx_t pool_io_ctx;
};

static int fsr_teardown(void **state)
{
    struct fsr_data *test_data = (struct fsr_data *) *state;
    int rc = 0;

    if (test_data->pool_io_ctx != NULL)
        rados_ioctx_destroy(test_data->pool_io_ctx);

    rc = ldm_lib_close(&test_data->lib_hdl);
    assert_int_equal(rc, -rc);

    free(test_data);

    return 0;
}

static int fsr_setup(void **state)
{
    struct fsr_data *test_data;
    rados_t cluster_hdl;
    int rc;

    test_data = malloc(sizeof(*test_data));
    if (test_data == NULL)
        return -1;

    test_data->pool_io_ctx = NULL;
    test_data->lib_hdl.lh_lib = NULL;
    *state = test_data;

    rc = get_lib_adapter(PHO_LIB_RADOS, &test_data->lib_hdl.ld_module);
    assert_int_equal(rc, -rc);

    rc = ldm_lib_open(&test_data->lib_hdl, POOLNAME, NULL);
    assert_int_equal(rc, -rc);

    cluster_hdl = test_data->lib_hdl.lh_lib;

    rc = rados_ioctx_create(cluster_hdl, POOLNAME, &test_data->pool_io_ctx);
    assert_int_equal(rc, -rc);

    rc = get_fs_adapter(PHO_FS_RADOS, &test_data->fsa);
    assert_int_equal(rc, -rc);

    return 0;
}

static void fsr_test_format(void **state)
{
    struct fsr_data *test_data = (struct fsr_data *) *state;
    char buf[PHO_LABEL_MAX_LEN + 1];
    struct ldm_fs_space fs_spc;
    int rc;

    memset(buf, 0, sizeof(buf));
    rc = rados_read(test_data->pool_io_ctx, RADOS_LABEL_PATH, buf,
                    sizeof(buf), 0);
    assert_int_equal(rc, -ENOENT);

    rc = ldm_fs_format(test_data->fsa, POOLNAME, RADOS_LABEL, &fs_spc);
    assert_int_equal(rc, -rc);

    rc = rados_read(test_data->pool_io_ctx, RADOS_LABEL_PATH, buf,
                    sizeof(buf), 0);
    assert_int_equal(rc, LABEL_SIZE);
    assert_string_equal(buf, RADOS_LABEL);

    rc = rados_remove(test_data->pool_io_ctx, RADOS_LABEL_PATH);
    assert_int_equal(rc, -rc);
}

static void fsr_test_format_again(void **state)
{
    struct fsr_data *test_data = (struct fsr_data *) *state;
    struct ldm_fs_space fs_spc;
    int rc;

    rc = ldm_fs_format(test_data->fsa, POOLNAME, RADOS_LABEL, &fs_spc);
    assert_int_equal(rc, -rc);

    rc = ldm_fs_format(test_data->fsa, POOLNAME, RADOS_LABEL, &fs_spc);
    assert_int_equal(rc, -EEXIST);

    rc = rados_remove(test_data->pool_io_ctx, RADOS_LABEL_PATH);
    assert_int_equal(rc, -rc);
}

static void fsr_test_get_label(void **state)
{
    struct fsr_data *test_data = (struct fsr_data *) *state;
    char buf[PHO_LABEL_MAX_LEN + 1];
    struct ldm_fs_space fs_spc;
    int rc;

    rc = ldm_fs_format(test_data->fsa, POOLNAME, RADOS_LABEL, &fs_spc);
    assert_int_equal(rc, -rc);

    rc = ldm_fs_get_label(test_data->fsa, POOLNAME, buf, sizeof(buf));
    assert_int_equal(rc, -rc);
    assert_string_equal(buf, RADOS_LABEL);

    rc = rados_remove(test_data->pool_io_ctx, RADOS_LABEL_PATH);
    assert_int_equal(rc, -rc);
}

static void fsr_test_pool_not_present(void **state)
{
    struct fsr_data *test_data = (struct fsr_data *) *state;
    char buf[POOLNAME_SIZE + 1];
    struct ldm_fs_space fs_spc;
    int rc;

    rc = ldm_fs_mounted(test_data->fsa, "invalid", buf, strlen("invalid"));
    assert_int_equal(rc, -ENOENT);
}

static void fsr_test_pool_present_but_not_mounted(void **state)
{
    struct fsr_data *test_data = (struct fsr_data *) *state;
    char buf[POOLNAME_SIZE + 1];
    struct ldm_fs_space fs_spc;
    int rc;

    rc = ldm_fs_mounted(test_data->fsa, POOLNAME, buf, POOLNAME_SIZE + 1);
    assert_int_equal(rc, -ENOENT);
}

static void fsr_test_mount(void **state)
{
    struct fsr_data *test_data = (struct fsr_data *) *state;
    char buf[POOLNAME_SIZE + 1];
    struct ldm_fs_space fs_spc;
    struct pho_log log;
    int rc;

    rc = ldm_fs_format(test_data->fsa, POOLNAME, RADOS_LABEL, &fs_spc);
    assert_int_equal(rc, -rc);

    rc = ldm_fs_mount(test_data->fsa, POOLNAME, POOLNAME, RADOS_LABEL,
                      &log.message);
    assert_int_equal(rc, -rc);

    rc = ldm_fs_mounted(test_data->fsa, POOLNAME, buf, POOLNAME_SIZE + 1);
    assert_int_equal(rc, -rc);
    assert_string_equal(buf, POOLNAME);

    rc = rados_remove(test_data->pool_io_ctx, RADOS_LABEL_PATH);
    assert_int_equal(rc, -rc);
}

static void fsr_test_df(void **state)
{
    struct fsr_data *test_data = (struct fsr_data *) *state;
    struct rados_cluster_stat_t cluster_stats;
    struct rados_pool_stat_t pool_stats;
    struct ldm_fs_space fs_spc;
    rados_t cluster_hdl;
    int rc;

    cluster_hdl = test_data->lib_hdl.lh_lib;

    rc = rados_cluster_stat(cluster_hdl, &cluster_stats);
    assert_int_equal(rc, -rc);

    rc = rados_ioctx_pool_stat(test_data->pool_io_ctx, &pool_stats);
    assert_int_equal(rc, -rc);

    rc = ldm_fs_df(test_data->fsa, POOLNAME, &fs_spc);
    assert_int_equal(rc, -rc);

    assert(fs_spc.spc_used == pool_stats.num_bytes);
    assert(fs_spc.spc_avail == cluster_stats.kb_avail * 1000);
    assert(fs_spc.spc_flags == 0);
}

int main(void)
{
    const struct CMUnitTest fs_rados_test_format[] = {
        cmocka_unit_test(fsr_test_format),
        cmocka_unit_test(fsr_test_format_again),
    };

    const struct CMUnitTest fs_rados_test_mount[] = {
        cmocka_unit_test(fsr_test_get_label),
        cmocka_unit_test(fsr_test_pool_not_present),
        cmocka_unit_test(fsr_test_pool_present_but_not_mounted),
        cmocka_unit_test(fsr_test_mount),
        cmocka_unit_test(fsr_test_df),
    };
    pho_context_init();
    atexit(pho_context_fini);

    return cmocka_run_group_tests(fs_rados_test_format, fsr_setup,
                                  fsr_teardown) +
        cmocka_run_group_tests(fs_rados_test_mount, fsr_setup,
                               fsr_teardown);
}
