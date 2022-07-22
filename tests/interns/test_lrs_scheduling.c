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
 * \brief  Test LRS request scheduling
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

#include "pho_common.h"
#include "pho_dss.h"
#include "pho_layout.h"

#include <cmocka.h>

#include "lrs_device.h"
#include "lrs_sched.h"

bool running;

#define check_rc(expr)                                             \
    do {                                                           \
        int rc = (expr);                                           \
        if (rc)                                                    \
            LOG_RETURN(rc, #expr ": failed at line %d", __LINE__); \
    } while (0)

static void create_device(struct lrs_dev *dev, const char *path)
{
    memset(dev, 0, sizeof(*dev));

    dev->ld_op_status = PHO_DEV_OP_ST_EMPTY;
    strcpy(dev->ld_dev_path, path);
    dev->ld_ongoing_io = false;
    dev->ld_needs_sync = false;
    dev->ld_dss_media_info = NULL;
    dev->ld_device_thread.state = THREAD_RUNNING;
}

static void medium_set_size(struct media_info *medium, ssize_t size)
{
    medium->stats.phys_spc_free = size;
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
    strcpy(medium->rsc.id.name, name);

    medium->flags.put = true;
    medium->flags.get = true;
    medium->flags.delete = true;

    medium_set_tags(medium, NULL, 0);
}

static void load_medium(struct lrs_dev *dev, struct media_info *medium)
{
    dev->ld_op_status = PHO_DEV_OP_ST_LOADED;
    dev->ld_dss_media_info = medium;
}

static void mount_medium(struct lrs_dev *dev, struct media_info *medium)
{
    dev->ld_op_status = PHO_DEV_OP_ST_MOUNTED;
    dev->ld_dss_media_info = medium;
}

__attribute__((unused))
static void unmount_medium(struct lrs_dev *dev, struct media_info *medium)
{
    dev->ld_op_status = PHO_DEV_OP_ST_LOADED;
    dev->ld_dss_media_info = medium;
}

__attribute__((unused))
static void unload_medium(struct lrs_dev *dev, struct media_info *medium)
{
    dev->ld_op_status = PHO_DEV_OP_ST_EMPTY;
    dev->ld_dss_media_info = NULL;
}

static void gptr_array_from_list(GPtrArray *array,
                                 void *data, guint len,
                                 size_t elem_size)
{
    guint i;

    for (i = 0; i < len; i++)
        g_ptr_array_add(array, (char *)data + elem_size * i);
}

static void dev_picker_no_device(void **data)
{
    GPtrArray *devices = g_ptr_array_new();
    struct lrs_dev *dev;

    dev = dev_picker(devices, PHO_DEV_OP_ST_UNSPEC, select_empty_loaded_mount,
                     0, &NO_TAGS, NULL, false);
    assert_null(dev);

    g_ptr_array_free(devices, true);
}

static void dev_picker_one_available_device(void **data)
{
    GPtrArray *devices = g_ptr_array_new();
    struct lrs_dev device;
    struct lrs_dev *dev;

    create_device(&device, "test");
    gptr_array_from_list(devices, &device, 1, sizeof(device));

    dev = dev_picker(devices, PHO_DEV_OP_ST_UNSPEC, select_empty_loaded_mount,
                     0, &NO_TAGS, NULL, false);
    assert_non_null(dev);
    assert_ptr_equal(dev, &device);

    g_ptr_array_free(devices, true);
}

static void dev_picker_one_booked_device(void **data)
{
    GPtrArray *devices = g_ptr_array_new();
    struct lrs_dev device;
    struct lrs_dev *dev;

    create_device(&device, "test");
    gptr_array_from_list(devices, &device, 1, sizeof(device));

    device.ld_ongoing_io = true;

    dev = dev_picker(devices, PHO_DEV_OP_ST_UNSPEC, select_empty_loaded_mount,
                     0, &NO_TAGS, NULL, false);
    assert_null(dev);

    g_ptr_array_free(devices, true);
}

static void dev_picker_one_booked_device_one_available(void **data)
{
    GPtrArray *devices = g_ptr_array_new();
    struct lrs_dev device[2];
    struct lrs_dev *dev;

    create_device(&device[0], "test1");
    create_device(&device[1], "test2");

    gptr_array_from_list(devices, &device, 2, sizeof(device[0]));

    device[0].ld_ongoing_io = true;

    dev = dev_picker(devices, PHO_DEV_OP_ST_UNSPEC, select_empty_loaded_mount,
                     0, &NO_TAGS, NULL, false);
    assert_non_null(dev);
    assert_string_equal(dev->ld_dev_path, "test2");

    dev->ld_ongoing_scheduled = true;
    dev = dev_picker(devices, PHO_DEV_OP_ST_UNSPEC, select_empty_loaded_mount,
                     0, &NO_TAGS, NULL, false);
    assert_null(dev);

    g_ptr_array_free(devices, true);
}

static void dev_picker_search_mounted(void **data)
{
    GPtrArray *devices = g_ptr_array_new();
    struct media_info medium;
    struct lrs_dev device[2];
    struct lrs_dev *dev;

    create_device(&device[0], "test1");
    create_device(&device[1], "test2");

    gptr_array_from_list(devices, &device, 2, sizeof(device[0]));

    dev = dev_picker(devices, PHO_DEV_OP_ST_MOUNTED, select_empty_loaded_mount,
                     0, &NO_TAGS, NULL, false);
    assert_null(dev);

    create_medium(&medium, "test");
    mount_medium(&device[1], &medium);

    device[0].ld_ongoing_io = true;

    dev = dev_picker(devices, PHO_DEV_OP_ST_MOUNTED, select_empty_loaded_mount,
                     0, &NO_TAGS, NULL, false);
    assert_non_null(dev);
    assert_string_equal(dev->ld_dev_path, "test2");

    device[0].ld_ongoing_io = false;
    dev->ld_ongoing_scheduled = true;
    dev = dev_picker(devices, PHO_DEV_OP_ST_MOUNTED, select_empty_loaded_mount,
                     0, &NO_TAGS, NULL, false);
    assert_null(dev);

    g_ptr_array_free(devices, true);
}

static void dev_picker_search_loaded(void **data)
{
    GPtrArray *devices = g_ptr_array_new();
    struct media_info medium;
    struct lrs_dev device[2];
    struct lrs_dev *dev;

    create_device(&device[0], "test1");
    create_device(&device[1], "test2");

    gptr_array_from_list(devices, &device, 2, sizeof(device[0]));

    dev = dev_picker(devices, PHO_DEV_OP_ST_LOADED, select_empty_loaded_mount,
                     0, &NO_TAGS, NULL, false);
    assert_null(dev);

    create_medium(&medium, "test");
    mount_medium(&device[1], &medium);

    device[0].ld_ongoing_io = true;

    dev = dev_picker(devices, PHO_DEV_OP_ST_LOADED, select_empty_loaded_mount,
                     0, &NO_TAGS, NULL, false);
    assert_null(dev);

    load_medium(&device[0], &medium);

    dev = dev_picker(devices, PHO_DEV_OP_ST_LOADED, select_empty_loaded_mount,
                     0, &NO_TAGS, NULL, false);
    assert_null(dev);

    device[0].ld_ongoing_io = false;

    dev = dev_picker(devices, PHO_DEV_OP_ST_LOADED, select_empty_loaded_mount,
                     0, &NO_TAGS, NULL, false);
    assert_non_null(dev);
    assert_string_equal(dev->ld_dev_path, "test1");

    g_ptr_array_free(devices, true);
}

static void dev_picker_available_space(void **data)
{
    GPtrArray *devices = g_ptr_array_new();
    struct media_info medium[2];
    struct lrs_dev device[2];
    struct lrs_dev *dev;

    create_device(&device[0], "test1");
    create_device(&device[1], "test2");

    create_medium(&medium[0], "test1");
    create_medium(&medium[1], "test2");

    mount_medium(&device[0], &medium[0]);
    mount_medium(&device[1], &medium[1]);

    medium_set_size(&medium[0], 0);
    medium_set_size(&medium[1], 100);

    gptr_array_from_list(devices, &device, 2, sizeof(device[0]));

    dev = dev_picker(devices, PHO_DEV_OP_ST_MOUNTED, select_first_fit,
                     200, &NO_TAGS, NULL, true);
    assert_null(dev);

    medium_set_size(&medium[0], 300);

    dev = dev_picker(devices, PHO_DEV_OP_ST_MOUNTED, select_first_fit,
                     200, &NO_TAGS, NULL, true);
    assert_non_null(dev);
    assert_string_equal(dev->ld_dev_path, "test1");

    dev->ld_ongoing_scheduled = true;
    dev = dev_picker(devices, PHO_DEV_OP_ST_MOUNTED, select_first_fit,
                     200, &NO_TAGS, NULL, true);
    assert_null(dev);

    g_ptr_array_free(devices, true);
}

static void dev_picker_flags(void **data)
{
    GPtrArray *devices = g_ptr_array_new();
    struct media_info medium[2];
    struct lrs_dev device[2];
    struct lrs_dev *dev;

    create_device(&device[0], "test1");
    create_device(&device[1], "test2");

    create_medium(&medium[0], "test1");
    create_medium(&medium[1], "test2");

    mount_medium(&device[0], &medium[0]);
    mount_medium(&device[1], &medium[1]);

    gptr_array_from_list(devices, &device, 2, sizeof(device[0]));

    device[0].ld_ongoing_io = true;
    device[1].ld_dss_media_info->flags.put = false;
    dev = dev_picker(devices, PHO_DEV_OP_ST_MOUNTED, select_first_fit,
                     0, &NO_TAGS, NULL, true);
    assert_null(dev);

    device[1].ld_dss_media_info->flags.put = true;
    device[1].ld_dss_media_info->fs.status = PHO_FS_STATUS_FULL;
    dev = dev_picker(devices, PHO_DEV_OP_ST_MOUNTED, select_first_fit,
                     0, &NO_TAGS, NULL, true);
    assert_null(dev);

    g_ptr_array_free(devices, true);
}

int main(void)
{
    const struct CMUnitTest test_dev_picker[] = {
        cmocka_unit_test(dev_picker_no_device),
        cmocka_unit_test(dev_picker_one_available_device),
        cmocka_unit_test(dev_picker_one_booked_device),
        cmocka_unit_test(dev_picker_one_booked_device_one_available),
        cmocka_unit_test(dev_picker_search_mounted),
        cmocka_unit_test(dev_picker_search_loaded),
        cmocka_unit_test(dev_picker_available_space),
        cmocka_unit_test(dev_picker_flags),
    };

    return cmocka_run_group_tests(test_dev_picker, NULL, NULL);
}
