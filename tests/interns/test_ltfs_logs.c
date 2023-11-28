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
#include <libgen.h>
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

#include "../test_setup.h"
#include "pho_cfg.h"
#include "pho_common.h"
#include "pho_daemon.h"
#include "pho_dss.h"
#include "pho_layout.h"
#include "phobos_admin.h"

#include <cmocka.h>

#include "lrs_device.h"
#include "lrs_sched.h"
#include "pho_test_utils.h"

// If there is a difference in the models, you may have to modify this macro
#define LTO5_MODEL "ULT3580-TD5"
#define DEVICE_NAME "/dev/st1"
#define MEDIUM_NAME "P00004L5"

static void check_log_is_valid(struct dss_handle *handle,
                               char *device_name, char *medium_name,
                               enum operation_type cause,
                               int error_number,
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
    assert_string_equal(device_name, log.device.name);
    assert_string_equal(medium_name, log.medium.name);
    assert_int_equal(cause, log.cause);

    assert_int_equal(-log.error_number, error_number);

    if (!json_equal(json_message, log.message)) {
        pho_error(-EINVAL,
                  "Retrieved message '%s' differ from expected log "
                  "message '%s'",
                  json_dumps(json_message, 0), json_dumps(log.message, 0));

        fail();
    }

    destroy_json(json_message);

    dss_res_free(logs, n_logs);
}

static int fail_mkdir(const char *path, mode_t mode)
{
    (void) path;
    (void) mode;

    errno = EPERM;
    return 1;
}

static void prepare_mount(struct dss_handle *handle, struct lrs_dev *device,
                          struct media_info *medium)
{
    struct fs_adapter_module *fsa = NULL;
    bool fod;
    bool fom;
    bool cr;
    int rc;

    rc = get_fs_adapter(PHO_FS_LTFS, &fsa);
    assert_return_code(-rc, rc);

    create_device(device, DEVICE_NAME, LTO5_MODEL, handle);
    create_medium(medium, MEDIUM_NAME);

    rc = dev_load(device, &medium, true, &fod, &fom, &cr, false);
    assert_return_code(-rc, rc);

    dss_logs_delete(handle, NULL);
    assert_ptr_equal(device->ld_dss_media_info, medium);

    setenv("PHOBOS_LTFS_cmd_format",
           "../../scripts/pho_ldm_helper format_ltfs \"%s\" \"%s\"", 0);

    rc = dev_format(device, fsa, true);
    assert_return_code(-rc, rc);
}

static void ltfs_mount_mkdir_failure(void **state)
{
    struct phobos_global_context *context = phobos_context();
    struct media_info *medium = xcalloc(1, sizeof(*medium));
    struct dss_handle *handle = *state;
    struct lrs_dev device;
    char *mount_path;
    json_t *message;
    int rc;

    prepare_mount(handle, &device, medium);

    context->mock_ltfs.mock_mkdir = fail_mkdir;

    rc = dev_mount(&device);
    assert_int_equal(rc, -EPERM);

    mount_path = get_mount_path(&device);

    message = json_pack("{s:s+}", "mkdir",
                        "Failed to create mount point: ", mount_path);

    check_log_is_valid(handle, DEVICE_NAME, MEDIUM_NAME, PHO_LTFS_MOUNT,
                       EPERM, message);

    free(mount_path);
    dev_unload(&device);
    dss_logs_delete(handle, NULL);
    cleanup_device(&device);
}

int main(void)
{
    const struct CMUnitTest test_ltfs_logs[] = {
        cmocka_unit_test(ltfs_mount_mkdir_failure),
    };
    int error_count;
    int rc;

    rc = access("/dev/changer", F_OK);
    if (rc)
        /* Exit code for skipping the test */
        return 77;

    pho_context_init();
    rc = pho_cfg_init_local("../phobos.conf");
    if (rc)
        return rc;

    pho_log_level_set(PHO_LOG_ERROR);

    error_count = cmocka_run_group_tests(test_ltfs_logs,
                                         global_setup_dss_with_dbinit,
                                         global_teardown_dss_with_dbdrop);

    pho_cfg_local_fini();
    pho_context_fini();

    return error_count;
}
