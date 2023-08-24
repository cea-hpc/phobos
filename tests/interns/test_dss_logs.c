/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2023 CEA/DAM.
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
 * \brief  Tests for DSS generic lock feature
 */

#include "../test_setup.h"
#include "pho_dss.h"
#include "pho_type_utils.h"

#include <assert.h>
#include <errno.h>
#include <jansson.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>

#include <cmocka.h>

static void check_log_equal(struct pho_log emitted_log,
                            struct pho_log dss_log)
{
    assert_int_equal(emitted_log.device.family, dss_log.device.family);
    assert_int_equal(emitted_log.medium.family, dss_log.medium.family);
    assert_string_equal(emitted_log.device.name, dss_log.device.name);
    assert_string_equal(emitted_log.medium.name, dss_log.medium.name);
    assert_int_equal(emitted_log.cause, dss_log.cause);
    assert_true(json_equal(emitted_log.message, dss_log.message));
}

static void dss_emit_logs_ok(void **state)
{
    struct dss_handle *handle = (struct dss_handle *)*state;
    struct pho_log log = { .error_number = 0 };
    struct pho_log *logs;
    int n_logs;
    int rc;

    log.device.family = PHO_RSC_TAPE;
    log.medium.family = PHO_RSC_TAPE;
    strcpy(log.device.name, "dummy_device");
    strcpy(log.medium.name, "dummy_medium");
    log.cause = PHO_DEVICE_LOAD;

    log.message = json_object();
    assert_non_null(log.message);

    rc = dss_emit_log(handle, &log);
    assert_return_code(rc, -rc);

    rc = dss_logs_get(handle, NULL, &logs, &n_logs);
    assert_return_code(rc, -rc);

    assert_int_equal(n_logs, 1);
    check_log_equal(log, logs[n_logs - 1]);

    json_decref(log.message);
    dss_res_free(logs, n_logs);
    dss_logs_delete(handle, NULL);
}

static void dss_emit_logs_with_message_ok(void **state)
{
    struct dss_handle *handle = (struct dss_handle *)*state;
    struct pho_log log = { .error_number = 1 };
    struct pho_log *logs;
    int n_logs;
    int rc;

    log.device.family = PHO_RSC_TAPE;
    log.medium.family = PHO_RSC_TAPE;
    strcpy(log.device.name, "dummy_device");
    strcpy(log.medium.name, "dummy_medium");
    log.cause = PHO_DEVICE_LOAD;

    log.message = json_loads("[\"foo\", {\"bar\":[\"baz\", null, 1.0, 2]}]",
                             0, NULL);
    assert_non_null(log.message);

    rc = dss_emit_log(handle, &log);
    assert_return_code(rc, -rc);

    rc = dss_logs_get(handle, NULL, &logs, &n_logs);
    assert_return_code(rc, -rc);

    assert_int_equal(n_logs, 1);
    check_log_equal(log, logs[n_logs - 1]);

    json_decref(log.message);
    dss_res_free(logs, n_logs);
    dss_logs_delete(handle, NULL);
}

int main(void)
{
    const struct CMUnitTest dss_logs_test_cases[] = {
        cmocka_unit_test(dss_emit_logs_ok),
        cmocka_unit_test(dss_emit_logs_with_message_ok),
    };

    pho_context_init();
    atexit(pho_context_fini);

    return cmocka_run_group_tests(dss_logs_test_cases,
                                  global_setup_dss_with_dbinit,
                                  global_teardown_dss_with_dbdrop);
}
