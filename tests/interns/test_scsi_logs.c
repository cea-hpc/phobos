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

#include "../test_setup.h"
#include "pho_cfg.h"
#include "pho_common.h"
#include "pho_dss.h"
#include "pho_layout.h"

#include <cmocka.h>

#include "lrs_device.h"
#include "scsi_api.h"

#define LTO5_MODEL "ULTRIUM-TD5"
#define LTO6_MODEL "ULTRIUM-TD6"
#define LTO7_MODEL "ULTRIUM-TD7"

static void create_device(struct lrs_dev *dev, char *path, char *model)
{
    int rc;

    memset(dev, 0, sizeof(*dev));

    dev->ld_op_status = PHO_DEV_OP_ST_EMPTY;
    strcpy(dev->ld_dev_path, path);
    dev->ld_ongoing_io = false;
    dev->ld_needs_sync = false;
    dev->ld_dss_media_info = NULL;
    dev->ld_device_thread.state = THREAD_RUNNING;
    dev->ld_sys_dev_state.lds_family = PHO_RSC_TAPE;

    dev->ld_dss_dev_info = calloc(1, sizeof(*dev->ld_dss_dev_info));
    dev->ld_dss_dev_info->rsc.adm_status = PHO_RSC_ADM_ST_UNLOCKED;
    assert_non_null(dev->ld_dss_dev_info);
    dev->ld_dss_dev_info->rsc.model = model;
    dev->ld_dss_dev_info->rsc.id.family = PHO_RSC_TAPE;
    strcpy(dev->ld_dss_dev_info->rsc.id.name, path);
    dev->ld_dss_dev_info->path = path;
    rc = lrs_dev_technology(dev, &dev->ld_technology);
    assert_return_code(rc, -rc);
}

static void cleanup_device(struct lrs_dev *dev)
{
    free((void *)dev->ld_technology);
    free(dev->ld_dss_dev_info);
}

static void medium_set_tags(struct media_info *medium,
                            char **tags, size_t n_tags)
{
    medium->tags.n_tags = n_tags;
    medium->tags.tags = tags;
}

static void create_medium(struct media_info *medium, const char *name)
{
    memset(medium, 0, sizeof(*medium));

    medium->fs.status = PHO_FS_STATUS_BLANK;
    medium->rsc.adm_status = PHO_RSC_ADM_ST_UNLOCKED;
    medium->rsc.model = NULL;
    medium->rsc.id.family = PHO_RSC_TAPE;
    strcpy(medium->rsc.id.name, name);

    medium->flags.put = true;
    medium->flags.get = true;
    medium->flags.delete = true;

    medium_set_tags(medium, NULL, 0);
}

static int mock_ioctl(int fd, unsigned long request, void *sg_io_hdr)
{
    struct sg_io_hdr *hdr = (struct sg_io_hdr *)sg_io_hdr;
    struct scsi_req_sense *sbp;

    /* Retrieve a mock value that will indicate which ioctl we want to mock, as
     * certain operations (for instance "dev_load") will do multiple ioctl, and
     * we may not want to mock every one.
     */
    if (mock())
        return ioctl(fd, request, hdr);

    /* This combination of masked_status and sense_key will lead to an EINVAL,
     * code 22, which is checked after the "dev_load" call.
     */
    hdr->masked_status = CHECK_CONDITION;
    sbp = (struct scsi_req_sense *)hdr->sbp;
    sbp->sense_key = SPC_SK_ILLEGAL_REQUEST;
    return 0;
}

static void scsi_logs_mode_sense_failure(void **state)
{
    struct phobos_global_context *context = phobos_context();
    struct dss_handle *handle = (struct dss_handle *)*state;
    /* The calloc here is necessary because if the dev_fails, it will free the
     * given medium.
     */
    struct media_info *medium = calloc(1, sizeof(*medium));
    struct lrs_dev device;
    bool fod;
    bool fom;
    bool cr;
    int rc;

    context->mock_ioctl = &mock_ioctl;

    create_device(&device, "test", LTO5_MODEL);
    device.ld_device_thread.dss = *handle;

    create_medium(medium, "test");

    will_return(mock_ioctl, 1);
    will_return(mock_ioctl, 0);

    rc = dev_load(&device, &medium, true, &fod, &fom, &cr, true);
    assert_int_equal(-rc, 22);

    cleanup_device(&device);
}

int main(void)
{
    const struct CMUnitTest test_scsi_logs[] = {
        cmocka_unit_test(scsi_logs_mode_sense_failure),
    };
    struct stat dev_changer;
    int error_count;
    int rc;

    rc = stat("/dev/changer", &dev_changer);
    if (rc)
        /* Exit code for skipping the test */
        return 77;

    pho_context_init();
    rc = pho_cfg_init_local("../phobos.conf");
    if (rc)
        return rc;

    pho_log_level_set(PHO_LOG_ERROR);

    error_count = cmocka_run_group_tests(test_scsi_logs, global_setup_dss,
                                         global_teardown_dss);

    pho_cfg_local_fini();
    pho_context_fini();

    return error_count;
}
