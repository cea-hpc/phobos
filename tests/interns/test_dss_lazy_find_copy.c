/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2026 CEA/DAM.
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
 * \brief  Tests for dss_lazy_find_copy function
 */

#include "test_setup.h"


#include "pho_dss.h"
#include "pho_dss_wrapper.h"
#include "pho_type_utils.h"

#include <assert.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <cmocka.h>

struct test_state {
    struct dss_handle *dss;
} global_state;

static int insert_state_copy(struct test_state *state,
                             char *object_uuid, int version, char *copy_name,
                             enum copy_status copy_status)
{
    struct copy_info copy;

    copy.object_uuid = object_uuid;
    copy.version = version;
    copy.copy_name = copy_name;
    copy.copy_status = copy_status;

    return dss_copy_insert(state->dss, &copy, 1, DSS_SET_INSERT);
}

static int dlfc_setup(void **state)
{
    int rc;

    *state = &global_state;

    rc = global_setup_dss_with_dbinit((void **)&global_state.dss);
    if (rc)
        return -1;

    return 0;
}

static int dlfc_teardown(void **void_state)
{
    struct test_state *state = (struct test_state *)*void_state;
    int rc;

    rc = global_teardown_dss_with_dbdrop((void **)&state->dss);
    if (rc)
        return -1;

    return 0;
}

static void dlfc_copy_name(void **void_state)
{
    struct test_state *state = (struct test_state *)*void_state;
    struct copy_info *copy_info;
    int rc;

    insert_state_copy(state, "obj_copy_name", 1, "complete",
                      PHO_COPY_STATUS_COMPLETE);
    insert_state_copy(state, "obj_copy_name", 1, "readable",
                      PHO_COPY_STATUS_READABLE);
    insert_state_copy(state, "obj_copy_name", 1, "incomplete",
                      PHO_COPY_STATUS_INCOMPLETE);

    rc = dss_lazy_find_copy(state->dss, "obj_copy_name", 1, "unknown_copy",
                            &copy_info);
    assert_int_equal(rc, -ENOENT);

    rc = dss_lazy_find_copy(state->dss, "obj_copy_name", 1, "complete",
                            &copy_info);
    assert_return_code(rc, -rc);
    assert_string_equal("complete", copy_info->copy_name);
    copy_info_free(copy_info);

    rc = dss_lazy_find_copy(state->dss, "obj_copy_name", 1, "readable",
                            &copy_info);
    assert_return_code(rc, -rc);
    assert_string_equal("readable", copy_info->copy_name);
    copy_info_free(copy_info);

    rc = dss_lazy_find_copy(state->dss, "obj_copy_name", 1, "incomplete",
                            &copy_info);
    assert_return_code(rc, -rc);
    assert_string_equal("incomplete", copy_info->copy_name);
    copy_info_free(copy_info);
}

static void dlfc_preferred_order(void **void_state)
{
    struct test_state *state = (struct test_state *)*void_state;
    struct copy_info *copy_info;
    int rc;

    insert_state_copy(state, "obj_preferred_order", 1, "other",
                      PHO_COPY_STATUS_COMPLETE);
    insert_state_copy(state, "obj_preferred_order", 1, "fast",
                      PHO_COPY_STATUS_COMPLETE);
    insert_state_copy(state, "obj_preferred_order", 1, "cache",
                      PHO_COPY_STATUS_COMPLETE);
    rc = dss_lazy_find_copy(state->dss, "obj_preferred_order", 1, NULL,
                            &copy_info);
    assert_return_code(rc, -rc);
    assert_string_equal("fast", copy_info->copy_name);
    copy_info_free(copy_info);

    insert_state_copy(state, "obj_preferred_order_fast_incomplete", 1, "fast",
                      PHO_COPY_STATUS_INCOMPLETE);
    insert_state_copy(state, "obj_preferred_order_fast_incomplete", 1, "cache",
                      PHO_COPY_STATUS_READABLE);
    insert_state_copy(state, "obj_preferred_order_fast_incomplete", 1, "other",
                      PHO_COPY_STATUS_COMPLETE);
    rc = dss_lazy_find_copy(state->dss, "obj_preferred_order_fast_incomplete",
                            1, NULL, &copy_info);
    assert_return_code(rc, -rc);
    assert_string_equal("cache", copy_info->copy_name);
    copy_info_free(copy_info);

    insert_state_copy(state, "obj_preferred_order_all_incomplete", 1, "fast",
                      PHO_COPY_STATUS_INCOMPLETE);
    insert_state_copy(state, "obj_preferred_order_all_incomplete", 1, "other",
                      PHO_COPY_STATUS_INCOMPLETE);
    rc = dss_lazy_find_copy(state->dss, "obj_preferred_order_all_incomplete",
                            1, NULL, &copy_info);
    assert_return_code(rc, -rc);
    assert_string_equal("fast", copy_info->copy_name);
    copy_info_free(copy_info);

    insert_state_copy(state, "obj_preferred_order_other_complete", 1, "fast",
                      PHO_COPY_STATUS_INCOMPLETE);
    insert_state_copy(state, "obj_preferred_order_other_complete", 1, "other",
                      PHO_COPY_STATUS_COMPLETE);
    rc = dss_lazy_find_copy(state->dss, "obj_preferred_order_other_complete",
                            1, NULL, &copy_info);
    assert_return_code(rc, -rc);
    assert_string_equal("other", copy_info->copy_name);
    copy_info_free(copy_info);

    insert_state_copy(state, "obj_preferred_order_other_readable", 1, "fast",
                      PHO_COPY_STATUS_INCOMPLETE);
    insert_state_copy(state, "obj_preferred_order_other_readable", 1, "other",
                      PHO_COPY_STATUS_READABLE);
    rc = dss_lazy_find_copy(state->dss, "obj_preferred_order_other_readable",
                            1, NULL, &copy_info);
    assert_return_code(rc, -rc);
    assert_string_equal("other", copy_info->copy_name);
    copy_info_free(copy_info);
}

static void dlfc_default(void **void_state)
{
    struct test_state *state = (struct test_state *)*void_state;
    struct copy_info *copy_info;
    int rc;

    insert_state_copy(state, "obj_default_complete", 1, "other",
                      PHO_COPY_STATUS_COMPLETE);
    insert_state_copy(state, "obj_default_complete", 1, "source",
                      PHO_COPY_STATUS_COMPLETE);
    rc = dss_lazy_find_copy(state->dss, "obj_default_complete", 1, NULL,
                            &copy_info);
    assert_return_code(rc, -rc);
    assert_string_equal("source", copy_info->copy_name);
    copy_info_free(copy_info);

    insert_state_copy(state, "obj_default_readable", 1, "other",
                      PHO_COPY_STATUS_COMPLETE);
    insert_state_copy(state, "obj_default_readable", 1, "source",
                      PHO_COPY_STATUS_READABLE);
    rc = dss_lazy_find_copy(state->dss, "obj_default_readable", 1, NULL,
                            &copy_info);
    assert_return_code(rc, -rc);
    assert_string_equal("source", copy_info->copy_name);
    copy_info_free(copy_info);

    insert_state_copy(state, "obj_default_incomplete_vs_complete", 1, "other",
                      PHO_COPY_STATUS_COMPLETE);
    insert_state_copy(state, "obj_default_incomplete_vs_complete", 1, "source",
                      PHO_COPY_STATUS_INCOMPLETE);
    rc = dss_lazy_find_copy(state->dss, "obj_default_incomplete_vs_complete",
                            1, NULL, &copy_info);
    assert_return_code(rc, -rc);
    assert_string_equal("other", copy_info->copy_name);
    copy_info_free(copy_info);

    insert_state_copy(state, "obj_default_incomplete_vs_readable", 1, "other",
                      PHO_COPY_STATUS_READABLE);
    insert_state_copy(state, "obj_default_incomplete_vs_readable", 1, "source",
                      PHO_COPY_STATUS_INCOMPLETE);
    rc = dss_lazy_find_copy(state->dss, "obj_default_incomplete_vs_readable",
                            1, NULL, &copy_info);
    assert_return_code(rc, -rc);
    assert_string_equal("other", copy_info->copy_name);
    copy_info_free(copy_info);
}

static void dlfc_other(void **void_state)
{
    struct test_state *state = (struct test_state *)*void_state;
    struct copy_info *copy_info;
    int rc;

    insert_state_copy(state, "obj_other", 1, "incomplete",
                      PHO_COPY_STATUS_INCOMPLETE);
    rc = dss_lazy_find_copy(state->dss, "obj_other", 1, NULL, &copy_info);
    assert_return_code(rc, -rc);
    assert_string_equal("incomplete", copy_info->copy_name);
    copy_info_free(copy_info);

    insert_state_copy(state, "obj_other", 1, "readable",
                      PHO_COPY_STATUS_READABLE);
    rc = dss_lazy_find_copy(state->dss, "obj_other", 1, NULL, &copy_info);
    assert_return_code(rc, -rc);
    assert_string_equal("readable", copy_info->copy_name);
    copy_info_free(copy_info);

    insert_state_copy(state, "obj_other", 1, "complete",
                      PHO_COPY_STATUS_COMPLETE);
    rc = dss_lazy_find_copy(state->dss, "obj_other", 1, NULL, &copy_info);
    assert_return_code(rc, -rc);
    assert_string_equal("complete", copy_info->copy_name);
    copy_info_free(copy_info);
}

int main(void)
{
    const struct CMUnitTest dss_lazy_find_copy_test_cases[] = {
        cmocka_unit_test(dlfc_copy_name),
        cmocka_unit_test(dlfc_preferred_order),
        cmocka_unit_test(dlfc_default),
        cmocka_unit_test(dlfc_other),
    };

    pho_context_init();
    atexit(pho_context_fini);

    return cmocka_run_group_tests(dss_lazy_find_copy_test_cases,
                                  dlfc_setup, dlfc_teardown);
}
