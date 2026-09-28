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
#include "pho_dss_wrapper.h"
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

    pho_attrs_to_json(&state->xfer.xd_targets->xt_attrs, gstr, 0);
    assert_string_equal(gstr->str, obj->user_md);
    g_string_free(gstr, true);

    if (state->xfer.xd_targets->xt_objid)
        assert_string_equal(state->xfer.xd_targets->xt_objid, obj->oid);
    else
        assert_null(state->xfer.xd_targets->xt_objid);

    if (state->xfer.xd_targets->xt_objuuid)
        assert_string_equal(state->xfer.xd_targets->xt_objuuid, obj->uuid);
    else
        assert_null(state->xfer.xd_targets->xt_objuuid);

    assert_int_equal(state->xfer.xd_targets->xt_version, obj->version);
    assert_return_code(rc, -rc);
}

static void update_state_xfer(struct test_state *state, char *oid, char *uuid,
                              int version)
{
    state->xfer.xd_targets->xt_version = version;

    free((void *)state->xfer.xd_targets->xt_objid);
    state->xfer.xd_targets->xt_objid = oid ? xstrdup(oid) : NULL;

    free(state->xfer.xd_targets->xt_objuuid);
    state->xfer.xd_targets->xt_objuuid = uuid ? xstrdup(uuid) : NULL;
}

static void clean_state_xfer(struct test_state *state)
{
    free((void *)state->xfer.xd_targets->xt_objid);
    free(state->xfer.xd_targets->xt_objuuid);
    pho_attrs_free(&state->xfer.xd_targets->xt_attrs);

    state->xfer.xd_targets->xt_version = 0;
    state->xfer.xd_targets->xt_objid = NULL;
    state->xfer.xd_targets->xt_objuuid = NULL;
}

static void get_xfer_and_check_res(struct test_state *state, int index,
                                   char *oid, char *uuid, int version)
{
    int rc;

    update_state_xfer(state, oid, uuid, version);
    rc = object_md_get(state->dss, state->xfer.xd_targets);
    assert_xfer_in_state(state, index, rc);
    clean_state_xfer(state);
}

static void check_omg_fails_with_rc(struct test_state *state,
                                    char *oid, char *uuid, int version,
                                    int expected_rc)
{
    int rc;

    update_state_xfer(state, oid, uuid, version);
    rc = object_md_get(state->dss, state->xfer.xd_targets);
    assert_int_equal(rc, expected_rc);
    clean_state_xfer(state);
}

static void check_obj_md(struct dss_handle *dss, const char *oid,
                         const char *expected_user_md)
{
    struct object_info *objs = NULL;
    struct dss_filter filter;
    int obj_cnt = 0;
    int rc;

    rc = dss_filter_build(&filter, "{\"DSS::OBJ::oid\": \"%s\"}", oid);
    assert_return_code(rc, -rc);

    rc = dss_object_get(dss, &filter, &objs, &obj_cnt, NULL);
    dss_filter_free(&filter);
    assert_return_code(rc, -rc);

    assert_int_equal(obj_cnt, 1);
    assert_string_equal(objs[0].oid, oid);
    assert_string_equal(objs[0].user_md, expected_user_md);

    dss_res_free(objs, obj_cnt);
}

static void check_obj_md_gone(struct dss_handle *dss, const char *oid)
{
    struct object_info *objs = NULL;
    struct dss_filter filter;
    int obj_cnt = 0;
    int rc;

    rc = dss_filter_build(&filter, "{\"DSS::OBJ::oid\": \"%s\"}", oid);
    assert_return_code(rc, -rc);

    rc = dss_object_get(dss, &filter, &objs, &obj_cnt, NULL);
    dss_filter_free(&filter);
    assert_return_code(rc, -rc);

    assert_int_equal(obj_cnt, 0);

    dss_res_free(objs, obj_cnt);
}

static void check_deprecated_obj_md(struct dss_handle *dss, const char *oid,
                                    const char *expected_user_md)
{
    struct object_info *objs = NULL;
    struct dss_filter filter;
    int obj_cnt = 0;
    int rc;

    rc = dss_filter_build(&filter, "{\"DSS::OBJ::oid\": \"%s\"}", oid);
    assert_return_code(rc, -rc);

    rc = dss_deprecated_object_get(dss, &filter, &objs, &obj_cnt, NULL);
    dss_filter_free(&filter);
    assert_return_code(rc, -rc);

    assert_int_equal(obj_cnt, 1);
    assert_string_equal(objs[0].oid, oid);
    assert_string_equal(objs[0].user_md, expected_user_md);

    dss_res_free(objs, obj_cnt);
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

    /* valid uuid/version must succeed*/
    get_xfer_and_check_res(state, 0, "oid1", "uuid1",  0);
    get_xfer_and_check_res(state, 0, "oid1", "uuid1",  1);

    /* invalid uuid/version must fail */
    check_omg_fails_with_rc(state, "oid1", "uuid1", 10, -ENOENT);
    check_omg_fails_with_rc(state, "oid1", "uuid6",  0, -ENOENT);
    check_omg_fails_with_rc(state, "oid1",    NULL,  4, -ENOENT);
}

static void omg_deprecated_object(void **void_state)
{
    struct test_state *state = (struct test_state *)*void_state;

    /* get deprecated object explicitly by uuid */
    get_xfer_and_check_res(state, 1, "oid2", "uuid2", 0);

    /* exact uuid + version must succeed*/
    get_xfer_and_check_res(state, 1, "oid2", "uuid2", 1);

    /* invalid uuid/version should fail */
    check_omg_fails_with_rc(state, "oid2", "uuid2", 2, -ENOENT);
    check_omg_fails_with_rc(state, "oid2", "uuid6", 1, -ENOENT);
}

static void omg_enoent(void **void_state)
{
    struct test_state *state = (struct test_state *)*void_state;

    /* check that the call fails with no oid */
    check_omg_fails_with_rc(state, NULL,    NULL, 0, -ENOENT);
    check_omg_fails_with_rc(state, NULL,    NULL, 1, -ENOENT);

    /* uuid-only should be allowed when oid is absent */
    get_xfer_and_check_res(state, 0, NULL, "uuid1", 0);

    /* still fail when no identifier is provided */
    check_omg_fails_with_rc(state, NULL, "uuid-missing", 0, -ENOENT);
}

/*
 * Object oids and user metadata may contain single quotes (e.g. the full
 * path of a file archived by the HSM copytool). Check that they are stored
 * and read back as-is through every object metadata write path.
 */
static void omg_quote_chars(void **void_state)
{
    struct test_state *state = (struct test_state *)*void_state;
    char *quote_md = "{\"path\": \"/mnt/d'essai/fichier\"}";
    struct object_info renamed = {0};
    struct object_info obj = {0};
    int rc;

    /* insert of a new object, as done by a put without overwrite */
    obj.oid = "oid'new";
    obj.user_md = quote_md;

    rc = dss_object_insert(state->dss, &obj, 1, DSS_SET_INSERT);
    assert_return_code(rc, -rc);

    check_obj_md(state->dss, "oid'new", quote_md);

    /* update of the user metadata, as done by setmd */
    obj.user_md = "{\"path\": \"/mnt/l'autre/fichier\"}";

    rc = dss_object_update(state->dss, &obj, &obj, 1,
                           DSS_OBJECT_UPDATE_USER_MD);
    assert_return_code(rc, -rc);

    check_obj_md(state->dss, "oid'new", obj.user_md);

    /* full insert, as done by a put with overwrite */
    obj.oid = "oid'full";
    obj.uuid = "uuid'full";
    obj.version = 1;
    obj.user_md = quote_md;

    rc = dss_object_insert(state->dss, &obj, 1, DSS_SET_FULL_INSERT);
    assert_return_code(rc, -rc);

    check_obj_md(state->dss, "oid'full", quote_md);

    /* rename, as done by the object rename command */
    renamed.oid = "oid'ren";
    renamed.uuid = "uuid'full";

    rc = dss_object_update(state->dss, &obj, &renamed, 1,
                           DSS_OBJECT_UPDATE_OID);
    assert_return_code(rc, -rc);

    check_obj_md(state->dss, "oid'ren", quote_md);

    /* delete of an object with a quoted oid */
    rc = dss_object_delete(state->dss, &renamed, 1);
    assert_return_code(rc, -rc);

    check_obj_md_gone(state->dss, "oid'ren");

    /* deprecated insert, as done by admin import */
    obj.oid = "oid'dep";
    obj.uuid = "uuid'dep";
    obj.version = 1;
    obj.user_md = quote_md;

    rc = dss_deprecated_object_insert(state->dss, &obj, 1);
    assert_return_code(rc, -rc);

    check_deprecated_obj_md(state->dss, "oid'dep", quote_md);
}

int main(void)
{
    const struct CMUnitTest object_md_save_test_cases[] = {
        cmocka_unit_test(omg_alive_object),
        cmocka_unit_test(omg_deprecated_object),
        cmocka_unit_test(omg_enoent),
        cmocka_unit_test(omg_quote_chars),
    };
    struct pho_xfer_target target = {0};

    pho_context_init();
    atexit(pho_context_fini);

    global_state.xfer.xd_targets = &target;

    return cmocka_run_group_tests(object_md_save_test_cases,
                                  omg_setup, omg_teardown);
}
