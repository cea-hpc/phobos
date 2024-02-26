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
#include "pho_common.h"

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

    iod = xcalloc(1, sizeof(*iod));
    iod_loc = xmalloc(sizeof(*iod_loc));
    extent = xmalloc(sizeof(*extent));

    extent->layout_idx = 1;
    extent->size = 2;
    extent->media.family = PHO_RSC_RADOS_POOL;
    strcpy(extent->media.name, "pho_io");
    extent->address.buff = NULL;
    extent->address.size = 0;

    iod_loc->extent = extent;
    iod_loc->root_path = "pho_io";
    iod_loc->addr_type = PHO_ADDR_HASH1;
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

    iod->iod_loc->extent->address.buff = "pho_io.obj";
    iod->iod_loc->extent->address.size = strlen("pho_io.obj");

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
    iod->iod_loc->extent->address.buff = NULL;
    iod->iod_loc->extent->address.size = 0;
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

    pho_attrs_free(&iod->iod_attrs);

    pho_attr_set(&iod->iod_attrs, "pho_io_new_xattr", "pho_io");

    iod->iod_flags = PHO_IO_MD_ONLY;

    ior_io_adapter_open_close(&ioa, true, state, 0);

    pho_attr_set(&iod->iod_attrs, "pho_io_new_xattr", "invalid");
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

    pho_attrs_free(&iod->iod_attrs);

    iod->iod_flags = PHO_IO_MD_ONLY;

    pho_attr_set(&iod->iod_attrs, "pho_io_replace_xattr", "pho_io_first");

    ior_io_adapter_open_close(&ioa, true, state, 0);

    iod->iod_flags = PHO_IO_REPLACE;

    pho_attr_set(&iod->iod_attrs, "pho_io_replace_xattr", "pho_io_second");

    ior_io_adapter_open_close(&ioa, true, state, 0);

    pho_attr_set(&iod->iod_attrs, "pho_io_replace_xattr", "invalid");

    ior_io_adapter_open_close(&ioa, false, state, 0);

    assert_string_equal("pho_io_second",
                        pho_attr_get(&iod->iod_attrs, "pho_io_replace_xattr"));
}

static void ior_test_set_new_xattr_with_existing_xattr(void **state)
{
    struct pho_io_descr *iod = (struct pho_io_descr *) *state;
    struct io_adapter_module ioa;

    pho_attrs_free(&iod->iod_attrs);

    iod->iod_flags = PHO_IO_MD_ONLY;
    pho_attr_set(&iod->iod_attrs, "pho_io_exist_xattr", "pho_io");

    ior_io_adapter_open_close(&ioa, true, state, 0);

    pho_attr_set(&iod->iod_attrs, "pho_io_exist_xattr", "pho_io");

    ior_io_adapter_open_close(&ioa, true, state, -EEXIST);
}

static void ior_test_remove_xattr(void **state)
{
    struct pho_io_descr *iod = (struct pho_io_descr *) *state;
    struct io_adapter_module ioa;

    pho_attrs_free(&iod->iod_attrs);

    iod->iod_flags = PHO_IO_REPLACE;

    pho_attr_set(&iod->iod_attrs, "pho_io_remove_xattr", "pho_io");

    ior_io_adapter_open_close(&ioa, true, state, 0);

    pho_attr_set(&iod->iod_attrs, "pho_io_remove_xattr", NULL);

    ior_io_adapter_open_close(&ioa, true, state, 0);

    ior_io_adapter_open_close(&ioa, false, state, 0);

    assert_null(pho_attr_get(&iod->iod_attrs, "pho_io_remove_xattr"));
}

static void ior_test_write_new_object(void **state)
{
    struct pho_io_descr *iod = (struct pho_io_descr *) *state;
    struct pho_rados_io_ctx *rados_io_ctx;
    struct io_adapter_module *ioa;
    char buf[12];
    int rc;

    memset(buf, 0, sizeof(buf));

    rc = get_io_adapter(PHO_FS_RADOS, &ioa);
    assert_int_equal(rc, -rc);

    rc = ioa_open(ioa, "pho_new_obj", "pho_io", iod, true);
    assert_int_equal(rc, -rc);
    rados_io_ctx = iod->iod_ctx;

    rc = ioa_write(ioa, iod, "new_obj", strlen("new_obj"));
    assert_int_equal(rc, -rc);

    rc = rados_read(rados_io_ctx->pool_io_ctx,
                    iod->iod_loc->extent->address.buff,
                    buf, sizeof(buf), 0);
    assert_int_equal(rc, strlen("new_obj"));
    assert_string_equal("new_obj", buf);

    rc = ioa_close(ioa, iod);
    assert_int_equal(rc, -rc);

    free(iod->iod_loc->extent->address.buff);
    iod->iod_loc->extent->address.buff = NULL;
}

static void ior_test_replace_object(void **state)
{
    struct pho_io_descr *iod = (struct pho_io_descr *) *state;
    struct pho_rados_io_ctx *rados_io_ctx;
    struct io_adapter_module *ioa;
    char buf_in[30];
    char buf_out[30];
    int rc;

    iod->iod_flags = PHO_IO_REPLACE;
    memset(buf_in, 1, sizeof(buf_in));
    memcpy(buf_in, "very_long_obj_first", strlen("very_long_obj_first"));

    rc = get_io_adapter(PHO_FS_RADOS, &ioa);
    assert_int_equal(rc, -rc);

    rc = ioa_open(ioa, "pho_replace_obj", "pho_io", iod, true);
    assert_int_equal(rc, -rc);

    rados_io_ctx = iod->iod_ctx;

    /* Create object 'pho_io.pho_replace_obj' with buf_first_in's
     * content
     */
    rc = ioa_write(ioa, iod, buf_in, sizeof(buf_in));
    assert_int_equal(rc, -rc);

    rc = rados_read(rados_io_ctx->pool_io_ctx,
                    iod->iod_loc->extent->address.buff,
                    buf_out, sizeof(buf_out), 0);
    assert_int_equal(rc, sizeof(buf_in));
    assert_memory_equal(buf_in, buf_out, sizeof(buf_in));

    memset(buf_in, 0, sizeof(buf_in));
    memcpy(buf_in, "obj_second", strlen("obj_second"));

    /* Replace object's content with buf_second_in */
    rc = ioa_write(ioa, iod, buf_in, sizeof(buf_in));
    assert_int_equal(rc, -rc);

    rc = rados_read(rados_io_ctx->pool_io_ctx,
                    iod->iod_loc->extent->address.buff,
                    buf_out, sizeof(buf_out), 0);
    assert_int_equal(rc, sizeof(buf_in));
    assert_memory_equal(buf_in, buf_out, sizeof(buf_in));

    rc = ioa_close(ioa, iod);
    assert_int_equal(rc, -rc);

    free(iod->iod_loc->extent->address.buff);
    iod->iod_loc->extent->address.buff = NULL;
}

static void ior_test_write_existing_object(void **state)
{
    struct pho_io_descr *iod = (struct pho_io_descr *) *state;
    struct io_adapter_module *ioa;
    int rc;

    iod->iod_flags = 0;

    rc = get_io_adapter(PHO_FS_RADOS, &ioa);
    assert_int_equal(rc, -rc);

    /* Open succeeds because object 'pho_io.pho_existing_obj' did not exist
     * before call
     */
    rc = ioa_open(ioa, "pho_existing_obj", "pho_io", iod, true);
    assert_int_equal(rc, -rc);

    rc = ioa_write(ioa, iod, "existing_obj", strlen("existing_obj"));
    assert_int_equal(rc, -rc);

    /* Write succeeds even though the object exists because it does not check if
     * the object already exists
     */
    rc = ioa_write(ioa, iod, "existing_obj", strlen("existing_obj"));
    assert_int_equal(rc, -rc);

    rc = ioa_close(ioa, iod);
    assert_int_equal(rc, -rc);

    /* open fails because object already exists and replace flag is not set */
    rc = ioa_open(ioa, "pho_existing_obj", "pho_io", iod, true);
    assert_int_equal(rc, -EEXIST);

    rc = ioa_close(ioa, iod);
    assert_int_equal(rc, -rc);

    free(iod->iod_loc->extent->address.buff);
    iod->iod_loc->extent->address.buff = NULL;
}

static void ior_test_write_object_too_big(void **state)
{
    struct pho_io_descr *iod = (struct pho_io_descr *) *state;
    struct io_adapter_module *ioa;
    int rc;

    iod->iod_flags = 0;

    rc = get_io_adapter(PHO_FS_RADOS, &ioa);
    assert_int_equal(rc, -rc);

    rc = ioa_open(ioa, "pho_obj_too_big", "pho_io", iod, true);
    assert_int_equal(rc, -rc);

    rc = ioa_write(ioa, iod, "obj_too_big", UINT_MAX);
    assert_int_equal(rc, -EFBIG);

    rc = ioa_close(ioa, iod);
    assert_int_equal(rc, -rc);

    free(iod->iod_loc->extent->address.buff);
    iod->iod_loc->extent->address.buff = NULL;
}

void fill_buffer_with_random_data(struct pho_buff *buffer)
{
    size_t random_data_len = 0;
    ssize_t bytes_written;
    int urandom_fd;

    urandom_fd = open("/dev/urandom", O_RDONLY);
    assert(urandom_fd >= 0);

    buffer->buff = xmalloc(buffer->size);
    assert_non_null(buffer->buff);

    bytes_written = read(urandom_fd, buffer->buff, buffer->size);
    assert_int_equal(bytes_written, buffer->size);

    close(urandom_fd);
}

static void ior_test_write_object_with_chunks(void **state)
{
    struct pho_io_descr *iod = (struct pho_io_descr *) *state;
    struct pho_rados_io_ctx *rados_io_ctx;
    struct io_adapter_module *ioa;
    size_t chunk_size = 4096;
    struct pho_buff buf_out;
    struct pho_buff buf_in;
    size_t to_write;
    int rc;

    buf_in.size = 50000;
    buf_out.size = buf_in.size;
    iod->iod_flags = PHO_IO_REPLACE;
    iod->iod_size = 0;

    rc = get_io_adapter(PHO_FS_RADOS, &ioa);
    assert_int_equal(rc, -rc);

    fill_buffer_with_random_data(&buf_in);
    buf_out.buff = xcalloc(buf_out.size, 1);

    rc = ioa_open(ioa, "pho_obj_chunks", "pho_io", iod, true);
    assert_int_equal(rc, -rc);

    to_write = buf_in.size;

    while (to_write > 0) {
        rc = ioa_write(ioa, iod, buf_in.buff + iod->iod_size,
                       to_write > chunk_size ? chunk_size : to_write);
        assert_int_equal(rc, -rc);

        iod->iod_size += (to_write > chunk_size ? chunk_size : to_write);
        to_write = to_write > chunk_size ? to_write - chunk_size : 0;
    }

    assert_int_equal(iod->iod_size, buf_in.size);

    rados_io_ctx = iod->iod_ctx;

    rc = rados_read(rados_io_ctx->pool_io_ctx,
                    iod->iod_loc->extent->address.buff,
                    buf_out.buff, buf_out.size, 0);
    assert_int_equal(rc, buf_in.size);
    assert_memory_equal(buf_in.buff, buf_out.buff, buf_in.size);

    rc = ioa_close(ioa, iod);
    assert_int_equal(rc, -rc);

    free(buf_in.buff);
    free(buf_out.buff);
    iod->iod_size = 0;
    free(iod->iod_loc->extent->address.buff);
    iod->iod_loc->extent->address.buff = NULL;
}

static void ior_get_object(struct pho_io_descr *iod, char *object_name,
                          size_t input_size)
{
    struct io_adapter_module *ioa;
    char output[input_size];
    struct pho_buff input;
    int rc;

    iod->iod_size = 0;
    input.size = input_size;
    fill_buffer_with_random_data(&input);

    rc = get_io_adapter(PHO_FS_RADOS, &ioa);
    assert_int_equal(rc, -rc);

    rc = ioa_open(ioa, object_name, "pho_io", iod, true);
    assert_int_equal(rc, -rc);

    rc = ioa_write(ioa, iod, input.buff, input_size);
    assert_int_equal(rc, -rc);

    rc = ioa_close(ioa, iod);
    assert_int_equal(rc, -rc);

    iod->iod_fd = open(".", O_TMPFILE | O_RDWR);
    iod->iod_size = input_size;

    rc = ioa_get(ioa, object_name, "pho_io", iod);
    assert_int_equal(rc, -rc);

    read(iod->iod_fd, &output, input_size);

    assert_memory_equal(input.buff, output, input_size);

    close(iod->iod_fd);

    free(input.buff);
    free(iod->iod_loc->extent->address.buff);
    iod->iod_loc->extent->address.buff = NULL;
}

static void ior_test_get_small_object(void **state)
{
    struct pho_io_descr *iod = (struct pho_io_descr *) *state;
    int rc;

    iod->iod_flags = 0;

    ior_get_object(iod, "pho_get_small_obj", 30);
}

static void ior_test_get_big_object(void **state)
{
    struct pho_io_descr *iod = (struct pho_io_descr *) *state;
    struct pho_buff input;
    int rc;

    iod->iod_flags = 0;

    ior_get_object(iod, "pho_get_big_obj", 1200);
}

static void ior_test_get_invalid_object(void **state)
{
    struct pho_io_descr *iod = (struct pho_io_descr *) *state;
    struct io_adapter_module *ioa;
    int rc;

    iod->iod_flags = 0;

    iod->iod_loc->extent->address.buff = "pho_io.pho_invalid_obj";
    iod->iod_loc->extent->address.size = strlen("pho_io.pho_invalid_obj");

    rc = get_io_adapter(PHO_FS_RADOS, &ioa);

    rc = ioa_get(ioa, "pho_invalid_obj", "pho_io", iod);
    assert_int_equal(rc, -ENOENT);
}

static void ior_test_delete_object(void **state)
{
    struct pho_io_descr *iod = (struct pho_io_descr *) *state;
    struct io_adapter_module *ioa;
    int rc;

    iod->iod_flags = 0;

    iod->iod_loc->extent->address.buff = "pho_io.pho_delete_obj";
    iod->iod_loc->extent->address.size = strlen("pho_io.pho_delete_obj");

    rc = get_io_adapter(PHO_FS_RADOS, &ioa);

    rc = ioa_open(ioa, "pho_delete_obj", "pho_io", iod, true);
    assert_int_equal(rc, -rc);

    rc = ioa_write(ioa, iod, "delete_obj", strlen("delete_obj"));
    assert_int_equal(rc, -rc);

    rc = ioa_del(ioa, iod);
    assert_int_equal(rc, -rc);

    rc = ioa_close(ioa, iod);
    assert_int_equal(rc, -rc);

    rc = ioa_get(ioa, "pho_delete_obj", "pho_io", iod);
    assert_int_equal(rc, -ENOENT);
}

static void ior_test_delete_invalid_object(void **state)
{
    struct pho_io_descr *iod = (struct pho_io_descr *) *state;
    struct io_adapter_module *ioa;
    int rc;

    iod->iod_flags = 0;

    iod->iod_loc->extent->address.buff = "pho_io.pho_invalid_obj";
    iod->iod_loc->extent->address.size = strlen("pho_io.pho_invalid_obj");

    rc = get_io_adapter(PHO_FS_RADOS, &ioa);

    rc = ioa_open(ioa, "pho_invalid_obj", "pho_io", iod, true);
    assert_int_equal(rc, -rc);

    rc = ioa_del(ioa, iod);
    assert_int_equal(rc, -ENOENT);

    rc = ioa_close(ioa, iod);
    assert_int_equal(rc, -rc);
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

    const struct CMUnitTest rados_io_tests_write[] = {
        cmocka_unit_test(ior_test_write_new_object),
        cmocka_unit_test(ior_test_replace_object),
        cmocka_unit_test(ior_test_write_existing_object),
        cmocka_unit_test(ior_test_write_object_too_big),
        cmocka_unit_test(ior_test_write_object_with_chunks),
    };

    const struct CMUnitTest rados_io_tests_get[] = {
        cmocka_unit_test(ior_test_get_small_object),
        cmocka_unit_test(ior_test_get_big_object),
        cmocka_unit_test(ior_test_get_invalid_object),
    };

    const struct CMUnitTest rados_io_tests_delete[] = {
        cmocka_unit_test(ior_test_delete_object),
        cmocka_unit_test(ior_test_delete_invalid_object),
    };
    pho_context_init();
    atexit(pho_context_fini);

    return cmocka_run_group_tests(rados_io_tests_open_close, ior_setup,
                                  ior_teardown) +
        cmocka_run_group_tests(rados_io_tests_write, ior_setup,
                               ior_teardown) +
        cmocka_run_group_tests(rados_io_tests_get, ior_setup,
                               ior_teardown) +
        cmocka_run_group_tests(rados_io_tests_delete, ior_setup,
                               ior_teardown);
}
