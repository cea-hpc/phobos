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
 * \brief  Tests for Store object_md_get operations
 */

#include "test_setup.h"

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
    struct object_info obj[2];
    struct pho_xfer_desc xfer;
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

static int omg_setup(void **state)
{
    int rc;

    *state = &global_state;

    rc = global_setup_dss_with_dbinit((void **)&global_state.dss);
    if (rc)
        return -1;

    rc = insert_state_obj(&global_state, 0, "oid1", "uuid1", 1,
                          "{\"titi\": \"tutu\"}");
    if (rc)
        return -1;

    rc = insert_state_obj(&global_state, 1, "oid2", "uuid2", 1,
                          "{\"titi\": \"tutu\"}");
    if (rc)
        return -1;
    rc = move_state_object_to_deprecated(&global_state, 1);
    if (rc)
        return -1;

    return 0;
}

static int omg_teardown(void **void_state)
{
    struct test_state *state = (struct test_state *)*void_state;
    int rc;

    rc = global_teardown_dss_with_dbdrop((void **)&state->dss);
    if (rc)
        return -1;

    return 0;
}

static void assert_xfer_in_state(const struct test_state *state,
                                 int index,
                                 int rc)
{
    const struct object_info *obj = state->obj + index;
    GString *gstr = g_string_new(NULL);

    pho_attrs_to_json(&state->xfer.xd_attrs, gstr, 0);
    assert_string_equal(gstr->str, obj->user_md);
    g_string_free(gstr, true);

    assert_string_equal(state->xfer.xd_objid,   obj->oid);
    assert_string_equal(state->xfer.xd_objuuid, obj->uuid);

    assert_int_equal(state->xfer.xd_version, obj->version);
    assert_return_code(rc, -rc);
}

static void update_state_xfer(struct test_state *state, char *oid, char *uuid,
                              int version)
{
    state->xfer.xd_objid = oid;
    state->xfer.xd_objuuid = uuid;
    state->xfer.xd_version = version;
}

static void clean_state_xfer(struct test_state *state)
{
    free(state->xfer.xd_objuuid);
    pho_attrs_free(&state->xfer.xd_attrs);

    state->xfer.xd_version = 0;
    state->xfer.xd_objid = NULL;
    state->xfer.xd_objuuid = NULL;
}

static void get_xfer_and_check_res(struct test_state *state, int index,
                                   char *oid, char *uuid, int version)
{
    int rc;

    update_state_xfer(state, oid, uuid, version);
    rc = object_md_get(state->dss, &state->xfer);
    assert_xfer_in_state(state, index, rc);
    clean_state_xfer(state);
}

static void check_omg_fails_with_rc(struct test_state *state,
                                    char *oid, char *uuid, int version,
                                    int expected_rc)
{
    int rc;

    update_state_xfer(state, oid, uuid, version);
    rc = object_md_get(state->dss, &state->xfer);
    assert_int_equal(rc, expected_rc);
}

/*
 * Table's State:
 *
 * +--------+------+-------+---------+------------+--------------------+
 * | status | oid  | uuid  | version | used_md    | global_state index |
 * +--------+------+-------+---------+------------+--------------------+
 * | deprec | oid2 | uuid3 | 1       | titi: tutu | 1                  |
 * +--------+------+-------+---------+------------+--------------------+
 * | alive  | oid1 | uuid1 | 1       | titi: tutu | 0                  |
 * +--------+------+-------+---------+------------+--------------------+
 */
static void omg_alive_object(void **void_state)
{
    struct test_state *state = (struct test_state *)*void_state;

    /* get alive object */
    get_xfer_and_check_res(state, 0, "oid1", NULL, 0);

    /* since uuid and version are not used, make sure that they are correctly
     * overwitten and that the call doesn't fail
     */
    get_xfer_and_check_res(state, 0, "oid1", "uuid1",  0);
    get_xfer_and_check_res(state, 0, "oid1", "uuid1",  1);
    get_xfer_and_check_res(state, 0, "oid1", "uuid1", 10);
    get_xfer_and_check_res(state, 0, "oid1", "uuid6",  0);
    get_xfer_and_check_res(state, 0, "oid1",    NULL,  4);
}

static void omg_enoent(void **void_state)
{
    struct test_state *state = (struct test_state *)*void_state;

    /* check that the call fails with a deprecated object */
    check_omg_fails_with_rc(state, "oid2", NULL, 0, -ENOENT);

    /* check that the call fails with no oid */
    check_omg_fails_with_rc(state, NULL,    NULL, 0, -ENOENT);
    check_omg_fails_with_rc(state, NULL, "uuid1", 0, -ENOENT);
    check_omg_fails_with_rc(state, NULL,    NULL, 1, -ENOENT);
    check_omg_fails_with_rc(state, NULL, "uuid1", 1, -ENOENT);
}

static void omg_filter_build_fail(void **void_state)
{
    struct test_state *state = (struct test_state *)*void_state;

    /* check that the function returns if dss_filter_build fails */
    check_omg_fails_with_rc(state, "oid1\"", NULL, 0, -EINVAL);
}

int main(void)
{
    const struct CMUnitTest object_md_save_test_cases[] = {
        cmocka_unit_test(omg_alive_object),
        cmocka_unit_test(omg_enoent),
        cmocka_unit_test(omg_filter_build_fail),
    };

    pho_context_init();
    atexit(pho_context_fini);

    return cmocka_run_group_tests(object_md_save_test_cases,
                                  omg_setup, omg_teardown);
}
