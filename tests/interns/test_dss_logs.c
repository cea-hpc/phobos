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
#include <time.h>

#include <cmocka.h>

#define LENGTH_DEVICES 2
static struct pho_id devices[LENGTH_DEVICES] = {
    { .family = PHO_RSC_TAPE, .name = "deviceA" },
    { .family = PHO_RSC_TAPE, .name = "deviceB" },
};

#define LENGTH_MEDIA 2
static struct pho_id media[LENGTH_MEDIA] = {
    { .family = PHO_RSC_TAPE, .name = "mediumA" },
    { .family = PHO_RSC_TAPE, .name = "mediumB" },
};

#define LENGTH_TYPES 2
static enum operation_type types[LENGTH_TYPES] = {
    PHO_LIBRARY_SCAN,
    PHO_DEVICE_UNLOAD,
};

#define LENGTH_TIMES LENGTH_DEVICES

static void generate_log(struct dss_handle *handle, int index_dev,
                         int index_med, int index_type)
{
    struct pho_log log;
    int rc;

    init_pho_log(&log, &devices[index_dev], &media[index_med],
                 types[index_type]);
    log.message = json_object();

    rc = dss_emit_log(handle, &log);
    assert_return_code(rc, -rc);
    json_decref(log.message);
}

static void generate_logs(struct dss_handle *handle,
                          struct timeval times[LENGTH_TIMES])
{
    int i, j, k;

    for (i = 0; i < LENGTH_DEVICES; ++i) {
        for (j = 0; j < LENGTH_MEDIA; ++j)
            for (k = 0; k < LENGTH_TYPES; ++k)
                generate_log(handle, i, j, k);

        times[i].tv_sec = (unsigned long) time(NULL) + 1;
        times[i].tv_usec = 0;
        if (i < LENGTH_DEVICES - 1)
            sleep(2);
    }
}

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

static void check_logs_with_filter(struct dss_handle *handle,
                                   struct pho_id *device,
                                   struct pho_id *medium,
                                   enum operation_type *type,
                                   struct timeval *start,
                                   struct timeval *end,
                                   int expected_log_number,
                                   bool action_is_clear)
{
    struct pho_log_filter filter = { .error_number = NULL };
    struct dss_filter *filter_ptr;
    struct dss_filter log_filter;
    struct pho_log *logs;
    int n_logs;
    int rc;

    if (device) {
        filter.device.family = device->family;
        strcpy(filter.device.name, device->name);
    } else {
        filter.device.family = PHO_RSC_NONE;
    }

    if (medium) {
        filter.medium.family = medium->family;
        strcpy(filter.medium.name, medium->name);
    } else {
        filter.medium.family = PHO_RSC_NONE;
    }

    if (type)
        filter.cause = *type;
    else
        filter.cause = PHO_OPERATION_INVALID;

    if (start)
        filter.start = *start;
    else
        filter.start.tv_sec = 0;

    if (end)
        filter.end = *end;
    else
        filter.end.tv_sec = 0;

    filter_ptr = &log_filter;
    rc = create_logs_filter(&filter, &filter_ptr);
    assert_return_code(rc, -rc);

    if (action_is_clear) {
        rc = dss_logs_delete(handle, filter_ptr);
        assert_return_code(rc, -rc);

        rc = dss_logs_get(handle, NULL, &logs, &n_logs);
    } else {
        rc = dss_logs_get(handle, filter_ptr, &logs, &n_logs);
    }

    dss_filter_free(filter_ptr);
    assert_return_code(rc, -rc);
    assert_int_equal(n_logs, expected_log_number);
    dss_res_free(logs, n_logs);
}

static void check_logs_by_dump(struct dss_handle *handle,
                               struct pho_id *device,
                               struct pho_id *medium,
                               enum operation_type *type,
                               struct timeval *start,
                               struct timeval *end,
                               int expected_log_number)
{
    check_logs_with_filter(handle, device, medium, type, start, end,
                           expected_log_number, false);
}

static void dss_logs_dump_with_filters(void **state)
{
    struct dss_handle *handle = (struct dss_handle *)*state;
    int max_log_number = LENGTH_DEVICES * LENGTH_MEDIA *
                         LENGTH_TYPES;
    int expected_log_number = max_log_number;
    struct timeval times[LENGTH_TIMES];

    generate_logs(handle, times);

    check_logs_by_dump(handle, NULL, NULL, NULL, NULL, NULL,
                       expected_log_number);

    expected_log_number = max_log_number / (LENGTH_DEVICES * LENGTH_MEDIA *
                                            LENGTH_TYPES);
    check_logs_by_dump(handle, &devices[0], &media[0], &types[0], NULL, NULL,
                       expected_log_number);

    expected_log_number = max_log_number / (LENGTH_DEVICES * LENGTH_TYPES);
    check_logs_by_dump(handle, &devices[1], NULL, &types[1], &times[0], NULL,
                       expected_log_number);

    expected_log_number = max_log_number / (LENGTH_DEVICES);
    check_logs_by_dump(handle, &devices[1], NULL, NULL, &times[0], &times[1],
                       expected_log_number);

    expected_log_number = max_log_number / (LENGTH_DEVICES * LENGTH_MEDIA *
                                            LENGTH_TYPES);
    check_logs_by_dump(handle, &devices[1], &media[1], &types[0], NULL,
                       &times[1], expected_log_number);

    dss_logs_delete(handle, NULL);
}

static void check_logs_by_clear(struct dss_handle *handle,
                                struct pho_id *device,
                                struct pho_id *medium,
                                enum operation_type *type,
                                struct timeval *start,
                                struct timeval *end,
                                int expected_log_number)
{
    check_logs_with_filter(handle, device, medium, type, start, end,
                           expected_log_number, true);
}

static void dss_logs_clear_with_filters(void **state)
{
    struct dss_handle *handle = (struct dss_handle *)*state;
    struct timeval times[LENGTH_TIMES];

    generate_logs(handle, times);

    check_logs_by_clear(handle, NULL, NULL, NULL, NULL, NULL, 0);

    generate_logs(handle, times);

    /* Only one log should be removed for device[0], media[0] and type[0] */
    check_logs_by_clear(handle, &devices[0], &media[0], &types[0], NULL, NULL,
                        7);

    /* All logs before time[1] should be removed, which amount to the 7 left */
    check_logs_by_clear(handle, NULL, NULL, NULL, NULL, &times[1], 0);

    generate_logs(handle, times);

    check_logs_by_clear(handle, &devices[1], NULL, &types[1], &times[0], NULL,
                        6);

    /* All logs of device[1] and type[1] have been cleared, so the ones left
     * are those of device[0] and device[1]/type[0], which amount to 6 logs.
     * Clearing those of device[0] should remove 4 logs, leaving only two
     */
    check_logs_by_clear(handle, &devices[0], NULL, NULL, NULL, NULL, 2);

    /* And clearing those with type[0] should remove all logs */
    check_logs_by_clear(handle, NULL, NULL, &types[0], NULL, NULL, 0);

    generate_logs(handle, times);

    check_logs_by_clear(handle, &devices[1], NULL, NULL, &times[0], &times[1],
                        4);

    /* 4 logs after left after clearing those device[1], all of those concern
     * device[0]
     */

    /* We clear the one about media[1] and type[0], leaving 3 behind */
    check_logs_by_clear(handle, NULL, &media[1], &types[0], NULL, &times[1], 3);

    /* Now clear the one with media[0], which should leave 1 log remaining */
    check_logs_by_clear(handle, NULL, &media[0], NULL, NULL, &times[1], 1);

    /* Clearing the logs with type[1] but after time[0] should remove no log */
    check_logs_by_clear(handle, NULL, NULL, &types[1], &times[0], NULL, 1);

    /* Finally, clearing the logs before time[1] should remove the last log */
    check_logs_by_clear(handle, NULL, NULL, NULL, NULL, &times[1], 0);
}

int main(void)
{
    const struct CMUnitTest dss_logs_test_cases[] = {
        cmocka_unit_test(dss_emit_logs_ok),
        cmocka_unit_test(dss_emit_logs_with_message_ok),
        cmocka_unit_test(dss_logs_dump_with_filters),
        cmocka_unit_test(dss_logs_clear_with_filters),
    };

    pho_context_init();
    atexit(pho_context_fini);

    return cmocka_run_group_tests(dss_logs_test_cases,
                                  global_setup_dss_with_dbinit,
                                  global_teardown_dss_with_dbdrop);
}
