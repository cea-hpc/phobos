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
 * \brief  Implementation of C utils function
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <libgen.h>

#include "pho_test_utils.h"

#include "pho_common.h"
#include "pho_cfg.h"

#include "lrs_device.h"

void pho_run_test(const char *descr, pho_unit_test_t test, void *hint,
                  enum pho_test_result xres)
{
    int rc;

    assert(descr != NULL);
    assert(test != NULL);

    pho_info("Starting %s...", descr);

    rc = test(hint);
    if ((xres == PHO_TEST_SUCCESS) != (rc == 0)) {
        pho_error(rc == PHO_TEST_FAILURE ? 0 : rc, "%s FAILED", descr);
        exit(EXIT_FAILURE);
    }

    pho_info("%s OK", descr);
}

void test_env_initialize(void)
{
    pho_context_init();
    atexit(pho_context_fini);

    pho_cfg_init_local(NULL);
    atexit(pho_cfg_local_fini);

    if (getenv("DEBUG"))
        pho_log_level_set(PHO_LOG_DEBUG);
    else
        pho_log_level_set(PHO_LOG_VERB);
}

void get_serial_from_path(char *path, char **serial)
{
    struct dev_adapter_module *deva;
    struct ldm_dev_state lds = {};
    int rc;

    rc = get_dev_adapter(PHO_RSC_TAPE, &deva);
    assert_return_code(rc, -rc);
    rc = ldm_dev_query(deva, path, &lds);
    assert_return_code(rc, -rc);

    *serial = xstrdup(lds.lds_serial);

    free(lds.lds_serial);
    free(lds.lds_model);
}

void create_device(struct lrs_dev *dev, char *path, char *model,
                   struct dss_handle *dss)
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

    dev->ld_sys_dev_state.lds_model = NULL;
    dev->ld_sys_dev_state.lds_serial = NULL;

    dev->ld_dss_dev_info = xcalloc(1, sizeof(*dev->ld_dss_dev_info));

    dev->ld_sync_params.tosync_array = g_ptr_array_new();

    if (dss)
        dev->ld_device_thread.dss = *dss;

    dev->ld_dss_dev_info->rsc.adm_status = PHO_RSC_ADM_ST_UNLOCKED;
    dev->ld_dss_dev_info->rsc.model = model;
    dev->ld_dss_dev_info->rsc.id.family = PHO_RSC_TAPE;
    strcpy(dev->ld_dss_dev_info->rsc.id.name, path);
    dev->ld_dss_dev_info->path = path;
    rc = lrs_dev_technology(dev, &dev->ld_technology);
    assert_return_code(rc, -rc);
}

void cleanup_device(struct lrs_dev *dev)
{
    free(dev->ld_sub_request);
    free((void *)dev->ld_technology);
    free(dev->ld_dss_dev_info);
    free(dev->ld_sys_dev_state.lds_model);
    free(dev->ld_sys_dev_state.lds_serial);
    g_ptr_array_free(dev->ld_sync_params.tosync_array, true);
}

void medium_set_tags(struct media_info *medium,
                            char **tags, size_t n_tags)
{
    medium->tags.n_tags = n_tags;
    medium->tags.tags = tags;
}

void create_medium(struct media_info *medium, const char *name)
{
    memset(medium, 0, sizeof(*medium));

    medium->fs.status = PHO_FS_STATUS_BLANK;
    medium->rsc.adm_status = PHO_RSC_ADM_ST_UNLOCKED;
    medium->rsc.model = "LTO5";
    medium->rsc.id.family = PHO_RSC_TAPE;
    medium->fs.type = PHO_FS_LTFS;
    pho_id_name_set(&medium->rsc.id, name);

    medium->flags.put = true;
    medium->flags.get = true;
    medium->flags.delete = true;

    medium_set_tags(medium, NULL, 0);
}

char *get_mount_path(struct lrs_dev *dev)
{
    const char *id;
    char *mnt_root;

    id = basename(dev->ld_dev_path);
    assert_non_null(id);

    mnt_root = mount_point(id);
    assert_non_null(mnt_root);

    return mnt_root;
}
