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
 * \brief  Tests for dss_lazy_find_object function
 */

#include "../test_setup.h"

#include <assert.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>

#include "pho_dss.h"
#include "pho_type_utils.h"

#include "store_utils.h"

#include <stdlib.h> /* malloc, setenv, unsetenv */
#include <unistd.h> /* execl, exit, fork */
#include <sys/wait.h> /* wait */
#include <cmocka.h>

struct test_state {
    struct dss_handle *dss;
    struct object_info obj[9];
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

    rc = dss_object_set(state->dss, obj, 1, DSS_SET_FULL_INSERT);
    if (rc)
        return -1;

    return 0;
}

static int move_state_object_to_deprecated(struct test_state *state, int index)
{
    struct object_info *obj = state->obj + index;
    int rc;

    rc = dss_object_move(state->dss, DSS_OBJECT, DSS_DEPREC, obj, 1);
    if (rc)
        return -1;

    return 0;
}

static int dlfo_setup(void **state)
{
    int rc;

    *state = &global_state;

    rc = global_setup_dss((void **)&global_state.dss);
    if (rc)
        return -1;

    rc = insert_state_obj(&global_state, 0, "oid1", "uuid1", 1,
                          "{\"titi\": \"tutu\"}");
    if (rc)
        return -1;
    rc = move_state_object_to_deprecated(&global_state, 0);
    if (rc)
        return -1;

    rc = insert_state_obj(&global_state, 1, "oid1", "uuid1", 2,
                          "{\"titi\": \"toto\"}");
    if (rc)
        return -1;
    rc = move_state_object_to_deprecated(&global_state, 1);
    if (rc)
        return -1;

    rc = insert_state_obj(&global_state, 2, "oid1", "uuid2", 3,
                          "{\"titi\": \"tata\"}");
    if (rc)
        return -1;
    rc = move_state_object_to_deprecated(&global_state, 2);
    if (rc)
        return -1;

    rc = insert_state_obj(&global_state, 3, "oid1", "uuid2", 4,
                          "{\"toto\": \"titi\"}");
    if (rc)
        return -1;

    rc = insert_state_obj(&global_state, 4, "oid2", "uuid3", 1,
                          "{\"titi\": \"tutu\"}");
    if (rc)
        return -1;
    rc = move_state_object_to_deprecated(&global_state, 4);
    if (rc)
        return -1;

    rc = insert_state_obj(&global_state, 5, "oid2", "uuid4", 2,
                          "{\"titi\": \"toto\"}");
    if (rc)
        return -1;
    rc = move_state_object_to_deprecated(&global_state, 5);
    if (rc)
        return -1;

    rc = insert_state_obj(&global_state, 6, "oid3", "uuid5", 1,
                          "{\"titi\": \"tutu\"}");
    if (rc)
        return -1;
    rc = move_state_object_to_deprecated(&global_state, 6);
    if (rc)
        return -1;

    rc = insert_state_obj(&global_state, 7, "oid3", "uuid5", 2,
                          "{\"titi\": \"toto\"}");
    if (rc)
        return -1;
    rc = move_state_object_to_deprecated(&global_state, 7);
    if (rc)
        return -1;

    rc = insert_state_obj(&global_state, 8, "oid4", "uuid6", 1,
                          "{\"no\": \"md\"}");
    if (rc)
        return -1;
    rc = move_state_object_to_deprecated(&global_state, 8);

    return 0;
}

static int dlfo_teardown(void **void_state)
{
    struct test_state *state = (struct test_state *)*void_state;
    int rc;

    rc = global_teardown_dss((void **)&state->dss);
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

    assert_string_equal(target->user_md, obj->user_md);
    assert_string_equal(target->oid, obj->oid);
    assert_string_equal(target->uuid, obj->uuid);
    assert_int_equal(target->version, obj->version);
    assert_return_code(rc, -rc);
}

static void get_obj_and_check_res(struct test_state *state, int index,
                                   char *oid, char *uuid, int version)
{
    struct object_info *obj;
    int rc;

    rc = dss_lazy_find_object(state->dss, oid, uuid, version, &obj);
    assert_obj_in_state(state, index, obj, rc);
    object_info_free(obj);
}

static void check_dlfo_fails_with_rc(struct test_state *state,
                                    char *oid, char *uuid, int version,
                                    int expected_rc)
{
    struct object_info *obj;
    int rc;

    rc = dss_lazy_find_object(state->dss, oid, uuid, version, &obj);
    assert_int_equal(rc, expected_rc);
}

/*
 * Table's State:
 *
 * +--------+------+-------+---------+------------+--------------------+
 * | status | oid  | uuid  | version | used_md    | global_state index |
 * +--------+------+-------+---------+------------+--------------------+
 * | deprec | oid4 | uuid6 | 1       | no: md     | 8                  |
 * +--------+------+-------+---------+------------+--------------------+
 * | deprec | oid3 | uuid5 | 2       | titi: toto | 7                  |
 * | deprec | oid3 | uuid5 | 1       | titi: tutu | 6                  |
 * +--------+------+-------+---------+------------+--------------------+
 * | deprec | oid2 | uuid4 | 2       | titi: toto | 5                  |
 * | deprec | oid2 | uuid3 | 1       | titi: tutu | 4                  |
 * +--------+------+-------+---------+------------+--------------------+
 * | alive  | oid1 | uuid2 | 4       | toto: titi | 3                  |
 * | deprec | oid1 | uuid2 | 3       | titi: tata | 2                  |
 * | deprec | oid1 | uuid1 | 2       | titi: toto | 1                  |
 * | deprec | oid1 | uuid1 | 1       | titi: tutu | 0                  |
 * +--------+------+-------+---------+------------+--------------------+
 */
static void dlfo_alive_object(void **void_state)
{
    struct test_state *state = (struct test_state *)*void_state;

    get_obj_and_check_res(state, 3, "oid1",    NULL, 0);
    get_obj_and_check_res(state, 3, "oid1",    NULL, 4);
    get_obj_and_check_res(state, 3, "oid1", "uuid2", 0);
    get_obj_and_check_res(state, 3, "oid1", "uuid2", 4);
    get_obj_and_check_res(state, 3,   NULL, "uuid2", 0);
    get_obj_and_check_res(state, 3,   NULL, "uuid2", 4);
}

static void dlfo_deprecated_object(void **void_state)
{
    struct test_state *state = (struct test_state *)*void_state;

    /* current generation's deprecated version */
    get_obj_and_check_res(state, 2, "oid1", NULL, 3);

    /* old generation's deprecated version */
    check_dlfo_fails_with_rc(state, "oid1", NULL, 1, -ENOENT);

    /* get most recent object from old generation */
    get_obj_and_check_res(state, 1, "oid1", "uuid1", 0);

    /* correct oid, wrong version */
    check_dlfo_fails_with_rc(state, "oid1", NULL, 5, -ENOENT);

    /* get version 1 of old generation */
    get_obj_and_check_res(state, 0, "oid1", "uuid1", 1);

    /* get version 3 of current generation */
    get_obj_and_check_res(state, 2, "oid1", "uuid2", 3);

    /* get deprecated object without uuid but uuids are not unique */
    check_dlfo_fails_with_rc(state, "oid2", NULL, 1, -EINVAL);

    /* oid not in alive and no version or uuid */
    check_dlfo_fails_with_rc(state, "oid2", NULL, 0, -ENOENT);

    /* uuid and not version, oid not in alive should get most recent version */
    get_obj_and_check_res(state, 7, "oid3", "uuid5", 0);

    /* oid in alive but corresponding object in deprecated */
    get_obj_and_check_res(state, 0, "oid1", "uuid1", 1);

    /* get deprecated object without uuid and uuids are the same */
    get_obj_and_check_res(state, 7, "oid3", NULL, 2);

    /* oid not in alive get specific uuid and version */
    get_obj_and_check_res(state, 6, "oid3", "uuid5", 1);

    /* oid not in alive, version and not uuid get specific version if uuids
     * are the same.
     */
    get_obj_and_check_res(state, 7, "oid3", NULL, 2);
    get_obj_and_check_res(state, 6, "oid3", NULL, 1);
    /* fails with wrong version */
    check_dlfo_fails_with_rc(state, "oid3", NULL, 3, -ENOENT);

    /* no uuid, invalid version on 1 object in deprecated */
    check_dlfo_fails_with_rc(state, "oid4", NULL, 5, -ENOENT);
}

static void dlfo_deprecated_object_with_uuid(void **void_state)
{
    struct test_state *state = (struct test_state *)*void_state;

    /* previous generation of oid1, most recent */
    get_obj_and_check_res(state, 1, NULL, "uuid1", 0);

    /* previous generation of oid1, version 1 */
    get_obj_and_check_res(state, 0, NULL, "uuid1", 1);

    /* previous generation of oid1, invalid version */
    check_dlfo_fails_with_rc(state, NULL, "uuid1", 4, -ENOENT);

    /* no alive version, only one deprecated */
    get_obj_and_check_res(state, 8, NULL, "uuid6", 0);

    /* no alive version, two deprecated */
    get_obj_and_check_res(state, 7, NULL, "uuid5", 0);
    get_obj_and_check_res(state, 6, NULL, "uuid5", 1);

    /* no alive version, two different deprecated generations */
    get_obj_and_check_res(state, 5, NULL, "uuid4", 0);
    get_obj_and_check_res(state, 4, NULL, "uuid3", 0);
}

int main(void)
{
    const struct CMUnitTest object_md_save_test_cases[] = {
        cmocka_unit_test(dlfo_alive_object),
        cmocka_unit_test(dlfo_deprecated_object),
        cmocka_unit_test(dlfo_deprecated_object_with_uuid),
    };

    return cmocka_run_group_tests(object_md_save_test_cases,
                                  dlfo_setup, dlfo_teardown);
}
