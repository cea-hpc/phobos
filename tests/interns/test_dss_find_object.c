/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2025 CEA/DAM.
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
 * \brief  Tests for dss_find_object function
 */

#include "test_setup.h"

#include <assert.h>
#include <setjmp.h>
#include <stdbool.h>
#include <cmocka.h>

#include "pho_dss.h"
#include "pho_type_utils.h"
#include "pho_dss_wrapper.h"

struct test_state {
    struct dss_handle *dss;
    struct object_info obj[6];
} global_state;

static int insert_state_obj(struct test_state *state, int index,
                            char *oid, char *uuid, int version, char *user_md)
{
    struct object_info *obj = state->obj + index;
    int rc;

    obj->oid = oid;
    obj->uuid = uuid;
    obj->version = version;
    obj->user_md = user_md;

    rc = dss_object_insert(state->dss, obj, 1, DSS_SET_FULL_INSERT);
    if (rc)
        return -1;

    return 0;
}

static int move_state_object_to_deprecated(struct test_state *state, int index)
{
    struct object_info *obj = state->obj + index;
    int rc;

    rc = dss_move_object_to_deprecated(state->dss, obj, 1);
    if (rc)
        return -1;

    return 0;
}

static int dfo_setup(void **state)
{
    int rc;

    *state = &global_state;

    rc = global_setup_dss_with_dbinit((void **)&global_state.dss);
    assert_return_code(rc, -rc);

    rc = insert_state_obj(&global_state, 0, "oid1", "uuid1", 1, "{}");
    assert_return_code(rc, -rc);

    rc = move_state_object_to_deprecated(&global_state, 0);
    assert_return_code(rc, -rc);

    rc = insert_state_obj(&global_state, 1, "oid1", "uuid1", 2, "{}");
    assert_return_code(rc, -rc);

    rc = move_state_object_to_deprecated(&global_state, 1);
    assert_return_code(rc, -rc);

    rc = insert_state_obj(&global_state, 2, "oid1", "uuid2", 1, "{}");
    assert_return_code(rc, -rc);

    rc = move_state_object_to_deprecated(&global_state, 2);
    assert_return_code(rc, -rc);

    rc = insert_state_obj(&global_state, 3, "oid1", "uuid2", 2, "{}");
    assert_return_code(rc, -rc);

    rc = insert_state_obj(&global_state, 4, "oid2", "uuid3", 1, "{}");
    assert_return_code(rc, -rc);

    rc = move_state_object_to_deprecated(&global_state, 4);
    assert_return_code(rc, -rc);

    rc = insert_state_obj(&global_state, 5, "oid2", "uuid4", 1, "{}");
    assert_return_code(rc, -rc);

    rc = move_state_object_to_deprecated(&global_state, 5);
    assert_return_code(rc, -rc);

    return 0;
}

static int dfo_teardown(void **void_state)
{
    struct test_state *state = (struct test_state *)*void_state;
    int rc;

    rc = global_teardown_dss_with_dbdrop((void **)&state->dss);
    if (rc)
        return -1;

    return 0;
}

static void assert_obj_in_state(const struct test_state *state,
                                int index,
                                const struct object_info *obj,
                                int rc)
{
    const struct object_info *target = state->obj + index;

    assert_string_equal(target->oid, obj->oid);
    assert_string_equal(target->uuid, obj->uuid);
    assert_int_equal(target->version, obj->version);
    assert_return_code(rc, -rc);
}

static void get_obj_and_check_res(struct test_state *state, int index,
                                   char *oid, char *uuid, int version,
                                   enum dss_obj_scope scope)
{
    struct object_info *obj;
    int rc;

    rc = dss_find_object(state->dss, oid, uuid, version, scope, &obj);
    assert_obj_in_state(state, index, obj, rc);
    object_info_free(obj);
}

static void check_dfo_fails_with_rc(struct test_state *state,
                                    char *oid, char *uuid, int version,
                                    enum dss_obj_scope scope,
                                    int expected_rc)
{
    struct object_info *obj;
    int rc;

    rc = dss_find_object(state->dss, oid, uuid, version, scope, &obj);
    assert_int_equal(rc, expected_rc);
}

/*
 * Table's State:
 *
 * +--------+------+-------+---------+--------------------+
 * | status | oid  | uuid  | version | global_state index |
 * +--------+------+-------+---------+--------------------+
 * | deprec | oid2 | uuid4 | 1       | 5                  |
 * | deprec | oid2 | uuid3 | 1       | 4                  |
 * +--------+------+-------+---------+--------------------+
 * | alive  | oid1 | uuid2 | 2       | 3                  |
 * | deprec | oid1 | uuid2 | 1       | 2                  |
 * | deprec | oid1 | uuid1 | 2       | 1                  |
 * | deprec | oid1 | uuid1 | 1       | 0                  |
 * +--------+------+-------+---------+--------------------+
 */

static void dfo_alive_object(void **void_state)
{
    struct test_state *state = (struct test_state *)*void_state;

    get_obj_and_check_res(state, 3, "oid1",    NULL, 0, DSS_OBJ_ALIVE);
    get_obj_and_check_res(state, 3, "oid1",    NULL, 2, DSS_OBJ_ALIVE);
    get_obj_and_check_res(state, 3, "oid1", "uuid2", 0, DSS_OBJ_ALIVE);
    get_obj_and_check_res(state, 3, "oid1", "uuid2", 2, DSS_OBJ_ALIVE);

    /* wrong version */
    check_dfo_fails_with_rc(state, "oid1", NULL, 1, DSS_OBJ_ALIVE, -ENOENT);

    /* wrong uuid */
    check_dfo_fails_with_rc(state, "oid1", "uuid1", 0, DSS_OBJ_ALIVE, -ENOENT);

    /* both wrong */
    check_dfo_fails_with_rc(state, "oid1", "uuid1", 1, DSS_OBJ_ALIVE, -ENOENT);
}

static void dfo_deprec_object(void **void_state)
{
    struct test_state *state = (struct test_state *)*void_state;

    /* get obj from alive */
    get_obj_and_check_res(state, 3, "oid1", NULL, 0, DSS_OBJ_ALL);
    get_obj_and_check_res(state, 3, "oid1", NULL, 2, DSS_OBJ_ALL);
    get_obj_and_check_res(state, 3, "oid1", "uuid2", 0, DSS_OBJ_ALL);

    /* get obj from deprecated */
    get_obj_and_check_res(state, 2, "oid1", "uuid2", 1, DSS_OBJ_ALL);

    /* get obj from deprecated with uuid */
    get_obj_and_check_res(state, 4, "oid2", "uuid3", 0, DSS_OBJ_ALL);

    /* Uuid and no version, get the latest version */
    get_obj_and_check_res(state, 1, "oid1", "uuid1", 0, DSS_OBJ_ALL);

    /* wrong version */
    check_dfo_fails_with_rc(state, "oid1", NULL, 3, DSS_OBJ_ALL, -ENOENT);

    /* wrong uuid */
    check_dfo_fails_with_rc(state, "oid1", "uuid3", 0, DSS_OBJ_ALL, -ENOENT);

    /* No uuid and no version, several results are possible => error */
    check_dfo_fails_with_rc(state, "oid2", NULL, 0, DSS_OBJ_ALL, -EINVAL);

    /* No uuid and several deprec object with the same version => error */
    check_dfo_fails_with_rc(state, "oid2", NULL, 1, DSS_OBJ_ALL, -EINVAL);
}

static void dfo_deprec_only_object(void **void_state)
{
    struct test_state *state = (struct test_state *)*void_state;

    get_obj_and_check_res(state, 2, "oid1", "uuid2", 1, DSS_OBJ_DEPRECATED);

    /* No uuid and no version, several results are possible => error */
    check_dfo_fails_with_rc(state, "oid1", NULL, 0, DSS_OBJ_DEPRECATED,
                            -EINVAL);

    /* No uuid and several deprec object with the same version => error */
    check_dfo_fails_with_rc(state, "oid1", NULL, 1, DSS_OBJ_DEPRECATED,
                            -EINVAL);

    /* Uuid and no version, get the latest version */
    get_obj_and_check_res(state, 1, "oid1", "uuid1", 0, DSS_OBJ_DEPRECATED);
}

int main(void)
{
    const struct CMUnitTest object_md_save_test_cases[] = {
        cmocka_unit_test(dfo_alive_object),
        cmocka_unit_test(dfo_deprec_object),
        cmocka_unit_test(dfo_deprec_only_object),
    };

    pho_context_init();
    atexit(pho_context_fini);

    return cmocka_run_group_tests(object_md_save_test_cases,
                                  dfo_setup, dfo_teardown);
}
