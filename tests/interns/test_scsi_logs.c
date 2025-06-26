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
 * \brief  Test SCSI logging mechanism
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <scsi/sg.h>
#include <scsi/sg_io_linux.h>
#include <scsi/scsi.h>
#include <fcntl.h>

#include "test_setup.h"
#include "pho_cfg.h"
#include "pho_common.h"
#include "pho_daemon.h"
#include "pho_dss.h"
#include "pho_layout.h"
#include "phobos_admin.h"

#include <cmocka.h>

#include "lrs_device.h"
#include "lrs_sched.h"
#include "scsi_api.h"
#include "scsi_common.h"
#include "pho_test_utils.h"

// If there is a difference in the models, you may have to modify this macro
#define LTO5_MODEL "ULT3580-TD5"

static json_t *create_log_message(enum operation_type cause,
                                  enum scsi_operation_type op, bool should_fail,
                                  char *medium_name, char *device_name,
                                  char *device_serial,
                                  struct lib_descriptor *lib)
{
    struct element_status *medium_element_status;
    struct element_status *drive_element_status;
    json_t *scsi_operation;
    json_t *scsi_execute;
    char *medium_address;
    char *device_address;
    json_t *scsi_error;

    drive_element_status = drive_element_status_from_serial(lib, device_serial);
    medium_element_status = media_element_status_from_label(lib, medium_name);
    assert_return_code(asprintf(&medium_address, "%#hx",
                                medium_element_status->address),
                       0);
    assert_return_code(asprintf(&device_address, "%#hx",
                                drive_element_status->address),
                       0);

    scsi_execute = json_object();
    assert_non_null(scsi_execute);

    if (should_fail) {
        scsi_error = json_loads(
        "{"
        "   \"asc\": 0,"
        "   \"ascq\": 0,"
        "   \"sense_key\": 5,"
        "   \"asc_ascq_str\":"
        "       \"Additional sense: No additional sense information\","
        "   \"driver_status\": 0,"
        "   \"sense_key_str\": \"Illegal Request\","
        "   \"adapter_status\": 0,"
        "   \"req_sense_error\": 0,"
        "   \"scsi_masked_status\": 1"
        "}", 0, NULL);
        assert_non_null(scsi_error);

        assert_false(json_object_set_new(scsi_execute, "SCSI ERROR",
                                         scsi_error));
    }

    assert_false(json_object_set_new(scsi_execute, "SCSI action",
                                     json_string(SCSI_ACTION_NAMES[op])));

    switch (op) {
    case LOAD_MEDIUM:
        assert_false(json_object_set_new(scsi_execute, "Arm address",
                                         json_string("0")));
        assert_false(json_object_set_new(scsi_execute, "Source address",
                                         json_string(medium_address)));
        free(medium_address);
        assert_false(json_object_set_new(scsi_execute, "Target address",
                                         json_string(device_address)));
        free(device_address);
        break;
    case UNLOAD_MEDIUM:
        assert_false(json_object_set_new(scsi_execute, "Arm address",
                                         json_string("0")));
        assert_false(json_object_set_new(scsi_execute, "Target address",
                                         json_string(medium_address)));
        free(medium_address);
        assert_false(json_object_set_new(scsi_execute, "Source address",
                                         json_string(device_address)));
        free(device_address);
        break;
    default:
        fail();
    }

    scsi_operation = json_object();
    assert_non_null(scsi_operation);

    assert_false(json_object_set_new(scsi_operation, "scsi_execute",
                                     scsi_execute));
    return scsi_operation;
}

static void check_log_is_valid(struct dss_handle *handle,
                               char *device_serial, char *medium_name,
                               enum operation_type cause,
                               enum scsi_operation_type op, bool should_fail,
                               json_t *json_message)
{
    struct pho_log *logs;
    struct pho_log log;
    int n_logs;
    int rc;

    rc = dss_logs_get(handle, NULL, &logs, &n_logs);
    assert_return_code(rc, -rc);

    assert_int_equal(n_logs, 1);

    log = logs[0];

    assert_int_equal(PHO_RSC_TAPE, log.medium.family);
    assert_int_equal(PHO_RSC_TAPE, log.device.family);
    assert_string_equal(device_serial, log.device.name);
    assert_string_equal("legacy", log.device.library);
    assert_string_equal(medium_name, log.medium.name);
    assert_string_equal("legacy", log.medium.library);
    assert_int_equal(cause, log.cause);

    if (should_fail)
        assert_int_equal(EINVAL, -log.error_number);
    else
        assert_return_code(-log.error_number, log.error_number);

    assert_true(json_equal(json_message, log.message));
    json_decref(json_message);

    dss_res_free(logs, n_logs);
}

static void get_op_params(struct sg_io_hdr *hdr, uint8_t *code, uint8_t *type)
{
    *code = *(hdr->cmdp);

    if (*code == READ_ELEMENT_STATUS) {
        struct read_status_cdb *req = (struct read_status_cdb *)hdr->cmdp;

        *type = req->element_type_code;
    } else if (*code != MODE_SENSE && *code != MOVE_MEDIUM) {
        fail();
    }
}

static bool op_to_mock(enum scsi_operation_type op_to_mock,
                       uint8_t current_element_type, uint8_t current_op)
{
    switch (op_to_mock) {
    case LIBRARY_LOAD:
        return current_op == MODE_SENSE;
    case ARMS_STATUS:
        return current_op == READ_ELEMENT_STATUS && current_element_type == 1;
    case SLOTS_STATUS:
        return current_op == READ_ELEMENT_STATUS && current_element_type == 2;
    case IMPEXP_STATUS:
        return current_op == READ_ELEMENT_STATUS && current_element_type == 3;
    case DRIVES_STATUS:
        return current_op == READ_ELEMENT_STATUS && current_element_type == 4;
    case LOAD_MEDIUM:
    case UNLOAD_MEDIUM:
        return current_op == MOVE_MEDIUM;
    default:
        fail();
        return false;
    }
}

static int mock_ioctl(int fd, unsigned long request, ...)
{
    struct scsi_req_sense *sbp;
    struct sg_io_hdr *hdr;
    int operation_to_mock;
    uint8_t type = 0;
    va_list args;
    uint8_t code;

    va_start(args, request);
    hdr = (struct sg_io_hdr *) va_arg(args, void *);
    va_end(args);

    get_op_params(hdr, &code, &type);
    operation_to_mock = mock();

    if (!op_to_mock(operation_to_mock, type, code))
        return ioctl(fd, request, hdr);

    /* This combination of masked_status and sense_key will lead to an EINVAL,
     * code 22, which is checked after the "dev_load" call.
     */
    hdr->masked_status = CHECK_CONDITION;
    sbp = (struct scsi_req_sense *)hdr->sbp;
    sbp->sense_key = SPC_SK_ILLEGAL_REQUEST;
    return 0;
}

static void scsi_dev_load_logs_check(struct dss_and_tlc_lib *dss_and_tlc_lib,
                                     enum scsi_operation_type op,
                                     bool should_fail,
                                     char *device_name, char *medium_name)
{
    struct phobos_global_context *context = phobos_context();
    json_t *full_message = NULL;
    json_t *json_message = NULL;
    char *device_serial;
    int rc;

    get_serial_from_path(device_name, &device_serial);

    /* The log must be created before the actual load because we need the
     * original address of the medium and its destination.
     */
    full_message = create_log_message(PHO_DEVICE_LOAD, op, should_fail,
                                      medium_name, device_name, device_serial,
                                      &dss_and_tlc_lib->tlc_lib);
    assert_non_null(full_message);

    if (should_fail) {
        context->mocks.mock_ioctl = &mock_ioctl;

        will_return_always(mock_ioctl, op);
    }

    rc = tlc_library_load(&dss_and_tlc_lib->dss, &dss_and_tlc_lib->tlc_lib,
                          device_serial, medium_name, &json_message);

    if (json_message) {
        json_decref(json_message);
        json_message = NULL;
    }

    if (should_fail) {
        pho_context_reset_mock_functions();
        assert_int_equal(-rc, EINVAL);
    } else {
        assert_return_code(-rc, rc);
    }

    check_log_is_valid(&dss_and_tlc_lib->dss, device_serial, medium_name,
                       PHO_DEVICE_LOAD, op, should_fail, full_message);

    if (!should_fail) {
        struct lib_item_addr unload_addr;
        char *unloaded_tape_label;

        tlc_library_unload(&dss_and_tlc_lib->dss, &dss_and_tlc_lib->tlc_lib,
                           device_serial, medium_name, &unloaded_tape_label,
                           &unload_addr, &json_message);
        if (json_message) {
            json_decref(json_message);
            json_message = NULL;
        }

        free(unloaded_tape_label);
    }

    dss_logs_delete(&dss_and_tlc_lib->dss, NULL);
    free(device_serial);
}

static void scsi_dev_load_logs_move_medium_failure(void **state)
{
    struct dss_and_tlc_lib *dss_and_tlc_lib = *state;

    /* The device and medium name here and in the following test are relevant
     * because we get to the actual load part of the dev_load function.
     */
    scsi_dev_load_logs_check(dss_and_tlc_lib, LOAD_MEDIUM, true, "/dev/st0",
                             "P00003L5");
}

static void scsi_dev_load_logs_move_medium_success(void **state)
{
    struct dss_and_tlc_lib *dss_and_tlc_lib = *state;

    scsi_dev_load_logs_check(dss_and_tlc_lib, LOAD_MEDIUM, false, "/dev/st0",
                             "P00003L5");
}

static void scsi_dev_unload_logs_check(struct dss_and_tlc_lib *dss_and_tlc_lib,
                                       enum scsi_operation_type op,
                                       bool should_fail,
                                       char *device_name, char *medium_name)
{
    struct phobos_global_context *context = phobos_context();
    struct lib_item_addr unload_addr;
    json_t *full_message = NULL;
    json_t *json_message = NULL;
    char *unloaded_tape_label;
    char *device_serial;
    int rc;

    get_serial_from_path(device_name, &device_serial);

    /* The log must be created before the actual load/unload because we need the
     * original address of the medium and its destination.
     */
    full_message = create_log_message(PHO_DEVICE_UNLOAD, op, should_fail,
                                      medium_name, device_name, device_serial,
                                      &dss_and_tlc_lib->tlc_lib);
    assert_non_null(full_message);

    rc = tlc_library_load(&dss_and_tlc_lib->dss, &dss_and_tlc_lib->tlc_lib,
                          device_serial, medium_name, &json_message);
    assert_return_code(-rc, rc);
    if (json_message) {
        json_decref(json_message);
        json_message = NULL;
    }

    dss_logs_delete(&dss_and_tlc_lib->dss, NULL);

    if (should_fail) {
        context->mocks.mock_ioctl = &mock_ioctl;

        will_return_always(mock_ioctl, op);
    }

    rc = tlc_library_unload(&dss_and_tlc_lib->dss, &dss_and_tlc_lib->tlc_lib,
                            device_serial, medium_name, &unloaded_tape_label,
                            &unload_addr, &json_message);
    if (json_message) {
        json_decref(json_message);
        json_message = NULL;
    }

    if (should_fail) {
        pho_context_reset_mock_functions();
        assert_int_equal(-rc, EINVAL);
    } else {
        assert_return_code(-rc, rc);
        assert_string_equal(unloaded_tape_label, medium_name);
        free(unloaded_tape_label);
    }


    check_log_is_valid(&dss_and_tlc_lib->dss, device_serial, medium_name,
                       PHO_DEVICE_UNLOAD, op, should_fail, full_message);

    if (should_fail) {
        tlc_library_unload(&dss_and_tlc_lib->dss, &dss_and_tlc_lib->tlc_lib,
                           device_serial, medium_name, &unloaded_tape_label,
                           &unload_addr, &json_message);
        if (json_message) {
            json_decref(json_message);
            json_message = NULL;
        }

        free(unloaded_tape_label);
    }

    dss_logs_delete(&dss_and_tlc_lib->dss, NULL);
    free(device_serial);
}

static void scsi_dev_unload_logs_move_medium_failure(void **state)
{
    struct dss_and_tlc_lib *dss_and_tlc_lib = *state;

    scsi_dev_unload_logs_check(dss_and_tlc_lib, UNLOAD_MEDIUM, true,
                               "/dev/st0", "P00003L5");
}

static void scsi_dev_unload_logs_move_medium_success(void **state)
{
    struct dss_and_tlc_lib *dss_and_tlc_lib = *state;

    scsi_dev_unload_logs_check(dss_and_tlc_lib, UNLOAD_MEDIUM, false,
                               "/dev/st0", "P00003L5");
}

int main(void)
{
    const struct CMUnitTest test_scsi_logs[] = {
        cmocka_unit_test(scsi_dev_load_logs_move_medium_failure),
        cmocka_unit_test(scsi_dev_load_logs_move_medium_success),

        cmocka_unit_test(scsi_dev_unload_logs_move_medium_failure),
        cmocka_unit_test(scsi_dev_unload_logs_move_medium_success),
    };
    int error_count;
    int rc;

    rc = access("/dev/changer", F_OK);
    if (rc == -1)
        /* Exit code for skipping the test */
        return 77;

    pho_context_init();
    rc = pho_cfg_init_local("../phobos.conf");
    if (rc)
        return rc;

    pho_log_level_set(PHO_LOG_INFO);

    error_count = cmocka_run_group_tests(
                      test_scsi_logs,
                      global_setup_dss_and_tlc_lib_with_dbinit,
                      global_teardown_dss_and_tlc_lib_with_dbdrop);

    pho_cfg_local_fini();
    pho_context_fini();

    return error_count;
}
