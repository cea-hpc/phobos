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
 * \brief  Tests for dss_object_move function
 */

/* phobos stuff */
#include "test_setup.h"
#include "pho_dss.h"
#include "pho_types.h"

/* standard stuff */
#include <stdlib.h>
#include <unistd.h>

/* cmocka stuff */
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <cmocka.h>

/* dom_simple_ok */
static struct object_info OBJ = {
    .oid = "object_to_move",
    .user_md = "{}"
};

/* dom_simple_ok and dom_simple_already_exist shares the same setup */
static int dom_simple_setup(void **state)
{
    struct dss_handle *handle = (struct dss_handle *)*state;
    int rc;

    /* insert the object */
    rc = dss_object_set(handle, &OBJ, 1, DSS_SET_INSERT);
    if (rc)
        return -1;

    return 0;
}

static void dom_simple_ok(void **state)
{
    struct dss_handle *handle = (struct dss_handle *)*state;
    struct object_info *obj_res;
    struct dss_filter filter;
    int obj_cnt;
    int rc;

    /* move to deprecated_object */
    rc = dss_object_move(handle, DSS_OBJECT, DSS_DEPREC, &OBJ, 1);
    assert_return_code(rc, -rc);

    /* check the object was moved from object to deprecated table */
    rc = dss_filter_build(&filter, "{\"DSS::OBJ::oid\": \"%s\"}", OBJ.oid);
    assert_return_code(rc, -rc);
    /* check object is no more into object table */
    rc = dss_object_get(handle, &filter, &obj_res, &obj_cnt);
    assert_return_code(rc, -rc);
    assert_int_equal(obj_cnt, 0);
    dss_res_free(obj_res, obj_cnt);
    /* check object is now into the object table */
    rc = dss_deprecated_object_get(handle, &filter, &obj_res, &obj_cnt);
    assert_return_code(rc, -rc);
    assert_int_equal(obj_cnt, 1);

    /* move back from deprecated_object to object */
    rc = dss_object_move(handle, DSS_DEPREC, DSS_OBJECT, obj_res, 1);
    dss_res_free(obj_res, obj_cnt);
    assert_return_code(rc, -rc);

    /* check the object is no more into deprecated table */
    rc = dss_deprecated_object_get(handle, &filter, &obj_res, &obj_cnt);
    assert_return_code(rc, -rc);
    assert_int_equal(obj_cnt, 0);
    dss_res_free(obj_res, obj_cnt);
    /* check the object is back into the object table */
    rc = dss_object_get(handle, &filter, &obj_res, &obj_cnt);
    dss_filter_free(&filter);
    dss_res_free(obj_res, obj_cnt);
    assert_return_code(rc, -rc);
    assert_int_equal(obj_cnt, 1);
}

static int dom_simple_ok_teardown(void **state)
{
    struct dss_handle *handle = (struct dss_handle *)*state;
    int rc;

    /* delete the object */
    rc = dss_object_set(handle, &OBJ, 1, DSS_SET_DELETE);
    if (rc)
        return -1;

    return 0;
}

/* dom_3_ok */
static const char *OBJ_3_OID_REGEXP = "^object_[012]";
static struct object_info OBJ_3[] = {
    { .oid = "object_0", .user_md = "{}" },
    { .oid = "object_1", .user_md = "{}" },
    { .oid = "object_2", .user_md = "{}" }
};

static int dom_3_ok_setup(void **state)
{
    struct dss_handle *handle = (struct dss_handle *)*state;
    int rc;

    /* insert the objects */
    rc = dss_object_set(handle, OBJ_3, 3, DSS_SET_INSERT);
    if (rc)
        return -1;

    return 0;
}

static void dom_3_ok(void **state)
{
    struct dss_handle *handle = (struct dss_handle *)*state;
    struct object_info *obj_res;
    struct dss_filter filter;
    int obj_cnt;
    int rc;

    /* move to deprecated_object */
    rc = dss_object_move(handle, DSS_OBJECT, DSS_DEPREC, OBJ_3, 3);
    assert_return_code(rc, -rc);

    /* check objects are moved from object to deprecated table */
    rc = dss_filter_build(&filter,
                          "{\"$REGEXP\": {\"DSS::OBJ::oid\": \"%s\"}}",
                          OBJ_3_OID_REGEXP);
    assert_return_code(rc, -rc);
    /* check objects are no more into the object table */
    rc = dss_object_get(handle, &filter, &obj_res, &obj_cnt);
    assert_return_code(rc, -rc);
    assert_int_equal(obj_cnt, 0);
    dss_res_free(obj_res, obj_cnt);
    /* check objects are now into the deprectad_object table */
    rc = dss_deprecated_object_get(handle, &filter, &obj_res, &obj_cnt);
    assert_return_code(rc, -rc);
    assert_int_equal(obj_cnt, 3);

    /* move back from deprecated_object to object */
    rc = dss_object_move(handle, DSS_DEPREC, DSS_OBJECT, obj_res, 3);
    dss_res_free(obj_res, obj_cnt);
    assert_return_code(rc, -rc);

    /* check objects are no more into the deprecated_object table */
    rc = dss_deprecated_object_get(handle, &filter, &obj_res, &obj_cnt);
    assert_return_code(rc, -rc);
    assert_int_equal(obj_cnt, 0);
    dss_res_free(obj_res, obj_cnt);
    /* check the objects are back */
    rc = dss_object_get(handle, &filter, &obj_res, &obj_cnt);
    dss_filter_free(&filter);
    dss_res_free(obj_res, obj_cnt);
    assert_return_code(rc, -rc);
    assert_int_equal(obj_cnt, 3);
}

static int dom_3_ok_teardown(void **state)
{
    struct dss_handle *handle = (struct dss_handle *)*state;
    int rc;

    /* delete the objects */
    rc = dss_object_set(handle, OBJ_3, 3, DSS_SET_DELETE);
    if (rc)
        return -1;

    return 0;
}

/* dom_type_einval */
static void dom_type_einval(void **state)
{
    struct dss_handle *handle = (struct dss_handle *)*state;
    struct object_info obj;
    int rc;

    /* move to invalid type */
    obj.oid = "object_to_move";
    obj.user_md = "{}";
    rc = dss_object_move(handle, DSS_OBJECT, DSS_MEDIA, &obj, 1);
    assert_int_equal(rc, -EINVAL);
}

/* dom_simple_already_exist */

/* setup is mutualized with dom_simple_ok as dom_simple_setup */

static void dom_simple_already_exist(void **state)
{
    struct dss_handle *handle = (struct dss_handle *)*state;
    struct object_info *obj_res;
    struct dss_filter filter;
    int obj_cnt;
    int rc;

    /* move to deprecated_object */
    rc = dss_object_move(handle, DSS_OBJECT, DSS_DEPREC, &OBJ, 1);
    assert_return_code(rc, -rc);

    /* check the deprecated_object */
    rc = dss_filter_build(&filter, "{\"DSS::OBJ::oid\": \"%s\"}", OBJ.oid);
    assert_return_code(rc, -rc);
    rc = dss_deprecated_object_get(handle, &filter, &obj_res, &obj_cnt);
    dss_filter_free(&filter);
    assert_return_code(rc, -rc);
    assert_int_equal(obj_cnt, 1);

    /* insert again object before moving back */
    rc = dss_object_set(handle, &OBJ, 1, DSS_SET_INSERT);
    assert_return_code(rc, -rc);

    /* trying to move back from deprecated_object to object */
    rc = dss_object_move(handle, DSS_DEPREC, DSS_OBJECT, obj_res, 1);
    dss_res_free(obj_res, obj_cnt);
    assert_int_equal(rc, -EEXIST);
}

static int dom_simple_already_exist_teardown(void **state)
{
    struct dss_handle *handle = (struct dss_handle *)*state;
    struct object_info *obj_res;
    struct dss_filter filter;
    int obj_cnt;
    int rc;

    /* delete the object */
    rc = dss_object_set(handle, &OBJ, 1, DSS_SET_DELETE);
    if (rc)
        return -1;

    /* delete the deprecated object */
    rc = dss_filter_build(&filter,
                          "{\"DSS::OBJ::oid\": \"%s\"}", OBJ.oid);
    if (rc)
        return -1;

    rc = dss_deprecated_object_get(handle, &filter, &obj_res, &obj_cnt);
    dss_filter_free(&filter);
    if (rc)
        return -1;

    rc = dss_deprecated_object_set(handle, obj_res, obj_cnt, DSS_SET_DELETE);
    dss_res_free(obj_res, obj_cnt);
    if (rc)
        return -1;

    return 0;
}


int main(void)
{
    const struct CMUnitTest dss_object_move_cases[] = {
        cmocka_unit_test_setup_teardown(dom_simple_ok, dom_simple_setup,
                                        dom_simple_ok_teardown),
        cmocka_unit_test_setup_teardown(dom_3_ok, dom_3_ok_setup,
                                        dom_3_ok_teardown),
        cmocka_unit_test(dom_type_einval),
        cmocka_unit_test_setup_teardown(dom_simple_already_exist,
                                        dom_simple_setup,
                                        dom_simple_already_exist_teardown),
    };

    pho_context_init();
    atexit(pho_context_fini);

    return cmocka_run_group_tests(dss_object_move_cases,
                                  global_setup_dss_with_dbinit,
                                  global_teardown_dss_with_dbdrop);
}
