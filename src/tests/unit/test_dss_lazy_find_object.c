/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2021 CEA/DAM.
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
 * \brief  Tests for dss_lazy_find_object function
 */

/* phobos stuff */
#include "pho_dss.h"
#include "pho_types.h"
#include "pho_type_utils.h"
#include "test_setup.h"

/* standard stuff */
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

/* cmocka stuff */
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <cmocka.h>

#define ASSERT_OID_UUID_VERSION(_obj_1, _obj_2)             \
do {                                                        \
    assert_string_equal((_obj_1).oid, (_obj_2).oid);        \
    assert_string_equal((_obj_1).uuid, (_obj_2).uuid);      \
    assert_int_equal((_obj_1).version, (_obj_2).version);   \
} while (0)

#define BAD_VERSION_SHIFT 77
#define FIRST_UUID "uuid"
#define SECOND_UUID "new_uuid"

static void check_oid_uuid_version(struct dss_handle *dss,
                                   struct object_info obj)
{
    struct object_info *found_obj;
    int rc;

    /* oid: ok */
    rc = dss_lazy_find_object(dss, obj.oid, NULL, 0, &found_obj);
    assert_return_code(rc, -rc);
    ASSERT_OID_UUID_VERSION(*found_obj, obj);
    object_info_free(found_obj);

    /* oid, version: ok */
    rc = dss_lazy_find_object(dss, obj.oid, NULL, obj.version, &found_obj);
    assert_return_code(rc, -rc);
    ASSERT_OID_UUID_VERSION(*found_obj, obj);
    object_info_free(found_obj);

    /* uuid: ok */
    rc = dss_lazy_find_object(dss, NULL, obj.uuid, 0, &found_obj);
    assert_return_code(rc, -rc);
    ASSERT_OID_UUID_VERSION(*found_obj, obj);
    object_info_free(found_obj);

    /* uuid, version: ok */
    rc = dss_lazy_find_object(dss, NULL, obj.uuid, obj.version, &found_obj);
    assert_return_code(rc, -rc);
    ASSERT_OID_UUID_VERSION(*found_obj, obj);
    object_info_free(found_obj);

    /* oid, uuid: ok */
    rc = dss_lazy_find_object(dss, obj.oid, obj.uuid, 0, &found_obj);
    assert_return_code(rc, -rc);
    ASSERT_OID_UUID_VERSION(*found_obj, obj);
    object_info_free(found_obj);

    /* oid, uuid, version: ok */
    rc = dss_lazy_find_object(dss, obj.oid, obj.uuid, obj.version, &found_obj);
    assert_return_code(rc, -rc);
    ASSERT_OID_UUID_VERSION(*found_obj, obj);
    object_info_free(found_obj);

    /* bad oid: ENOENT */
    rc = dss_lazy_find_object(dss, "bad", NULL, 0, &found_obj);
    assert_int_equal(rc, -ENOENT);

    /* bad uuid: ENOENT */
    rc = dss_lazy_find_object(dss, NULL, "bad", 0, &found_obj);
    assert_int_equal(rc, -ENOENT);

    /* oid, bad uuid: ENOENT */
    rc = dss_lazy_find_object(dss, obj.oid, "bad", 0, &found_obj);
    assert_int_equal(rc, -ENOENT);

    /* bad oid, uuid: ENOENT */
    rc = dss_lazy_find_object(dss, "bad", obj.uuid, 0, &found_obj);
    assert_int_equal(rc, -ENOENT);

    /* oid, bad version: ENOENT */
    rc = dss_lazy_find_object(dss, obj.oid, NULL,
                              obj.version + BAD_VERSION_SHIFT, &found_obj);
    assert_int_equal(rc, -ENOENT);

    /* uuid, bad version: ENOENT */
    rc = dss_lazy_find_object(dss, NULL, obj.uuid,
                              obj.version + BAD_VERSION_SHIFT, &found_obj);
    assert_int_equal(rc, -ENOENT);

    /* oid, uuid, bad version: ENOENT */
    rc = dss_lazy_find_object(dss, obj.oid, obj.uuid,
                              obj.version + BAD_VERSION_SHIFT, &found_obj);
    assert_int_equal(rc, -ENOENT);
}

static void two_versions_check_oid_uuid_version(struct dss_handle *dss,
                                                struct object_info obj)
{
    struct object_info *found_obj;
    int rc;

    /* find new version */
    check_oid_uuid_version(dss, obj);
    obj.version -= 1;
    /* find old version */
    /* oid, version: ok */
    rc = dss_lazy_find_object(dss, obj.oid, NULL, obj.version, &found_obj);
    assert_return_code(rc, -rc);
    ASSERT_OID_UUID_VERSION(*found_obj, obj);
    object_info_free(found_obj);

    /* uuid, version: ok */
    rc = dss_lazy_find_object(dss, NULL, obj.uuid, obj.version, &found_obj);
    assert_return_code(rc, -rc);
    ASSERT_OID_UUID_VERSION(*found_obj, obj);
    object_info_free(found_obj);

    /* oid, uuid, version: ok */
    rc = dss_lazy_find_object(dss, obj.oid, obj.uuid, obj.version, &found_obj);
    assert_return_code(rc, -rc);
    ASSERT_OID_UUID_VERSION(*found_obj, obj);
    object_info_free(found_obj);
}

/* test dss_lazy_find_object */
static void test_dlfo(void **state)
{
    struct dss_handle *dss = (struct dss_handle *)*state;
    struct object_info *found_obj;
    struct object_info obj = {
        .oid = "oid",
        .uuid = FIRST_UUID,
        .version = 1,
        .user_md = "{}"
    };
    int rc;

    /********************************/
    /* one object into object table */
    /********************************/
    rc = dss_object_set(dss, &obj, 1, DSS_SET_FULL_INSERT);
    assert_return_code(rc, -rc);
    check_oid_uuid_version(dss, obj);

    /*******************************************/
    /* one object into deprecated_object table */
    /*******************************************/
    /* move to deprecated_object */
    rc = dss_object_move(dss, DSS_OBJECT, DSS_DEPREC, &obj, 1);
    assert_return_code(rc, -rc);
    check_oid_uuid_version(dss, obj);

    /******************************************************/
    /* one object into object and deprecated_object table */
    /******************************************************/
    /* add a new living version */
    obj.version += 1;
    rc = dss_object_set(dss, &obj, 1, DSS_SET_FULL_INSERT);
    assert_return_code(rc, -rc);
    two_versions_check_oid_uuid_version(dss, obj);

    /********************************************/
    /* two objects into deprecated_object table */
    /********************************************/
    /* move new version to deprecated */
    rc = dss_object_move(dss, DSS_OBJECT, DSS_DEPREC, &obj, 1);
    assert_return_code(rc, -rc);
    two_versions_check_oid_uuid_version(dss, obj);

    /*************************************************************************/
    /* two objects into deprecated_object table and one new uuid into object */
    /*************************************************************************/
    /* add new object with same oid but new uuid */
    obj.version = 1;
    obj.uuid = SECOND_UUID;
    rc = dss_object_set(dss, &obj, 1, DSS_SET_FULL_INSERT);
    assert_return_code(rc, -rc);
    check_oid_uuid_version(dss, obj);

    obj.uuid = FIRST_UUID;
    /* first_uuid, version: ok */
    rc = dss_lazy_find_object(dss, NULL, obj.uuid, obj.version, &found_obj);
    assert_return_code(rc, -rc);
    ASSERT_OID_UUID_VERSION(*found_obj, obj);
    object_info_free(found_obj);

    /* oid, first_uuid, version: ok */
    rc = dss_lazy_find_object(dss, obj.oid, obj.uuid, obj.version, &found_obj);
    assert_return_code(rc, -rc);
    ASSERT_OID_UUID_VERSION(*found_obj, obj);
    object_info_free(found_obj);

    obj.version += 1;
    /* first_uuid: ok */
    rc = dss_lazy_find_object(dss, NULL, obj.uuid, 0, &found_obj);
    assert_return_code(rc, -rc);
    ASSERT_OID_UUID_VERSION(*found_obj, obj);
    object_info_free(found_obj);

    /***********************************************************************/
    /* three objects into deprecated_object table (2*1st uuid, 1*2nd uuid) */
    /***********************************************************************/
    /* move new uuid to deprecated */
    obj.version -= 1;
    obj.uuid = SECOND_UUID;
    rc = dss_object_move(dss, DSS_OBJECT, DSS_DEPREC, &obj, 1);
    assert_return_code(rc, -rc);

    /* oid: EINVAL */
    rc = dss_lazy_find_object(dss, obj.oid, NULL, 0, &found_obj);
    assert_int_equal(rc, -EINVAL);

    /* oid, version == 1 : EINVAL */
    rc = dss_lazy_find_object(dss, obj.oid, NULL, obj.version, &found_obj);
    assert_int_equal(rc, -EINVAL);

    /* oid, version == 2 : ok */
    obj.version += 1;
    obj.uuid = FIRST_UUID;
    rc = dss_lazy_find_object(dss, obj.oid, NULL, obj.version, &found_obj);
    assert_return_code(rc, -rc);
    ASSERT_OID_UUID_VERSION(*found_obj, obj);
    object_info_free(found_obj);
}

int main(void)
{
    const struct CMUnitTest dss_lazy_find_object_cases[] = {
        cmocka_unit_test(test_dlfo),
    };

    return cmocka_run_group_tests(dss_lazy_find_object_cases, global_setup_dss,
                                  global_teardown_dss);
}
