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

#include "pho_cfg.h"
#include "pho_common.h"
#include "pho_dss.h"
#include "pho_layout.h"

#include <cmocka.h>

#include "lrs_device.h"
#include "lrs_sched.h"
#include "io_sched.h"
#include "io_schedulers/schedulers.h"

bool running;

#define check_rc(expr)                                             \
    do {                                                           \
        int rc = (expr);                                           \
        if (rc)                                                    \
            LOG_RETURN(rc, #expr ": failed at line %d", __LINE__); \
    } while (0)

static void create_device(struct lrs_dev *dev, char *path)
{
    memset(dev, 0, sizeof(*dev));

    dev->ld_op_status = PHO_DEV_OP_ST_EMPTY;
    strcpy(dev->ld_dev_path, path);
    dev->ld_ongoing_io = false;
    dev->ld_needs_sync = false;
    dev->ld_dss_media_info = NULL;
    dev->ld_device_thread.state = THREAD_RUNNING;
    dev->ld_sys_dev_state.lds_family = PHO_RSC_TAPE;

    dev->ld_dss_dev_info = malloc(sizeof(*dev->ld_dss_dev_info));
    assert_non_null(dev->ld_dss_dev_info);
    strcpy(dev->ld_dss_dev_info->rsc.id.name, path);
    dev->ld_dss_dev_info->path = path;
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
    medium->rsc.model = "";
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

static void unload_medium(struct lrs_dev *dev)
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
    free(device.ld_dss_dev_info);
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
    free(device.ld_dss_dev_info);
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
    free(device[0].ld_dss_dev_info);
    free(device[1].ld_dss_dev_info);
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
    free(device[0].ld_dss_dev_info);
    free(device[1].ld_dss_dev_info);
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
    free(device[0].ld_dss_dev_info);
    free(device[1].ld_dss_dev_info);
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
    free(device[0].ld_dss_dev_info);
    free(device[1].ld_dss_dev_info);
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
    free(device[0].ld_dss_dev_info);
    free(device[1].ld_dss_dev_info);
}

enum io_request_type IO_REQ_TYPE;

int fetch_and_check_medium_info(struct lock_handle *lock_handle,
                                struct req_container *reqc,
                                struct pho_id *m_id,
                                size_t index,
                                struct media_info **target_medium)
{
    PhoResourceId *medium_id;

    assert_true(pho_request_is_format(reqc->req) ||
                pho_request_is_read(reqc->req));

    if (pho_request_is_format(reqc->req))
        medium_id = reqc->req->format->med_id;
    else if (pho_request_is_read(reqc->req))
        medium_id = reqc->req->ralloc->med_ids[index];
    else
        return -EINVAL;

    *target_medium = calloc(1, sizeof(**target_medium));
    assert_non_null(*target_medium);

    strcpy((*target_medium)->rsc.id.name, medium_id->name);

    return 0;
}

int tape_drive_compat_models(const char *tape_model, const char *drive_model,
                             bool *res)
{
    *res = true;

    return 0;
}

int sched_select_medium(struct io_scheduler *io_sched,
                        struct media_info **p_media,
                        size_t required_size,
                        enum rsc_family family,
                        const struct tags *tags,
                        struct req_container *reqc,
                        size_t n_med,
                        size_t not_alloc)
{
    *p_media = mock_ptr_type(struct media_info *);

    return mock();
}

static void create_request(struct req_container *reqc,
                           const char * const *media_names,
                           size_t n, size_t n_required,
                           struct lock_handle *lock_handle)
{
    int rc = 0;

    reqc->req = calloc(1, sizeof(*reqc->req));
    assert_non_null(reqc->req);

    switch (IO_REQ_TYPE) {
    case IO_REQ_WRITE: {
        struct rwalloc_params *params = &reqc->params.rwalloc;
        size_t *n_tags;
        int i;

        n_tags = calloc(n, sizeof(*n_tags));
        assert_non_null(n_tags);

        params->n_media = n;
        params->media = calloc(n, sizeof(*params->media));
        assert_non_null(params->media);

        rc = pho_srl_request_write_alloc(reqc->req, n, n_tags);
        free(n_tags);
        assert_return_code(rc, -rc);

        for (i = 0; i < n; i++)
            reqc->req->walloc->media[i]->size = 0;

        break;
    } case IO_REQ_READ: {
        struct rwalloc_params *params = &reqc->params.rwalloc;
        int i;

        rc = pho_srl_request_read_alloc(reqc->req, n);
        assert_return_code(rc, -rc);

        reqc->req->ralloc->n_required = n_required;

        params->n_media = n_required;
        params->media = calloc(n_required, sizeof(*params->media));
        assert_non_null(params->media);

        for (i = 0; i < n; i++) {
            reqc->req->ralloc->med_ids[i]->name = strdup(media_names[i]);
            reqc->req->ralloc->med_ids[i]->family = PHO_RSC_DIR;
        }
        break;
    } case IO_REQ_FORMAT: {
        struct pho_id m;

        rc = pho_srl_request_format_alloc(reqc->req);
        assert_return_code(rc, -rc);

        reqc->req->format->med_id->name = strdup(media_names[0]);
        assert_non_null(reqc->req->format->med_id->name);
        reqc->req->format->med_id->family = PHO_RSC_DIR;

        rc = fetch_and_check_medium_info(lock_handle, reqc, &m, 0,
                                         reqc_get_medium_to_alloc(reqc, 0));
        assert_return_code(rc, -rc);
        break;
    }
    }
}

static void destroy_request(struct req_container *reqc)
{
    if (pho_request_is_write(reqc->req))
        free(reqc->params.rwalloc.media);
    else if (pho_request_is_read(reqc->req))
        free(reqc->params.rwalloc.media);
    else if (pho_request_is_format(reqc->req))
        free(*reqc_get_medium_to_alloc(reqc, 0));

    pho_srl_request_free(reqc->req, false);
    free(reqc->req);
}

static void free_medium_to_alloc(struct req_container *reqc, size_t i)
{
    struct media_info **target_medium;

    if (IO_REQ_TYPE != IO_REQ_READ)
        return;

    target_medium = &reqc->params.rwalloc.media[i].alloc_medium;

    media_info_free(*target_medium);
    *target_medium = NULL;
}

static int io_sched_setup(void **data)
{
    struct io_sched_handle *io_sched;
    int rc;

    io_sched = malloc(sizeof(*io_sched));
    assert_non_null(io_sched);

    *data = io_sched;

    rc = io_sched_handle_load_from_config(io_sched, PHO_RSC_TAPE);
    assert_return_code(rc, -rc);

    return 0;
}

static int io_sched_teardown(void **data)
{
    struct io_sched_handle *io_sched = (struct io_sched_handle *) *data;

    io_sched_fini(io_sched);
    free(io_sched);

    return 0;
}

static void io_sched_add_device_twice(void **data)
{
    struct io_sched_handle *io_sched = (struct io_sched_handle *) *data;
    struct io_scheduler *handler;
    struct lrs_dev device;
    int rc;

    create_device(&device, "test");

    switch (IO_REQ_TYPE) {
    case IO_REQ_READ:
        handler = &io_sched->read;
        break;
    case IO_REQ_WRITE:
        handler = &io_sched->write;
        break;
    case IO_REQ_FORMAT:
        handler = &io_sched->format;
        break;
    default:
        return;
    }

    rc = handler->ops.add_device(handler, &device);
    assert_return_code(rc, -rc);
    assert_int_equal(handler->devices->len, 1);

    rc = handler->ops.add_device(handler, &device);
    assert_return_code(rc, -rc);
    assert_int_equal(handler->devices->len, 1);

    rc = handler->ops.remove_device(handler, &device);
    free(device.ld_dss_dev_info);
    assert_return_code(rc, -rc);
    assert_int_equal(handler->devices->len, 0);
}

static void io_sched_remove_non_existing_device(void **data)
{
    struct io_sched_handle *io_sched = (struct io_sched_handle *) *data;
    struct io_scheduler *handler;
    struct lrs_dev devices[2];
    int rc;

    create_device(&devices[0], "D1");
    create_device(&devices[1], "D2");

    switch (IO_REQ_TYPE) {
    case IO_REQ_READ:
        handler = &io_sched->read;
        break;
    case IO_REQ_WRITE:
        handler = &io_sched->write;
        break;
    case IO_REQ_FORMAT:
        handler = &io_sched->format;
        break;
    default:
        return;
    }

    rc = handler->ops.add_device(handler, &devices[0]);
    assert_return_code(rc, -rc);
    assert_int_equal(handler->devices->len, 1);

    rc = handler->ops.remove_device(handler, &devices[1]);
    assert_return_code(rc, -rc);
    assert_int_equal(handler->devices->len, 1);

    rc = handler->ops.remove_device(handler, &devices[0]);
    assert_return_code(rc, -rc);
    assert_int_equal(handler->devices->len, 0);

    free(devices[0].ld_dss_dev_info);
    free(devices[1].ld_dss_dev_info);
}

static void io_sched_no_request(void **data)
{
    struct io_sched_handle *io_sched = (struct io_sched_handle *) *data;
    struct req_container *reqc;
    int rc;

    rc = io_sched_peek_request(io_sched, &reqc);
    assert_return_code(rc, -rc);
    assert_null(reqc);
}

static void io_sched_one_request(void **data)
{
    struct io_sched_handle *io_sched = (struct io_sched_handle *) *data;
    GPtrArray *devices = g_ptr_array_new();
    struct req_container *second_reqc;
    struct req_container *first_reqc;
    static const char * const media_names[] = {
        "M1", "M2",
    };
    struct req_container reqc;
    struct lrs_dev dev;
    int rc;

    create_device(&dev, "test");
    gptr_array_from_list(devices, &dev, 1, sizeof(dev));
    create_request(&reqc, media_names, 2, 1, io_sched->lock_handle);

    rc = io_sched_push_request(io_sched, &reqc);
    assert_return_code(rc, -rc);

    rc = io_sched_dispatch_devices(io_sched, devices);
    assert_return_code(rc, -rc);

    rc = io_sched_peek_request(io_sched, &first_reqc);
    assert_return_code(rc, -rc);
    assert_ptr_equal(first_reqc, &reqc);
    free_medium_to_alloc(&reqc, 0);

    rc = io_sched_peek_request(io_sched, &second_reqc);
    assert_return_code(rc, -rc);
    assert_ptr_equal(first_reqc, second_reqc);

    rc = io_sched_remove_request(io_sched, &reqc);
    assert_return_code(rc, -rc);

    rc = io_sched_remove_device(io_sched, &dev);
    free(dev.ld_dss_dev_info);
    assert_return_code(rc, -rc);

    destroy_request(&reqc);
    g_ptr_array_free(devices, true);
}

static void io_sched_one_medium_no_device(void **data)
{
    struct io_sched_handle *io_sched = (struct io_sched_handle *) *data;
    GPtrArray *devices = g_ptr_array_new();
    struct req_container *new_reqc;
    static const char * const media_names[] = {
        "M1", "M2", "M3",
    };
    struct req_container reqc;
    struct media_info M1;
    struct lrs_dev *dev;
    size_t index;
    int rc;

    create_request(&reqc, media_names, 3, 2, io_sched->lock_handle);
    create_medium(&M1, media_names[0]);

    rc = io_sched_push_request(io_sched, &reqc);
    assert_return_code(rc, -rc);

    rc = io_sched_dispatch_devices(io_sched, devices);
    assert_return_code(rc, -rc);

    rc = io_sched_peek_request(io_sched, &new_reqc);
    assert_return_code(rc, -rc);

    if (new_reqc == NULL) {
        rc = io_sched_remove_request(io_sched, &reqc);
        destroy_request(&reqc);
        assert_return_code(rc, -rc);
        g_ptr_array_free(devices, true);
        return;
    } else {
        /* some schedulers can return a request without having devices */
        assert_ptr_equal(&reqc, new_reqc);
    }

    if (IO_REQ_TYPE == IO_REQ_WRITE) {
        will_return(sched_select_medium, &M1);
        will_return(sched_select_medium, 0);
    }

    index = 0;
    rc = io_sched_get_device_medium_pair(io_sched, &reqc, &dev, &index, false);
    free_medium_to_alloc(&reqc, 0);
    assert_return_code(rc, -rc);
    assert_null(dev);

    rc = io_sched_remove_request(io_sched, &reqc);
    assert_return_code(rc, -rc);

    destroy_request(&reqc);
    g_ptr_array_free(devices, true);
}

static void io_sched_one_medium_no_device_available(void **data)
{
    struct io_sched_handle *io_sched = (struct io_sched_handle *) *data;
    GPtrArray *device_array = g_ptr_array_new();
    static const char * const media_names[] = {
        "M1", "M2", "M3",
    };
    struct req_container *new_reqc;
    struct media_info media[2];
    struct req_container reqc;
    struct lrs_dev devices[2];
    struct lrs_dev *dev;
    size_t index;
    int rc;

    create_device(&devices[0], "D1");
    create_device(&devices[1], "D2");
    create_medium(&media[0], media_names[0]);
    create_medium(&media[1], media_names[1]);
    create_request(&reqc, media_names, 3, 2, io_sched->lock_handle);
    mount_medium(&devices[0], &media[0]);
    mount_medium(&devices[1], &media[1]);
    gptr_array_from_list(device_array, &devices, 2, sizeof(devices[0]));

    rc = io_sched_push_request(io_sched, &reqc);
    assert_return_code(rc, -rc);

    rc = io_sched_dispatch_devices(io_sched, device_array);
    assert_return_code(rc, -rc);

    rc = io_sched_peek_request(io_sched, &new_reqc);
    assert_return_code(rc, -rc);
    assert_non_null(new_reqc);

    /* device already used */
    devices[0].ld_ongoing_io = true;
    devices[1].ld_ongoing_io = true;

    if (IO_REQ_TYPE == IO_REQ_WRITE) {
        will_return(sched_select_medium, &media[0]);
        will_return(sched_select_medium, 0);
    }

    index = 0;
    rc = io_sched_get_device_medium_pair(io_sched, &reqc, &dev, &index, false);
    free_medium_to_alloc(&reqc, 0);
    assert_return_code(rc, -rc);
    assert_false(dev && dev->ld_ongoing_scheduled);

    rc = io_sched_remove_request(io_sched, &reqc);
    assert_return_code(rc, -rc);

    rc = io_sched_remove_device(io_sched, &devices[0]);
    free(devices[0].ld_dss_dev_info);
    assert_return_code(rc, -rc);

    rc = io_sched_remove_device(io_sched, &devices[1]);
    free(devices[1].ld_dss_dev_info);
    assert_return_code(rc, -rc);

    destroy_request(&reqc);
    g_ptr_array_free(device_array, true);
}

static void io_sched_one_medium(void **data)
{
    struct io_sched_handle *io_sched = (struct io_sched_handle *) *data;
    GPtrArray *devices = g_ptr_array_new();
    static const char * const media_names[] = {
        "M1",
    };
    struct req_container *new_reqc;
    struct req_container reqc;
    struct lrs_dev device;
    struct media_info M1;
    struct lrs_dev *dev;
    size_t index = 0;
    int rc;

    create_device(&device, "test");
    create_medium(&M1, media_names[0]);
    create_request(&reqc, media_names, 1, 1, io_sched->lock_handle);

    mount_medium(&device, &M1);
    gptr_array_from_list(devices, &device, 1, sizeof(device));

    rc = io_sched_push_request(io_sched, &reqc);
    assert_return_code(rc, -rc);

    rc = io_sched_dispatch_devices(io_sched, devices);
    assert_return_code(rc, -rc);

    rc = io_sched_peek_request(io_sched, &new_reqc);
    assert_return_code(rc, -rc);
    assert_ptr_equal(&reqc, new_reqc);

    rc = io_sched_get_device_medium_pair(io_sched, &reqc, &dev, &index, false);
    free_medium_to_alloc(&reqc, 0);
    assert_return_code(rc, -rc);
    assert_int_equal(index, 0);
    assert_ptr_equal(dev, &device);

    rc = io_sched_remove_request(io_sched, &reqc);
    assert_return_code(rc, -rc);

    rc = io_sched_remove_device(io_sched, &device);
    free(device.ld_dss_dev_info);
    assert_return_code(rc, -rc);

    destroy_request(&reqc);
    g_ptr_array_free(devices, true);
}

static void io_sched_4_medium(void **data)
{
    struct io_sched_handle *io_sched = (struct io_sched_handle *) *data;
    GPtrArray *device_array = g_ptr_array_new();
    static const char * const media_names[] = {
        "M1", "M2", "M3", "M4", "M5",
    };
    struct req_container *new_reqc;
    struct media_info media[4];
    struct lrs_dev devices[4];
    struct req_container reqc;
    bool index_seen[] = {
        false, false, false, false,
    };
    struct lrs_dev *dev;
    size_t index = 0;
    int rc;
    int i;

    create_device(&devices[0], "D1");
    create_device(&devices[1], "D2");
    create_device(&devices[2], "D3");
    create_device(&devices[3], "D4");

    for (i = 0; i < 4; i++)
        create_medium(&media[i], media_names[i]);

    create_request(&reqc, media_names, 4, 2, io_sched->lock_handle);

    mount_medium(&devices[0], &media[0]);
    mount_medium(&devices[1], &media[1]);
    mount_medium(&devices[2], &media[2]);
    mount_medium(&devices[3], &media[3]);
    gptr_array_from_list(device_array, &devices, 4, sizeof(*devices));

    rc = io_sched_push_request(io_sched, &reqc);
    assert_return_code(rc, -rc);

    rc = io_sched_dispatch_devices(io_sched, device_array);
    assert_return_code(rc, -rc);

    rc = io_sched_peek_request(io_sched, &new_reqc);
    assert_return_code(rc, -rc);
    assert_ptr_equal(&reqc, new_reqc);

    if (IO_REQ_TYPE == IO_REQ_WRITE) {
        will_return(sched_select_medium, &media[0]);
        will_return(sched_select_medium, 0);
    }

    index = 0;
    rc = io_sched_get_device_medium_pair(io_sched, &reqc, &dev, &index, false);
    free_medium_to_alloc(&reqc, 0);
    assert_return_code(rc, -rc);
    assert_true(index < 4);
    assert_false(index_seen[index]);
    index_seen[index] = true;
    assert_ptr_equal(dev, &devices[0]);
    dev->ld_ongoing_scheduled = true;

    if (IO_REQ_TYPE == IO_REQ_FORMAT)
        goto test_end;

    index = 1;
    rc = io_sched_get_device_medium_pair(io_sched, &reqc, &dev, &index, false);
    free_medium_to_alloc(&reqc, 1);
    assert_return_code(rc, -rc);
    assert_true(index < 4);
    assert_false(index_seen[index]);
    index_seen[index] = true;
    assert_ptr_equal(dev, &devices[1]);
    dev->ld_ongoing_scheduled = true;

    if (IO_REQ_TYPE == IO_REQ_READ)
        goto test_end;

    index = 2;
    rc = io_sched_get_device_medium_pair(io_sched, &reqc, &dev, &index, false);
    free_medium_to_alloc(&reqc, 2);
    assert_return_code(rc, -rc);
    assert_true(index < 4);
    assert_false(index_seen[index]);
    index_seen[index] = true;
    assert_ptr_equal(dev, &devices[2]);
    dev->ld_ongoing_scheduled = true;

    index = 3;
    rc = io_sched_get_device_medium_pair(io_sched, &reqc, &dev, &index, false);
    free_medium_to_alloc(&reqc, 3);
    assert_return_code(rc, -rc);
    assert_true(index < 4);
    assert_false(index_seen[index]);
    index_seen[index] = true;
    assert_ptr_equal(dev, &devices[3]);
    dev->ld_ongoing_scheduled = true;

    index = 1;
    rc = io_sched_get_device_medium_pair(io_sched, &reqc, &dev, &index, false);
    assert_return_code(rc, -rc);
    assert_null(dev);

test_end:
    rc = io_sched_remove_request(io_sched, &reqc);
    assert_return_code(rc, -rc);

    for (i = 0; i < 4; i++) {
        rc = io_sched_remove_device(io_sched, &devices[i]);
        free(devices[i].ld_dss_dev_info);
        assert_return_code(rc, -rc);
    }

    destroy_request(&reqc);
    g_ptr_array_free(device_array, true);
}

static void io_sched_not_enough_devices(void **data)
{
    struct io_sched_handle *io_sched = (struct io_sched_handle *) *data;
    static const char * const media_names[] = {
        "M1", "M2",
    };
    struct req_container *new_reqc;
    struct media_info media[2];
    struct req_container reqc;
    struct lrs_dev devices[2];
    GPtrArray *device_array;
    struct lrs_dev *dev;
    size_t index = 0;
    int rc;

    if (IO_REQ_TYPE == IO_REQ_FORMAT)
        skip();

    device_array = g_ptr_array_new();

    create_device(&devices[0], "D1");
    create_device(&devices[1], "D2");
    create_medium(&media[0], media_names[0]);
    create_medium(&media[1], media_names[1]);
    create_request(&reqc, media_names, 2, 2, io_sched->lock_handle);

    mount_medium(&devices[0], &media[0]);
    mount_medium(&devices[1], &media[1]);
    gptr_array_from_list(device_array, &devices, 2, sizeof(devices[0]));

    rc = io_sched_push_request(io_sched, &reqc);
    assert_return_code(rc, -rc);

    rc = io_sched_dispatch_devices(io_sched, device_array);
    assert_return_code(rc, -rc);

    rc = io_sched_peek_request(io_sched, &new_reqc);
    assert_return_code(rc, -rc);
    assert_ptr_equal(&reqc, new_reqc);

    if (IO_REQ_TYPE == IO_REQ_WRITE) {
        will_return(sched_select_medium, &media[0]);
        will_return(sched_select_medium, 0);
    }

    /* device 1 is busy */
    devices[1].ld_ongoing_scheduled = true;

    index = 0;
    rc = io_sched_get_device_medium_pair(io_sched, &reqc, &dev, &index, false);
    free_medium_to_alloc(&reqc, 0);
    assert_return_code(rc, -rc);
    assert_true(index < 2);
    assert_ptr_equal(dev, &devices[0]);
    dev->ld_ongoing_scheduled = true;

    if (IO_REQ_TYPE == IO_REQ_FORMAT)
        goto test_end;

    index = 1;
    rc = io_sched_get_device_medium_pair(io_sched, &reqc, &dev, &index, false);
    free_medium_to_alloc(&reqc, 1);
    assert_return_code(rc, -rc);
    /* Some I/O schedulers may return devices[1] since the medium is loaded but
     * in this case, ld_ongoing_scheduled will be true. This is interpreted by
     * the upper layers as 'the device is in use, I cannot use it'.
     */
    assert_true(!dev || dev->ld_ongoing_scheduled);

test_end:
    rc = io_sched_remove_request(io_sched, &reqc);
    assert_return_code(rc, -rc);

    rc = io_sched_remove_device(io_sched, &devices[0]);
    free(devices[0].ld_dss_dev_info);
    assert_return_code(rc, -rc);

    rc = io_sched_remove_device(io_sched, &devices[1]);
    free(devices[1].ld_dss_dev_info);
    assert_return_code(rc, -rc);

    destroy_request(&reqc);
    g_ptr_array_free(device_array, true);
}

static void io_sched_requeue_one_request(void **data)
{
    struct io_sched_handle *io_sched = (struct io_sched_handle *) *data;
    GPtrArray *devices = g_ptr_array_new();
    struct req_container *new_reqc;
    static const char * const media_names[] = {
        "M1", "M2",
    };
    struct req_container reqc;
    struct lrs_dev device;
    struct media_info M1;
    struct lrs_dev *dev;
    size_t index;
    int rc;

    create_device(&device, "test");
    gptr_array_from_list(devices, &device, 1, sizeof(device));

    create_medium(&M1, media_names[0]);
    mount_medium(&device, &M1);

    create_request(&reqc, media_names, 2, 1, io_sched->lock_handle);

    rc = io_sched_push_request(io_sched, &reqc);
    assert_return_code(rc, -rc);

    rc = io_sched_dispatch_devices(io_sched, devices);
    assert_return_code(rc, -rc);

    rc = io_sched_peek_request(io_sched, &new_reqc);
    assert_return_code(rc, -rc);
    assert_non_null(new_reqc);

    index = 0;
    rc = io_sched_get_device_medium_pair(io_sched, &reqc, &dev, &index, false);
    free_medium_to_alloc(&reqc, 0);
    assert_return_code(rc, -rc);
    assert_ptr_equal(dev, &device);
    assert_true(index < 2);

    rc = io_sched_requeue(io_sched, &reqc);
    assert_return_code(rc, -rc);

    index = 0;
    /* reset alloc medium */
    reqc.params.rwalloc.media[0].alloc_medium = NULL;
    /* the device is not scheduled */
    dev->ld_ongoing_scheduled = false;

    rc = io_sched_peek_request(io_sched, &new_reqc);
    assert_return_code(rc, -rc);
    assert_non_null(new_reqc);

    rc = io_sched_get_device_medium_pair(io_sched, &reqc, &dev, &index, false);
    free_medium_to_alloc(&reqc, 0);
    assert_return_code(rc, -rc);
    assert_ptr_equal(dev, &device);
    assert_true(index < 2);

    rc = io_sched_remove_device(io_sched, &device);
    free(device.ld_dss_dev_info);
    assert_return_code(rc, -rc);

    rc = io_sched_remove_request(io_sched, &reqc);
    assert_return_code(rc, -rc);

    destroy_request(&reqc);
    g_ptr_array_free(devices, true);
}

static void test_io_sched_error(void **data, bool free_device)
{
    struct io_sched_handle *io_sched = (struct io_sched_handle *) *data;
    static const char * const media_names[] = {
        "M1", "M2", "M3", "M4",
    };
    struct req_container *new_reqc;
    struct media_info media[4];
    struct lrs_dev devices[3];
    struct req_container reqc;
    GPtrArray *device_array;
    struct lrs_dev *dev;
    size_t index = 0;
    int rc;
    int i;

    if (IO_REQ_TYPE == IO_REQ_FORMAT)
        skip();

    device_array = g_ptr_array_new();

    create_device(&devices[0], "D1");
    create_device(&devices[1], "D2");
    create_device(&devices[2], "D3");

    for (i = 0; i < 4; i++)
        create_medium(&media[i], media_names[i]);

    create_request(&reqc, media_names, 4, 3, io_sched->lock_handle);

    mount_medium(&devices[0], &media[0]);
    mount_medium(&devices[1], &media[1]);
    mount_medium(&devices[2], &media[2]);
    gptr_array_from_list(device_array, &devices, 3, sizeof(*devices));

    rc = io_sched_push_request(io_sched, &reqc);
    assert_return_code(rc, -rc);

    rc = io_sched_dispatch_devices(io_sched, device_array);
    assert_return_code(rc, -rc);

    rc = io_sched_peek_request(io_sched, &new_reqc);
    assert_return_code(rc, -rc);
    assert_ptr_equal(&reqc, new_reqc);

    index = 0;
    rc = io_sched_get_device_medium_pair(io_sched, &reqc, &dev, &index, false);
    free_medium_to_alloc(&reqc, 0);
    assert_return_code(rc, -rc);
    assert_ptr_equal(dev, &devices[0]);
    if (IO_REQ_TYPE == IO_REQ_READ)
        assert_true(index < 4);
    else
        assert_null(reqc.params.rwalloc.media[index].alloc_medium);

    dev->ld_ongoing_scheduled = true;

    index = 1;
    rc = io_sched_get_device_medium_pair(io_sched, &reqc, &dev, &index, false);
    free_medium_to_alloc(&reqc, 1);
    assert_return_code(rc, -rc);
    assert_ptr_equal(dev, &devices[1]);
    if (IO_REQ_TYPE == IO_REQ_READ)
        assert_true(index < 4);
    else
        assert_null(reqc.params.rwalloc.media[index].alloc_medium);

    dev->ld_ongoing_scheduled = true;

    index = 2;
    rc = io_sched_get_device_medium_pair(io_sched, &reqc, &dev, &index, false);
    free_medium_to_alloc(&reqc, 2);
    assert_return_code(rc, -rc);
    assert_ptr_equal(dev, &devices[2]);
    if (IO_REQ_TYPE == IO_REQ_READ)
        assert_true(index < 4);
    else
        assert_null(reqc.params.rwalloc.media[index].alloc_medium);

    dev->ld_ongoing_scheduled = true;

    /* the request is scheduled, remove it from the scheduler */
    rc = io_sched_remove_request(io_sched, &reqc);
    assert_return_code(rc, -rc);

    /* error on M2 */
    if (IO_REQ_TYPE == IO_REQ_WRITE) {
        /* the scheduler should pick M4 */
        will_return(sched_select_medium, &media[3]);
        will_return(sched_select_medium, 0);
    }

    index = 1;
    /* device D2 will be chosen as it is free */
    if (free_device)
        devices[1].ld_ongoing_scheduled = false;
    unload_medium(&devices[1]);

    rc = io_sched_get_device_medium_pair(io_sched, &reqc, &dev, &index, true);
    free_medium_to_alloc(&reqc, 1);
    assert_return_code(rc, -rc);
    if (free_device) {
        assert_ptr_equal(dev, &devices[1]);
        if (IO_REQ_TYPE == IO_REQ_READ)
            assert_true(index < 4);
        else
            assert_ptr_equal(reqc.params.rwalloc.media[index].alloc_medium,
                             &media[3]);
    } else {
        assert_null(dev);
    }

    for (i = 0; i < 3; i++) {
        rc = io_sched_remove_device(io_sched, &devices[i]);
        free(devices[i].ld_dss_dev_info);
        assert_return_code(rc, -rc);
    }

    destroy_request(&reqc);
    g_ptr_array_free(device_array, true);
}

static void io_sched_one_error(void **data)
{
    test_io_sched_error(data, true);
}


static void io_sched_one_error_no_device_available(void **data)
{
    test_io_sched_error(data, false);
}

static void io_sched_eagain(void **data)
{
    struct io_sched_handle *io_sched = (struct io_sched_handle *) *data;
    static const char * const media_names[] = {
        "M1", "M2", "M3",
    };
    struct req_container *new_reqc;
    struct media_info media[3];
    struct req_container reqc;
    GPtrArray *device_array;
    struct lrs_dev device;
    bool index_seen[] = {
        false, false, false,
    };
    struct lrs_dev *dev;
    size_t index = 0;
    int rc;
    int i;

    if (IO_REQ_TYPE != IO_REQ_READ)
        skip();

    device_array = g_ptr_array_new();
    create_device(&device, "D1");

    for (i = 0; i < 3; i++)
        create_medium(&media[i], media_names[i]);

    create_request(&reqc, media_names, 3, 1, io_sched->lock_handle);

    gptr_array_from_list(device_array, &device, 1, sizeof(device));

    rc = io_sched_push_request(io_sched, &reqc);
    assert_return_code(rc, -rc);

    rc = io_sched_dispatch_devices(io_sched, device_array);
    assert_return_code(rc, -rc);

    rc = io_sched_peek_request(io_sched, &new_reqc);
    assert_return_code(rc, -rc);
    assert_ptr_equal(&reqc, new_reqc);

    index = 0;
    rc = io_sched_get_device_medium_pair(io_sched, &reqc, &dev, &index, false);
    assert_return_code(rc, -rc);
    assert_ptr_equal(dev, &device);
    dev->ld_ongoing_scheduled = false;
    assert_true(index < 3);
    assert_false(index_seen[index]);
    index_seen[index] = true;

    index = 0;
    rc = io_sched_get_device_medium_pair(io_sched, &reqc, &dev, &index, false);
    assert_return_code(rc, -rc);
    assert_ptr_equal(dev, &device);
    dev->ld_ongoing_scheduled = false;
    assert_true(index < 3);
    assert_false(index_seen[index]);
    index_seen[index] = true;

    index = 0;
    rc = io_sched_get_device_medium_pair(io_sched, &reqc, &dev, &index, false);
    free_medium_to_alloc(&reqc, 0);
    assert_return_code(rc, -rc);
    assert_ptr_equal(dev, &device);
    dev->ld_ongoing_scheduled = false;
    assert_true(index < 3);
    assert_false(index_seen[index]);
    index_seen[index] = true;

    index = 0;
    rc = io_sched_get_device_medium_pair(io_sched, &reqc, &dev, &index, false);
    assert_int_equal(rc, -ERANGE);

    rc = io_sched_remove_request(io_sched, &reqc);
    assert_return_code(rc, -rc);

    rc = io_sched_remove_device(io_sched, &device);
    free(device.ld_dss_dev_info);
    assert_return_code(rc, -rc);

    destroy_request(&reqc);
    g_ptr_array_free(device_array, true);
}

static int set_schedulers(const char *read_algo,
                          const char *write_algo,
                          const char *format_algo)
{
    int rc;

    rc = setenv("PHOBOS_IO_SCHED_TAPE_read_algo", read_algo, 1);
    if (rc)
        return rc;

    rc = setenv("PHOBOS_IO_SCHED_TAPE_write_algo", write_algo, 1);
    if (rc)
        return rc;

    rc = setenv("PHOBOS_IO_SCHED_TAPE_format_algo", format_algo, 1);
    if (rc)
        return rc;

    return 0;
}

static int set_fair_share_minmax(const char *_model,
                                 const char *min,
                                 const char *max)
{
    char key[64] = "PHOBOS_IO_SCHED_TAPE_fair_share_";
    char *model;
    int rc = 0;

    assert(strlen(_model) <=
           sizeof(key) - strlen("PHOBOS_IO_SCHED_TAPE_fair_share_") -
           strlen("_min"));

    model = strdup(_model);
    assert_non_null(model);
    /* pho_cfg_get_val will search for a lower case value in the environment */
    lowerstr(model);

    strcat(key, model);
    strcat(key, "_min");
    rc = setenv(key, min, 1);
    if (rc)
        goto free_model;

    strcpy(key, "PHOBOS_IO_SCHED_TAPE_fair_share_");
    strcat(key, model);
    strcat(key, "_max");
    rc = setenv(key, max, 1);
    if (rc)
        goto free_model;

free_model:
    free(model);
    return rc;
}

static char *make_name(size_t i)
{
    char *name;
    int rc;

    rc = asprintf(&name, "D%lu", i);
    assert_true(rc > 0);

    return name;
}

static GPtrArray *init_devices(GPtrArray *devices, size_t n, char *model)
{
    size_t i;

    if (!devices)
        devices = g_ptr_array_new();

    for (i = 0; i < n; i++) {
        struct lrs_dev *dev;
        char *name;

        name = make_name(i);
        assert_non_null(name);

        dev = malloc(sizeof(*dev));
        assert_non_null(dev);

        create_device(dev, name);
        dev->ld_dss_dev_info->rsc.model = model;
        free(name);

        g_ptr_array_add(devices, dev);
    }

    assert_non_null(devices);
    return devices;
}

static void cleanup_devices(GPtrArray *devices)
{
    int i;

    for (i = 0; i < devices->len; i++) {
        struct lrs_dev *dev;

        dev = g_ptr_array_index(devices, i);
        free(dev->ld_dss_dev_info);
        free(dev);
    }
    g_ptr_array_free(devices, TRUE);
}

static void io_sched_remove_all_devices(GPtrArray *devices,
                                        struct io_scheduler *io_sched)
{
    int i;

    for (i = 0; i < devices->len; i++) {
        struct lrs_dev *dev;
        int rc;

        dev = g_ptr_array_index(devices, i);
        rc = io_sched->ops.remove_device(io_sched, dev);
        assert_return_code(rc, -rc);
    }
}

#define log_test_dispatch(data, nb_devs, read_req, write_req, format_req,   \
                          read_dev, write_dev, format_dev, devices)         \
    test_dispatch(__LINE__, data, nb_devs, read_req, write_req, format_req, \
                  read_dev, write_dev, format_dev, devices)

/**
 * Simple test of repartition for the fair_share algorithm.
 *
 * \param[in]  line     line to display in logs for simpler debugging
 * \param[in]  data     pointer to a valid io_sched_handle
 * \param[in]  nb_devs  total number of devices to create and dispatch
 * \param[in]  read_req number of read requests waiting to be scheduled
 * \param[in]  read_dev expected number of devices for read after the dispatch
 * \param[in]  devices  a list of devices already allocated. If given, \p
 *                      nb_devs is ignored.
 */
static void test_dispatch(int line, void **data, size_t nb_devs,
                          size_t read_req, size_t write_req, size_t format_req,
                          size_t read_dev, size_t write_dev, size_t format_dev,
                          GPtrArray *devices)
{
    struct io_sched_handle *io_sched_hdl = (struct io_sched_handle *) *data;
    bool cleanup = false;
    int rc;

    pho_info("%s: %d", __func__, line);

    if (!devices) {
        devices = init_devices(NULL, nb_devs, "LTO5");
        cleanup = true;
    }

    io_sched_hdl->io_stats.nb_reads = read_req;
    io_sched_hdl->io_stats.nb_writes = write_req;
    io_sched_hdl->io_stats.nb_formats = format_req;

    rc = fair_share_number_of_requests(io_sched_hdl, devices);
    assert_return_code(rc, -rc);

    assert_int_equal(io_sched_hdl->read.devices->len, read_dev);
    assert_int_equal(io_sched_hdl->write.devices->len, write_dev);
    assert_int_equal(io_sched_hdl->format.devices->len, format_dev);

    io_sched_remove_all_devices(devices, &io_sched_hdl->read);
    io_sched_remove_all_devices(devices, &io_sched_hdl->write);
    io_sched_remove_all_devices(devices, &io_sched_hdl->format);
    if (cleanup)
        cleanup_devices(devices);
}

static void fair_share_repartition(void **data)
{

    /* no devices to dispatch */
    log_test_dispatch(data, 0, 17, 4, 8, 0, 0, 0, NULL);

    /* 1 device: each non empty scheduler should have one device */
    log_test_dispatch(data, 1, 0, 0, 0, 0, 0, 0, NULL);
    log_test_dispatch(data, 1, 0, 0, 1, 0, 0, 1, NULL);
    log_test_dispatch(data, 1, 0, 1, 0, 0, 1, 0, NULL);
    log_test_dispatch(data, 1, 0, 1, 1, 0, 1, 1, NULL);
    log_test_dispatch(data, 1, 1, 0, 0, 1, 0, 0, NULL);
    log_test_dispatch(data, 1, 1, 0, 1, 1, 0, 1, NULL);
    log_test_dispatch(data, 1, 1, 1, 0, 1, 1, 0, NULL);
    log_test_dispatch(data, 1, 1, 1, 1, 1, 1, 1, NULL);

    /* 2 devices: the scheduler with the most requests should have 2 */
    log_test_dispatch(data, 2, 5, 1, 1, 2, 1, 1, NULL);
    log_test_dispatch(data, 2, 1, 5, 1, 1, 2, 1, NULL);
    log_test_dispatch(data, 2, 1, 1, 5, 1, 1, 2, NULL);
    log_test_dispatch(data, 2, 5, 0, 1, 2, 0, 1, NULL);
    /* This does not work because we will give one device to read and write
     * and then add one additional device to the scheduler with the biggest
     * weight.
     * This seems like a small optimization since in practice, we probably won't
     * have the exact same repartition of requests.
     *
     * But we could also consider that a repartition of 53% and 47% is close
     * enough to 50/50 and allocate a seperate device to both schedulers.
     * This idea can be extented to more complex repartitions to prevent 1
     * request from making a device switch schedulers and on the next iteration
     * when a request is handled, make the device switch again.
     *
     * log_test_dispatch(data, 2, 1, 0, 1, 1, 0, 1, NULL);
     */

    /* check that dispatched devices match the request proportions */
    log_test_dispatch(data, 4, 2, 1, 1, 2, 1, 1, NULL);
    log_test_dispatch(data, 4, 4, 2, 2, 2, 1, 1, NULL);
    log_test_dispatch(data, 4, 6, 2, 0, 3, 1, 0, NULL);

    /* some random values (non divisible)
     * 31 requests in total                                        Δp
     * P_read   =  7 / 31 = 22.58% =>  3(.84) devices => 20.0% => -2% => +1 dev
     * P_write  = 19 / 31 = 61.29% => 10(.42) devices => 66.6% => +5% => +0 dev
     * P_format =  5 / 31 = 16.12% =>  2(.74) devices => 13.3% => -3% => +1 dev
     */
    log_test_dispatch(data, 17, 7, 19, 5, 4, 10, 3, NULL);
    log_test_dispatch(data, 7, 1, 1, 4, 1, 1, 5, NULL);
}

static void fair_share_add_device(void **data)
{
    struct io_sched_handle *io_sched_hdl = (struct io_sched_handle *) *data;
    struct lrs_dev new_device;
    GPtrArray *devices;
    int rc;

    devices = init_devices(NULL, 8, "LTO5");
    create_device(&new_device, "D8");
    new_device.ld_dss_dev_info->rsc.model = "LTO5";

    io_sched_hdl->io_stats.nb_reads = 5;
    io_sched_hdl->io_stats.nb_writes = 5;
    io_sched_hdl->io_stats.nb_formats = 10;

    rc = fair_share_number_of_requests(io_sched_hdl, devices);
    assert_return_code(rc, -rc);

    assert_int_equal(io_sched_hdl->read.devices->len, 2);
    assert_int_equal(io_sched_hdl->write.devices->len, 2);
    assert_int_equal(io_sched_hdl->format.devices->len, 4);

    g_ptr_array_add(devices, &new_device);

    rc = fair_share_number_of_requests(io_sched_hdl, devices);
    assert_return_code(rc, -rc);

    assert_int_equal(io_sched_hdl->read.devices->len, 2);
    assert_int_equal(io_sched_hdl->write.devices->len, 2);
    assert_int_equal(io_sched_hdl->format.devices->len, 5);

    io_sched_remove_all_devices(devices, &io_sched_hdl->read);
    io_sched_remove_all_devices(devices, &io_sched_hdl->write);
    io_sched_remove_all_devices(devices, &io_sched_hdl->format);

    /* the last device is on the stack, do not free it */
    free(new_device.ld_dss_dev_info);
    g_ptr_array_remove_index(devices, devices->len - 1);
    cleanup_devices(devices);
}

static void fair_share_take_devices(void **data)
{
    struct io_sched_handle *io_sched_hdl = (struct io_sched_handle *) *data;
    GPtrArray *devices;
    int rc;

    devices = init_devices(NULL, 8, "LTO5");

    io_sched_hdl->io_stats.nb_reads = 5;
    io_sched_hdl->io_stats.nb_writes = 5;
    io_sched_hdl->io_stats.nb_formats = 10;

    rc = fair_share_number_of_requests(io_sched_hdl, devices);
    assert_return_code(rc, -rc);

    assert_int_equal(io_sched_hdl->read.devices->len, 2);
    assert_int_equal(io_sched_hdl->write.devices->len, 2);
    assert_int_equal(io_sched_hdl->format.devices->len, 4);

    io_sched_hdl->io_stats.nb_reads = 10;
    io_sched_hdl->io_stats.nb_writes = 5;
    io_sched_hdl->io_stats.nb_formats = 5;

    rc = fair_share_number_of_requests(io_sched_hdl, devices);
    assert_return_code(rc, -rc);

    assert_int_equal(io_sched_hdl->read.devices->len, 4);
    assert_int_equal(io_sched_hdl->write.devices->len, 2);
    assert_int_equal(io_sched_hdl->format.devices->len, 2);

    io_sched_hdl->io_stats.nb_reads = 5;
    io_sched_hdl->io_stats.nb_writes = 1;
    io_sched_hdl->io_stats.nb_formats = 10;

    rc = fair_share_number_of_requests(io_sched_hdl, devices);
    assert_return_code(rc, -rc);

    assert_int_equal(io_sched_hdl->read.devices->len, 2);
    assert_int_equal(io_sched_hdl->write.devices->len, 1);
    assert_int_equal(io_sched_hdl->format.devices->len, 5);

    io_sched_remove_all_devices(devices, &io_sched_hdl->read);
    io_sched_remove_all_devices(devices, &io_sched_hdl->write);
    io_sched_remove_all_devices(devices, &io_sched_hdl->format);
    cleanup_devices(devices);
}

static void fair_share_multi_technologies(void **data)
{
    GPtrArray *devices;

    devices = init_devices(NULL, 8, "LTO5");
    devices = init_devices(devices, 8, "LTO6");
    devices = init_devices(devices, 8, "LTO7");

    log_test_dispatch(data, -1, 17, 4, 8, 15, 3, 6, devices);

    cleanup_devices(devices);
}

static void fair_share_ensure_min_max(void **data)
{
    GPtrArray *devices;

    set_fair_share_minmax("LTO5", "0,0,0", "0,2,2");
    log_test_dispatch(data, 2, 20, 15, 10, 0, 2, 1, NULL);
    log_test_dispatch(data, 2, 20, 10, 15, 0, 1, 2, NULL);

    devices = init_devices(NULL, 8, "LTO5");
    devices = init_devices(devices, 8, "LTO6");
    devices = init_devices(devices, 8, "LTO7");

    set_fair_share_minmax("LTO5", "0,0,0", "100,100,100");
    log_test_dispatch(data, -1, 20, 4, 12, 12, 3, 9, devices);

    set_fair_share_minmax("LTO5", "0,1,0", "0,100,0");
    set_fair_share_minmax("LTO6", "0,1,0", "0,100,0");
    set_fair_share_minmax("LTO7", "0,1,0", "0,100,0");

    log_test_dispatch(data, -1, 20, 4, 12, 0, 24, 0, devices);

    set_fair_share_minmax("LTO5", "1,1,0", "100,100,0");
    set_fair_share_minmax("LTO6", "1,1,0", "100,100,0");
    set_fair_share_minmax("LTO7", "1,1,0", "100,100,0");

    /* Since we have one scheduler that won't be able to have devices, its share
     * will be given equally to the two other schedulers. This computation is
     * done by device model. This is what it will look like for one model:
     *
     * R: 20 / 36 = 55% => 4(.44) devs
     * W:  4 / 36 = 11% => 0(.88) devs
     * F: 12 / 36 = 33% => 2(.66) devs => 0 since max = 0
     *
     * The read and write schedulers will have half of the format scheduler's
     * weight added.
     *
     * R: 26 / 36 = 72% => 5(.77) devs => 5/8 - 26/36: -9.7% => +1 dev => 6 devs
     * W: 10 / 36 = 27% => 2(.22) devs => 2/8 - 10/36: -2.7% => +0 dev => 2 devs
     * F: 0
     *
     * Since each model type has the same number of devices, the read scheduler
     * will have 3 * 6 devices and the write one will have 2 * 3.
     *
     * XXX: in this case, we distribute the weight of the format scheduler
     * equally between read and write. But since we have a repartition of 20/24
     * reads and 4/24 writes, we are giving more importance to the writes in
     * this case. We could give 20/24 * 12/36 to reads and 4/24 * 12/36 to
     * writes to respect the initial balance. Which means, giving the 2.66
     * devices that the formats should have had to each scheduler in a
     * proportion that respect their relative weights. But this approach is not
     * easy to implement in the general case (i.e. when a scheduler reaches its
     * max and this max is > 0).
     */
    log_test_dispatch(data, -1, 20, 4, 12, 18, 6, 0, devices);

    set_fair_share_minmax("LTO5", "5,1,0", "10,100,0");
    set_fair_share_minmax("LTO6", "5,1,0", "10,100,0");
    set_fair_share_minmax("LTO7", "5,1,0", "10,100,0");

    log_test_dispatch(data, -1, 0, 4, 12, 0, 24, 0, devices);

    cleanup_devices(devices);
    devices = init_devices(NULL, 8, "LTO5");

    /* R: 4(.44) devs =>  -5.5% => +0 dev => 4
     * W: 0(.88) devs => -11.1% => +1 dev => 1
     * F: 2(.66) devs =>  -8.3% => +1 dev => 3
     */
    set_fair_share_minmax("LTO5", "1,1,1", "3,1,2");
    log_test_dispatch(data, -1, 20, 4, 12, 3, 1, 2, devices);

    set_fair_share_minmax("LTO5", "0,2,4", "8,8,8");
    log_test_dispatch(data, -1, 20, 4, 12, 2, 2, 4, devices);

    /* the sum of the mins is greater than the number of available devices */
    set_fair_share_minmax("LTO5", "3,2,4", "8,8,8");
    log_test_dispatch(data, -1, 20, 4, 12, 3, 2, 3, devices);

    cleanup_devices(devices);

    /* tests with 1 device */
    set_fair_share_minmax("LTO5", "0,0,0", "0,0,0");
    log_test_dispatch(data, 1, 1, 0, 0, 0, 0, 0, NULL);
    log_test_dispatch(data, 1, 0, 1, 0, 0, 0, 0, NULL);
    log_test_dispatch(data, 1, 0, 0, 1, 0, 0, 0, NULL);

    /* tests with 2 devices */
    set_fair_share_minmax("LTO5", "0,0,0", "1,1,1");
    log_test_dispatch(data, 2, 1, 0, 0, 1, 0, 0, NULL);
    log_test_dispatch(data, 2, 0, 1, 0, 0, 1, 0, NULL);
    log_test_dispatch(data, 2, 0, 0, 1, 0, 0, 1, NULL);
    log_test_dispatch(data, 2, 5, 0, 1, 1, 0, 1, NULL);
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
    const struct CMUnitTest test_io_sched_api[] = {
        cmocka_unit_test(io_sched_add_device_twice),
        cmocka_unit_test(io_sched_remove_non_existing_device),
        cmocka_unit_test(io_sched_no_request),
        cmocka_unit_test(io_sched_one_request),
        cmocka_unit_test(io_sched_one_medium_no_device),
        cmocka_unit_test(io_sched_one_medium_no_device_available),
        cmocka_unit_test(io_sched_one_medium),
        cmocka_unit_test(io_sched_4_medium),
        cmocka_unit_test(io_sched_not_enough_devices),
        cmocka_unit_test(io_sched_requeue_one_request),
        cmocka_unit_test(io_sched_one_error),
        cmocka_unit_test(io_sched_one_error_no_device_available),
        cmocka_unit_test(io_sched_eagain),
        /* TODO test out of order medium?
         * Add med_ids_switch when necessary.
         */
        /* TODO failure on device: set status to failed */
    };
    const struct CMUnitTest test_fair_share[] = {
        cmocka_unit_test(fair_share_repartition),
        cmocka_unit_test(fair_share_add_device),
        cmocka_unit_test(fair_share_take_devices),
        cmocka_unit_test(fair_share_multi_technologies),
        cmocka_unit_test(fair_share_ensure_min_max),
    };
    int rc;

    pho_context_init();

    pho_log_level_set(PHO_LOG_INFO);
    /* TODO the initial state of the devices can be a parameter (mounted,
     * loaded, empty)
     */

    rc = cmocka_run_group_tests(test_dev_picker, NULL, NULL);

    check_rc(set_schedulers("fifo", "fifo", "fifo"));

    IO_REQ_TYPE = IO_REQ_FORMAT;
    pho_info("Starting I/O scheduler test for FORMAT requests");
    rc += cmocka_run_group_tests(test_io_sched_api,
                                 io_sched_setup,
                                 io_sched_teardown);

    IO_REQ_TYPE = IO_REQ_WRITE;
    pho_info("Starting I/O scheduler test for WRITE requests");
    rc += cmocka_run_group_tests(test_io_sched_api,
                                 io_sched_setup,
                                 io_sched_teardown);


    IO_REQ_TYPE = IO_REQ_READ;
    pho_info("Starting I/O scheduler test for READ requests");
    rc += cmocka_run_group_tests(test_io_sched_api,
                                 io_sched_setup,
                                 io_sched_teardown);

    check_rc(set_schedulers("grouped_read", "fifo", "fifo"));
    check_rc(set_fair_share_minmax("LTO5", "0,0,0", "100,100,100"));
    check_rc(set_fair_share_minmax("LTO6", "0,0,0", "100,100,100"));
    check_rc(set_fair_share_minmax("LTO7", "0,0,0", "100,100,100"));

    pho_info("Starting I/O scheduler test for READ requests with "
             "'grouped_read' scheduler");
    rc += cmocka_run_group_tests(test_io_sched_api,
                                 io_sched_setup,
                                 io_sched_teardown);

    pho_info("Starting device dispatch tests");
    set_fair_share_minmax("LTO5", "1,1,1", "100,100,100");
    rc += cmocka_run_group_tests(test_fair_share,
                                 io_sched_setup,
                                 io_sched_teardown);

    pho_context_fini();

    return rc;
}
