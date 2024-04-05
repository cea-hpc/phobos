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
 * \brief  Tests for Store object_md operations
 */

#include <assert.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>

#include "dss_lock.h"
#include "pho_dss_wrapper.h"
#include "store_utils.h"

#include <cmocka.h>

/** Mock functions */
int pho_attrs_to_json(const struct pho_attrs *md, GString *str, int flags)
{
    (void)md; (void)str; (void)flags;

    return (int)mock();
}

int dss_lock(struct dss_handle *handle, enum dss_type type,
              const void *item_list, int item_cnt)
{
    (void)handle; (void)type; (void)item_list; (void)item_cnt;

    return (int)mock();
}

int dss_unlock(struct dss_handle *handle, enum dss_type type,
                const void *item_list, int item_cnt, bool force_unlock)
{
    (void)handle; (void)type; (void)item_list; (void)item_cnt;
    (void)force_unlock;

    return (int)mock();
}

int dss_object_set(struct dss_handle *hdl, struct object_info *obj_lst,
                   int obj_cnt, enum dss_set_action action)
{
    (void)hdl; (void)obj_lst; (void)obj_cnt; (void)action;

    return (int)mock();
}

int dss_object_get(struct dss_handle *hdl, const struct dss_filter *filter,
                   struct object_info **obj_ls, int *obj_cnt)
{
    int rc = (int)mock();
    void *obj_container;
    bool *ctn_bool;
    int i;

    (void)hdl; (void)filter;

    if (rc != 0)
        return rc;

    *obj_cnt = (int)mock();

    if (*obj_cnt == 0) {
        *obj_ls = NULL;
        return rc;
    }

    obj_container = xmalloc(*obj_cnt * sizeof(**obj_ls) + sizeof(*ctn_bool));
    ctn_bool = (bool *)obj_container;
    *ctn_bool = true; // true for object, false for layout
    *obj_ls = (struct object_info *)(obj_container + sizeof(*ctn_bool));

    memset(*obj_ls, 0, *obj_cnt * sizeof(**obj_ls));

    for (i = 0; i < *obj_cnt; ++i) {
        (*obj_ls)[i].version = 7;
        (*obj_ls)[i].uuid = xstrdup("abcdefgh12345678");
    }

    return rc;
}

#define MOCK_DSS_OBJECT_GET(_rc, _cnt)                                         \
do {                                                                           \
    will_return(dss_object_get, _rc);                                          \
    if (_rc == 0)                                                              \
        will_return(dss_object_get, _cnt);                                     \
} while (0)

int dss_deprecated_object_get(struct dss_handle *hdl,
                              const struct dss_filter *filter,
                              struct object_info **obj_ls, int *obj_cnt)
{
    int rc = (int)mock();
    void *obj_container;
    bool *ctn_bool;

    (void)hdl; (void)filter;

    if (rc != 0)
        return rc;

    *obj_cnt = (int)mock();

    if (*obj_cnt == 0) {
        *obj_ls = NULL;
        return rc;
    }

    obj_container = xmalloc(*obj_cnt * sizeof(**obj_ls) + sizeof(*ctn_bool));
    ctn_bool = (bool *)obj_container;
    *ctn_bool = true; // true for object, false for layout
    *obj_ls = (struct object_info *)(obj_container + sizeof(*ctn_bool));
    memset(*obj_ls, 0, *obj_cnt * sizeof(**obj_ls));

    return rc;
}

#define MOCK_DSS_DEPRECATED_OBJECT_GET(_rc, _cnt)                              \
do {                                                                           \
    will_return(dss_deprecated_object_get, _rc);                               \
    if (_rc == 0)                                                              \
        will_return(dss_deprecated_object_get, _cnt);                          \
} while (0)

int dss_layout_get(struct dss_handle *hdl, const struct dss_filter *filter,
                   struct layout_info **layouts, int *layout_count)
{
    void *layout_container;
    int rc = (int)mock();
    bool *ctn_bool;

    (void)hdl; (void)filter;

    if (rc != 0)
        return rc;

    *layout_count = (int)mock();

    if (*layout_count == 0) {
        layouts = NULL;
        return rc;
    }

    /* allocate dss_result, which consists in a boolean and an array of
     * pointers and return the array field as layouts
     */
    layout_container = xmalloc(
        sizeof(*ctn_bool) + *layout_count * sizeof(**layouts));
    ctn_bool = (bool *)layout_container;
    *ctn_bool = false; // true for object, false for layout
    *layouts = (struct layout_info *)(layout_container + sizeof(*ctn_bool));
    memset(*layouts, 0, *layout_count * sizeof(**layouts));

    return rc;
}

#define MOCK_DSS_LAYOUT_GET(_rc, _cnt)                                         \
do {                                                                           \
    will_return(dss_layout_get, _rc);                                          \
    if (_rc == 0)                                                              \
        will_return(dss_layout_get, _cnt);                                     \
} while (0)

int dss_move_object_to_deprecated(struct dss_handle *handle,
                                  struct object_info *obj_list,
                                  int obj_cnt)
{
    (void)handle; (void)obj_list; (void)obj_cnt;

    return (int)mock();
}

int dss_move_deprecated_to_object(struct dss_handle *handle,
                                  struct object_info *obj_list,
                                  int obj_cnt)
{
    (void)handle; (void)obj_list; (void)obj_cnt;

    return (int)mock();
}

void dss_res_free(void *item_list, int item_cnt)
{
    bool *ctn_bool;

    if (item_list == NULL)
        return;

    ctn_bool = (bool *)(item_list - sizeof(*ctn_bool));
    if (ctn_bool) { // item_list is an object list
        struct object_info *obj_ls = (struct object_info *)item_list;
        int i;

        for (i = 0; i < item_cnt; ++i)
            free(obj_ls[i].uuid);

    }

    free(item_list - sizeof(*ctn_bool));
}

int dss_filter_build(struct dss_filter *filter, const char *fmt, ...)
{
    (void)filter, (void)fmt;

    return (int)mock();
}

void dss_filter_free(struct dss_filter *filter)
{
    (void)filter;

    return;
}

/** Tests for object_md_save */
static void oms_attrs_to_json_failure(void **state)
{
    struct pho_xfer_desc xfer;
    int rc;

    (void)state;

    will_return(pho_attrs_to_json, -ENOMEM);
    rc = object_md_save(NULL, &xfer);
    assert_int_equal(rc, -ENOMEM);
}

static const struct pho_xfer_desc PUT_XFER = {
    .xd_params.put.overwrite = false,
    .xd_objid = "dummy_object",
};

static void oms_dss_lock_failure(void **state)
{
    struct pho_xfer_desc xfer = PUT_XFER;
    int rc;

    (void)state;

    will_return(pho_attrs_to_json, 0);
    will_return(dss_lock, -EINVAL);
    rc = object_md_save(NULL, &xfer);
    assert_int_equal(rc, -EINVAL);
}

static void oms_dss_object_set_failure_without_overwrite(void **state)
{
    struct pho_xfer_desc xfer = PUT_XFER;
    int rc;

    (void)state;

    will_return(pho_attrs_to_json, 0);
    will_return(dss_lock, 0);
    will_return(dss_object_set, -EINVAL);
    will_return(dss_unlock, 0);
    rc = object_md_save(NULL, &xfer);
    assert_int_equal(rc, -EINVAL);
}

static const struct pho_xfer_desc OVERWRITE_XFER = {
    .xd_params.put.overwrite = true,
    .xd_objid = "dummy_object",
};

static void oms_dss_filter_build_failure_with_overwrite(void **state)
{
    struct pho_xfer_desc xfer = OVERWRITE_XFER;
    int rc;

    (void)state;

    will_return(pho_attrs_to_json, 0);
    will_return(dss_lock, 0);
    will_return(dss_filter_build, -ENOMEM);
    will_return(dss_unlock, 0);
    rc = object_md_save(NULL, &xfer);
    assert_int_equal(rc, -ENOMEM);
}

static void oms_dss_object_set_failure_with_fake_overwrite(void **state)
{
    struct pho_xfer_desc xfer = OVERWRITE_XFER;
    int rc;

    (void)state;

    will_return(pho_attrs_to_json, 0);
    will_return(dss_lock, 0);
    will_return(dss_filter_build, 0);
    MOCK_DSS_OBJECT_GET(0, 0);
    will_return(dss_object_set, -EINVAL);
    will_return(dss_unlock, 0);
    rc = object_md_save(NULL, &xfer);
    assert_int_equal(rc, -EINVAL);

    will_return(pho_attrs_to_json, 0);
    will_return(dss_lock, 0);
    will_return(dss_filter_build, 0);
    MOCK_DSS_OBJECT_GET(-ENOENT, 0);
    will_return(dss_object_set, -EINVAL);
    will_return(dss_unlock, 0);
    rc = object_md_save(NULL, &xfer);
    assert_int_equal(rc, -EINVAL);
}

static void oms_dss_object_move_failure_with_overwrite(void **state)
{
    struct pho_xfer_desc xfer = OVERWRITE_XFER;
    int rc;

    (void)state;

    will_return(pho_attrs_to_json, 0);
    will_return(dss_lock, 0);
    will_return(dss_filter_build, 0);
    MOCK_DSS_OBJECT_GET(0, 1);
    will_return(dss_move_object_to_deprecated, -ENOENT);
    will_return(dss_unlock, 0);
    rc = object_md_save(NULL, &xfer);
    assert_int_equal(rc, -ENOENT);
}

static void oms_dss_object_set_failure_with_overwrite(void **state)
{
    struct pho_xfer_desc xfer = OVERWRITE_XFER;
    int rc;

    (void)state;

    will_return(pho_attrs_to_json, 0);
    will_return(dss_lock, 0);
    will_return(dss_filter_build, 0);
    MOCK_DSS_OBJECT_GET(0, 1);
    will_return(dss_move_object_to_deprecated, 0);
    will_return(dss_object_set, -EINVAL);
    will_return(dss_unlock, 0);
    rc = object_md_save(NULL, &xfer);
    assert_int_equal(rc, -EINVAL);
}

static void oms_dss_filter_build_failure(void **state)
{
    struct pho_xfer_desc xfer = PUT_XFER;
    int rc;

    (void)state;

    will_return(pho_attrs_to_json, 0);
    will_return(dss_lock, 0);
    will_return(dss_object_set, 0);
    will_return(dss_filter_build, -ENOMEM);
    will_return(dss_unlock, 0);
    rc = object_md_save(NULL, &xfer);
    assert_int_equal(rc, -ENOMEM);
}

static void oms_dss_object_get_failure(void **state)
{
    struct pho_xfer_desc xfer = PUT_XFER;
    int rc;

    (void)state;

    will_return(pho_attrs_to_json, 0);
    will_return(dss_lock, 0);
    will_return(dss_object_set, 0);
    will_return(dss_filter_build, 0);
    MOCK_DSS_OBJECT_GET(-EINVAL, 0);
    will_return(dss_unlock, 0);
    rc = object_md_save(NULL, &xfer);
    assert_int_equal(rc, -EINVAL);
}

static void oms_dss_unlock_failure(void **state)
{
    struct pho_xfer_desc xfer = PUT_XFER;
    int rc;

    (void)state;

    will_return(pho_attrs_to_json, 0);
    will_return(dss_lock, 0);
    will_return(dss_object_set, 0);
    will_return(dss_filter_build, 0);
    MOCK_DSS_OBJECT_GET(0, 1);
    will_return(dss_unlock, -ENOLCK);
    rc = object_md_save(NULL, &xfer);
    assert_int_equal(rc, -ENOLCK);

    free(xfer.xd_objuuid);
}

static void oms_success_without_overwrite(void **state)
{
    struct pho_xfer_desc xfer = PUT_XFER;
    int rc;

    (void)state;

    will_return(pho_attrs_to_json, 0);
    will_return(dss_lock, 0);
    will_return(dss_object_set, 0);
    will_return(dss_filter_build, 0);
    MOCK_DSS_OBJECT_GET(0, 1);
    will_return(dss_unlock, 0);
    rc = object_md_save(NULL, &xfer);
    assert_int_equal(rc, 0);

    free(xfer.xd_objuuid);
}

static void oms_success_with_fake_overwrite(void **state)
{
    struct pho_xfer_desc xfer = OVERWRITE_XFER;
    int rc;

    (void)state;

    will_return(pho_attrs_to_json, 0);
    will_return(dss_lock, 0);
    will_return(dss_filter_build, 0);
    MOCK_DSS_OBJECT_GET(-ENOENT, 0);
    will_return(dss_object_set, 0);
    will_return(dss_filter_build, 0);
    MOCK_DSS_OBJECT_GET(0, 1);
    will_return(dss_unlock, 0);
    rc = object_md_save(NULL, &xfer);
    assert_int_equal(rc, 0);

    free(xfer.xd_objuuid);
}

static void oms_success_with_overwrite(void **state)
{
    struct pho_xfer_desc xfer = OVERWRITE_XFER;
    int rc;

    (void)state;

    will_return(pho_attrs_to_json, 0);
    will_return(dss_lock, 0);
    will_return(dss_filter_build, 0);
    MOCK_DSS_OBJECT_GET(0, 1);
    will_return(dss_move_object_to_deprecated, 0);
    will_return(dss_object_set, 0);
    will_return(dss_filter_build, 0);
    MOCK_DSS_OBJECT_GET(0, 1);
    will_return(dss_unlock, 0);
    rc = object_md_save(NULL, &xfer);
    assert_int_equal(rc, 0);

    free(xfer.xd_objuuid);
}

/** Tests for object_md_del */
static void omd_dss_filter_build_for_get_failure(void **state)
{
    struct pho_xfer_desc xfer = PUT_XFER;
    int rc;

    (void)state;

    will_return(dss_filter_build, -ENOMEM);
    rc = object_md_del(NULL, &xfer);
    assert_int_equal(rc, -ENOMEM);
}

static const struct pho_xfer_desc DEL_XFER = {
    .xd_objid = "dummy_object",
    .xd_objuuid = "abcdefgh12345678",
};

static void omd_dss_lock_failure(void **state)
{
    struct pho_xfer_desc xfer = DEL_XFER;
    int rc;

    (void)state;

    will_return(dss_filter_build, 0);
    will_return(dss_lock, -EINVAL);
    rc = object_md_del(NULL, &xfer);
    assert_int_equal(rc, -EINVAL);
}

static void omd_dss_object_get_failure(void **state)
{
    struct pho_xfer_desc xfer = DEL_XFER;
    int rc;

    (void)state;

    will_return(dss_filter_build, 0);
    will_return(dss_lock, 0);
    MOCK_DSS_OBJECT_GET(-ENOMEM, 0);
    will_return(dss_unlock, 0);
    rc = object_md_del(NULL, &xfer);
    assert_int_equal(rc, -ENOMEM);

    will_return(dss_filter_build, 0);
    will_return(dss_lock, 0);
    MOCK_DSS_OBJECT_GET(0, 2);
    will_return(dss_unlock, 0);
    rc = object_md_del(NULL, &xfer);
    assert_int_equal(rc, -EINVAL);
}

static void omd_dss_filter_build_for_deprec_failure(void **state)
{
    struct pho_xfer_desc xfer = DEL_XFER;
    int rc;

    (void)state;

    will_return(dss_filter_build, 0);
    will_return(dss_lock, 0);
    MOCK_DSS_OBJECT_GET(0, 1);
    will_return(dss_filter_build, -ENOMEM);
    will_return(dss_unlock, 0);
    rc = object_md_del(NULL, &xfer);
    assert_int_equal(rc, -ENOMEM);
}

static void omd_dss_deprecated_object_get_failure(void **state)
{
    struct pho_xfer_desc xfer = DEL_XFER;
    int rc;

    (void)state;

    will_return(dss_filter_build, 0);
    will_return(dss_lock, 0);
    MOCK_DSS_OBJECT_GET(0, 1);
    will_return(dss_filter_build, 0);
    MOCK_DSS_DEPRECATED_OBJECT_GET(-EINVAL, 0);
    will_return(dss_unlock, 0);
    rc = object_md_del(NULL, &xfer);
    assert_int_equal(rc, -EINVAL);
}

static void omd_dss_filter_build_for_layout_failure(void **state)
{
    struct pho_xfer_desc xfer = DEL_XFER;
    int rc;

    (void)state;

    will_return(dss_filter_build, 0);
    will_return(dss_lock, 0);
    MOCK_DSS_OBJECT_GET(0, 1);
    will_return(dss_filter_build, 0);
    MOCK_DSS_DEPRECATED_OBJECT_GET(0, 1);
    will_return(dss_filter_build, -ENOMEM);
    will_return(dss_unlock, 0);
    rc = object_md_del(NULL, &xfer);
    assert_int_equal(rc, -ENOMEM);
}

static void omd_dss_full_layout_get_failure(void **state)
{
    struct pho_xfer_desc xfer = DEL_XFER;
    int rc;

    (void)state;

    will_return(dss_filter_build, 0);
    will_return(dss_lock, 0);
    MOCK_DSS_OBJECT_GET(0, 1);
    will_return(dss_filter_build, 0);
    MOCK_DSS_DEPRECATED_OBJECT_GET(0, 1);
    will_return(dss_filter_build, 0);
    MOCK_DSS_LAYOUT_GET(-EINVAL, 0);
    will_return(dss_unlock, 0);
    rc = object_md_del(NULL, &xfer);
    assert_int_equal(rc, -EINVAL);

    will_return(dss_filter_build, 0);
    will_return(dss_lock, 0);
    MOCK_DSS_OBJECT_GET(0, 1);
    will_return(dss_filter_build, 0);
    MOCK_DSS_DEPRECATED_OBJECT_GET(0, 1);
    will_return(dss_filter_build, 0);
    MOCK_DSS_LAYOUT_GET(0, 1);
    will_return(dss_unlock, 0);
    rc = object_md_del(NULL, &xfer);
    assert_int_equal(rc, -EEXIST);
}

static void omd_dss_object_set_failure(void **state)
{
    struct pho_xfer_desc xfer = DEL_XFER;
    int rc;

    (void)state;

    will_return(dss_filter_build, 0);
    will_return(dss_lock, 0);
    MOCK_DSS_OBJECT_GET(0, 1);
    will_return(dss_filter_build, 0);
    MOCK_DSS_DEPRECATED_OBJECT_GET(0, 1);
    will_return(dss_filter_build, 0);
    MOCK_DSS_LAYOUT_GET(0, 0);
    will_return(dss_object_set, -EINVAL);
    will_return(dss_unlock, 0);
    rc = object_md_del(NULL, &xfer);
    assert_int_equal(rc, -EINVAL);
}

static void omd_dss_object_move_failure(void **state)
{
    struct pho_xfer_desc xfer = DEL_XFER;
    int rc;

    (void)state;

    will_return(dss_filter_build, 0);
    will_return(dss_lock, 0);
    MOCK_DSS_OBJECT_GET(0, 1);
    will_return(dss_filter_build, 0);
    MOCK_DSS_DEPRECATED_OBJECT_GET(0, 1);
    will_return(dss_filter_build, 0);
    MOCK_DSS_LAYOUT_GET(0, 0);
    will_return(dss_object_set, 0);
    will_return(dss_move_deprecated_to_object, -ENOENT);
    will_return(dss_unlock, 0);
    rc = object_md_del(NULL, &xfer);
    assert_int_equal(rc, -ENOENT);
}

static void omd_dss_unlock_failure(void **state)
{
    struct pho_xfer_desc xfer = DEL_XFER;
    int rc;

    (void)state;

    will_return(dss_filter_build, 0);
    will_return(dss_lock, 0);
    MOCK_DSS_OBJECT_GET(0, 1);
    will_return(dss_filter_build, 0);
    MOCK_DSS_DEPRECATED_OBJECT_GET(0, 0);
    will_return(dss_filter_build, 0);
    MOCK_DSS_LAYOUT_GET(0, 0);
    will_return(dss_object_set, 0);
    will_return(dss_unlock, -ENOLCK);
    rc = object_md_del(NULL, &xfer);
    assert_int_equal(rc, -ENOLCK);
}

static void omd_success(void **state)
{
    struct pho_xfer_desc xfer = DEL_XFER;
    int rc;

    (void)state;

    will_return(dss_filter_build, 0);
    will_return(dss_lock, 0);
    MOCK_DSS_OBJECT_GET(0, 1);
    will_return(dss_filter_build, 0);
    MOCK_DSS_DEPRECATED_OBJECT_GET(0, 1);
    will_return(dss_filter_build, 0);
    MOCK_DSS_LAYOUT_GET(0, 0);
    will_return(dss_object_set, 0);
    will_return(dss_move_deprecated_to_object, 0);
    will_return(dss_unlock, 0);
    rc = object_md_del(NULL, &xfer);
    assert_int_equal(rc, 0);

    will_return(dss_filter_build, 0);
    will_return(dss_lock, 0);
    MOCK_DSS_OBJECT_GET(0, 1);
    will_return(dss_filter_build, 0);
    MOCK_DSS_DEPRECATED_OBJECT_GET(0, 0);
    will_return(dss_filter_build, 0);
    MOCK_DSS_LAYOUT_GET(0, 0);
    will_return(dss_object_set, 0);
    will_return(dss_unlock, 0);
    rc = object_md_del(NULL, &xfer);
    assert_int_equal(rc, 0);
}

int main(void)
{
    const struct CMUnitTest object_md_test_cases[] = {
        cmocka_unit_test(oms_attrs_to_json_failure),
        cmocka_unit_test(oms_dss_lock_failure),
        cmocka_unit_test(oms_dss_object_set_failure_without_overwrite),
        cmocka_unit_test(oms_dss_filter_build_failure_with_overwrite),
        cmocka_unit_test(oms_dss_object_set_failure_with_fake_overwrite),
        cmocka_unit_test(oms_dss_object_move_failure_with_overwrite),
        cmocka_unit_test(oms_dss_object_set_failure_with_overwrite),
        cmocka_unit_test(oms_dss_filter_build_failure),
        cmocka_unit_test(oms_dss_object_get_failure),
        cmocka_unit_test(oms_dss_unlock_failure),
        cmocka_unit_test(oms_success_without_overwrite),
        cmocka_unit_test(oms_success_with_fake_overwrite),
        cmocka_unit_test(oms_success_with_overwrite),

        cmocka_unit_test(omd_dss_filter_build_for_get_failure),
        cmocka_unit_test(omd_dss_lock_failure),
        cmocka_unit_test(omd_dss_object_get_failure),
        cmocka_unit_test(omd_dss_filter_build_for_deprec_failure),
        cmocka_unit_test(omd_dss_deprecated_object_get_failure),
        cmocka_unit_test(omd_dss_filter_build_for_layout_failure),
        cmocka_unit_test(omd_dss_full_layout_get_failure),
        cmocka_unit_test(omd_dss_object_set_failure),
        cmocka_unit_test(omd_dss_object_move_failure),
        cmocka_unit_test(omd_dss_unlock_failure),
        cmocka_unit_test(omd_success),
    };

    pho_context_init();
    atexit(pho_context_fini);

    return cmocka_run_group_tests(object_md_test_cases, NULL, NULL);
}
