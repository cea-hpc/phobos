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
 * \brief  Test TLC multi lib device
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <errno.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stddef.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <scsi/sg.h>
#include <scsi/sg_io_linux.h>
#include <scsi/scsi.h>
#include <cmocka.h>
#include <fcntl.h>

#include "scsi_api.h"
#include "scsi_common.h"
#include "test_setup.h"
#include "pho_cfg.h"
#include "pho_common.h"
#include "pho_daemon.h"
#include "pho_dss.h"
#include "pho_layout.h"
#include "phobos_admin.h"
#include "pho_test_utils.h"

static void get_op_params(struct sg_io_hdr *hdr, uint8_t *code, uint8_t *type)
{
    *code = *(hdr->cmdp);

    if (*code == READ_ELEMENT_STATUS) {
        struct read_status_cdb *req = (struct read_status_cdb *)hdr->cmdp;

        *type = req->element_type_code;
    } else if (*code != MODE_SENSE && *code != MOVE_MEDIUM &&
               *code != INQUIRY) {
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

int nb_mock_ioctl;
int max_mock;

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

    if (!op_to_mock(operation_to_mock, type, code) || nb_mock_ioctl >= max_mock)
        return ioctl(fd, request, hdr);

    /* This combination of masked_status and sense_key will lead to an EINVAL,
     * code 22, which is checked after the "dev_load" call.
     */
    hdr->masked_status = CHECK_CONDITION;
    sbp = (struct scsi_req_sense *)hdr->sbp;
    sbp->sense_key = SPC_SK_ILLEGAL_REQUEST;

    nb_mock_ioctl++;

    return 0;
}

static void tlc_load(struct dss_and_tlc_lib *dss_and_tlc_lib, bool should_fail,
                     enum scsi_operation_type op, char *device_name,
                     char *medium_name)
{
    struct phobos_global_context *context = phobos_context();
    json_t *json_message = NULL;
    char *device_serial;
    int rc;

    get_serial_from_path(device_name, &device_serial);

    context->mocks.mock_ioctl = &mock_ioctl;
    will_return_always(mock_ioctl, op);

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

    free(device_serial);
}

static void setup(struct lib_descriptor *lib, int try, int nb_expected_mock)
{
    nb_mock_ioctl = 0;
    max_mock = nb_expected_mock;
    lib->max_device_retry = try;
}

static void cleanup_lib(struct lib_descriptor *lib)
{
    lib->curr_fd_idx = 0;
    lib->max_device_retry = -1;
}

/* SCSI failed first fd && max_device_try=1 => error
 * First fd still valid and current fd should be the second
 */
static void tlc_load_dev_scsi_failed_one_try(void **state)
{
    struct dss_and_tlc_lib *dss_and_tlc_lib = *state;
    struct lib_descriptor *lib;

    lib = &dss_and_tlc_lib->tlc_lib;

    setup(lib, 1, 1);

    tlc_load(dss_and_tlc_lib, true, LOAD_MEDIUM, "/dev/st0", "P00003L5");

    assert_int_not_equal(lib->fd_array[0], -1);
    assert_int_not_equal(lib->fd_array[1], -1);
    assert_int_equal(lib->curr_fd_idx, 1);
    assert_int_equal(lib->fd, lib->fd_array[1]);

    cleanup_lib(lib);
}

/* SCSI + INQUIRY failed first fd and max_device_try=1 => error
 * First fd should be invalid and current fd should be the second
 */
static void tlc_load_dev_scsi_inquiry_failed_one_try(void **state)
{
    struct dss_and_tlc_lib *dss_and_tlc_lib = *state;
    struct lib_descriptor *lib;

    lib = &dss_and_tlc_lib->tlc_lib;

    setup(lib, 1, 1);

    /* Will failed the inquiry for the first lib device, we can close the fd
     * because we are mocking the ioctl
     */
    close(lib->fd_array[0]);
    lib->fd = lib->fd_array[0];
    tlc_load(dss_and_tlc_lib, true, LOAD_MEDIUM, "/dev/st0", "P00003L5");

    assert_int_equal(lib->fd_array[0], -1);
    assert_int_not_equal(lib->fd_array[1], -1);
    assert_int_equal(lib->curr_fd_idx, 1);
    assert_int_equal(lib->fd, lib->fd_array[1]);

    lib->fd_array[0] = open("/dev/changer", O_RDWR | O_NONBLOCK);
    lib->fd = lib->fd_array[0];
    cleanup_lib(lib);
}
/* SCSI failed first fd and max_device_try=2 => good
 * The current fd should be the second
 */
static void tlc_load_dev_scsi_failed_two_try(void **state)
{
    struct dss_and_tlc_lib *dss_and_tlc_lib = *state;
    struct lib_descriptor *lib;

    lib = &dss_and_tlc_lib->tlc_lib;

    setup(lib, 2, 1);

    tlc_load(dss_and_tlc_lib, false, LOAD_MEDIUM, "/dev/st0", "P00003L5");

    assert_int_not_equal(lib->fd_array[0], -1);
    assert_int_not_equal(lib->fd_array[1], -1);
    assert_int_equal(lib->curr_fd_idx, 1);
    assert_int_equal(lib->fd, lib->fd_array[1]);

    cleanup_lib(lib);
}

/* SCSI + INQUIRY failed first fd and max_device_try=2  => good
 * First fd should be invalid and current fd should be the second
 */
static void tlc_load_dev_scsi_inquiry_failed_two_try(void **state)
{
    struct dss_and_tlc_lib *dss_and_tlc_lib = *state;
    struct lib_descriptor *lib;

    lib = &dss_and_tlc_lib->tlc_lib;

    setup(lib, 2, 1);

    /* Will failed the inquiry for the first lib device, we can close the fd
     * because we are mocking the ioctl
     */
    close(lib->fd_array[0]);
    lib->fd = lib->fd_array[0];
    tlc_load(dss_and_tlc_lib, false, LOAD_MEDIUM, "/dev/st0", "P00003L5");

    /* Only the first lib device should be marked as failed and we should have
     * change the current lib device
     */
    assert_int_equal(lib->fd_array[0], -1);
    assert_int_not_equal(lib->fd_array[1], -1);
    assert_int_equal(lib->curr_fd_idx, 1);
    assert_int_equal(lib->fd, lib->fd_array[1]);

    lib->fd_array[0] = open("/dev/changer", O_RDWR | O_NONBLOCK);
    lib->fd = lib->fd_array[0];
    cleanup_lib(lib);
}

/* SCSI failed for all fd and max_device_try=2 => error
 * All fd are still valid and current fd should be the first
 */
static void tlc_load_dev_scsi_all_failed_two_try(void **state)
{
    struct dss_and_tlc_lib *dss_and_tlc_lib = *state;
    struct lib_descriptor *lib;

    lib = &dss_and_tlc_lib->tlc_lib;

    setup(lib, 2, 2);

    tlc_load(dss_and_tlc_lib, true, LOAD_MEDIUM, "/dev/st0", "P00003L5");

    /* Only the first lib device should be marked as failed and we should have
     * change the current lib device
     */
    assert_int_not_equal(lib->fd_array[0], -1);
    assert_int_not_equal(lib->fd_array[1], -1);
    assert_int_equal(lib->curr_fd_idx, 0);
    assert_int_equal(lib->fd, lib->fd_array[0]);

    cleanup_lib(lib);
}


/* SCSI + INQUIRY failed for all fd mand max_device_try=2 => error
 * All fd are invalid and curr_fd_idx should be -1
 */
static void tlc_load_dev_scsi_inquiry_all_failed_two_try(void **state)
{
    struct dss_and_tlc_lib *dss_and_tlc_lib = *state;
    struct lib_descriptor *lib;

    lib = &dss_and_tlc_lib->tlc_lib;

    setup(lib, 2, 2);

    /* Will failed the inquiry for the first lib device, we can close the fd
     * because we are mocking the ioctl
     */
    close(lib->fd_array[0]);
    lib->fd = lib->fd_array[0];
    close(lib->fd_array[1]);

    tlc_load(dss_and_tlc_lib, true, LOAD_MEDIUM, "/dev/st0", "P00003L5");

    /* Only the first lib device should be marked as failed and we should have
     * change the current lib device
     */
    assert_int_equal(lib->fd_array[0], -1);
    assert_int_equal(lib->fd_array[1], -1);
    assert_int_equal(lib->curr_fd_idx, -1);

    lib->fd_array[0] = open("/dev/changer", O_RDWR | O_NONBLOCK);
    lib->fd = lib->fd_array[0];
    lib->fd_array[1] = open("/dev/changer2", O_RDWR | O_NONBLOCK);

    cleanup_lib(lib);
}

int main(void)
{
    const struct CMUnitTest test_tlc_multi_lib_device[] = {
        cmocka_unit_test(tlc_load_dev_scsi_failed_one_try),
        cmocka_unit_test(tlc_load_dev_scsi_inquiry_failed_one_try),
        cmocka_unit_test(tlc_load_dev_scsi_failed_two_try),
        cmocka_unit_test(tlc_load_dev_scsi_inquiry_failed_two_try),
        cmocka_unit_test(tlc_load_dev_scsi_all_failed_two_try),
        cmocka_unit_test(tlc_load_dev_scsi_inquiry_all_failed_two_try),
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

    rc = symlink("/dev/changer", "/dev/changer2");
    if (rc)
        return rc;

    rc = setenv("PHOBOS_TLC_LEGACY_lib_device",
                "/dev/changer,/dev/changer2", 1);
    if (rc)
        return rc;

    error_count = cmocka_run_group_tests(
                      test_tlc_multi_lib_device,
                      global_setup_dss_and_tlc_lib_with_dbinit,
                      global_teardown_dss_and_tlc_lib_with_dbdrop);

    pho_cfg_local_fini();
    pho_context_fini();

    rc = unlink("/dev/changer2");
    if (rc)
        return rc;

    rc = unsetenv("PHOBOS_TLC_LEGACY_lib_device");
    if (rc)
        return rc;

    return error_count;
}
