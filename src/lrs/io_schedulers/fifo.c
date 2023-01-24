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
 * \brief  LRS FIFO I/O Scheduler
 */
#include <glib.h>
#include <pthread.h>
#include <stdbool.h>

#include "lrs_device.h"
#include "lrs_sched.h"
#include "pho_cfg.h"
#include "pho_types.h"
#include "io_sched.h"
#include "schedulers.h"

struct queue_element {
    struct req_container *reqc;
    size_t num_media_allocated;
};

static void print_elem(gpointer data, gpointer user_data)
{
    struct queue_element *elem = data;

    pho_info("%p: reqc %p, num_allocated: %lu",
             elem, elem->reqc, elem->num_media_allocated);
}

/* useful for debuging */
__attribute__((unused))
static void print_queue(GQueue *queue)
{
    g_queue_foreach(queue, print_elem, NULL);
}

static int fifo_init(struct io_scheduler *io_sched)
{
    io_sched->private_data = (void *)g_queue_new();
    if (!io_sched->private_data)
        return -ENOMEM;

    return 0;
}

static void fifo_fini(struct io_scheduler *io_sched)
{
    g_queue_free((GQueue *) io_sched->private_data);
    return;
}

static int fifo_push_request(struct io_scheduler *io_sched,
                             struct req_container *reqc)
{
    struct queue_element *elem;

    elem = malloc(sizeof(*elem));
    if (!elem)
        return -errno;

    elem->reqc = reqc;
    elem->num_media_allocated = 0;

    g_queue_push_head((GQueue *)io_sched->private_data, elem);

    return 0;
}

static bool is_reqc_the_first_element(GQueue *queue, struct req_container *reqc)
{
    struct queue_element *elem;

    elem = (struct queue_element *) g_queue_peek_tail(queue);
    if (!elem || elem->reqc != reqc)
        return false;

    return true;
}

static int fifo_remove_request(struct io_scheduler *io_sched,
                               struct req_container *reqc)
{
    struct queue_element *elem;
    GQueue *queue;

    queue = (GQueue *) io_sched->private_data;

    if (!is_reqc_the_first_element(queue, reqc))
        LOG_RETURN(-EINVAL, "element '%p' is not first, cannot remove it",
                   reqc);

    elem = g_queue_pop_tail(queue);
    free(elem);

    return 0;
}

static int fifo_requeue(struct io_scheduler *io_sched,
                        struct req_container *reqc)
{
    struct queue_element *elem;
    GQueue *queue;

    queue = (GQueue *) io_sched->private_data;
    if (!is_reqc_the_first_element(queue, reqc))
        return -EINVAL;

    elem = g_queue_pop_tail(queue);

    /* reset internal state */
    elem->num_media_allocated = 0;

    /* not FIFO but this is the current behavior */
    g_queue_push_head(queue, elem);
    return 0;
}

static int fifo_peek_request(struct io_scheduler *io_sched,
                             struct req_container **reqc)
{
    int len = g_queue_get_length(io_sched->private_data);
    struct queue_element *elem;

    pho_debug("fifo: nb requests %d", len);
    elem = g_queue_peek_tail((GQueue *) io_sched->private_data);
    if (!elem) {
        *reqc = NULL;
        return 0;
    }

    *reqc = elem->reqc;

    return 0;
}

static int find_read_device(struct io_scheduler *io_sched,
                            struct req_container *reqc,
                            struct lrs_dev **dev,
                            size_t index_in_reqc,
                            size_t index)
{
    struct media_info *medium;
    struct lrs_dev **dev_ref;
    struct pho_id medium_id;
    const char *name;
    bool sched_ready;
    int rc;

    rc = fetch_and_check_medium_info(io_sched->io_sched_hdl->lock_handle,
                                     reqc, &medium_id, index_in_reqc,
                                     reqc_get_medium_to_alloc(reqc, index));
    if (rc)
        return rc;

    /* alloc_medium should not be NULL as it was initialized by
     * fetch_and_check_medium_info
     */
    medium = reqc->params.rwalloc.media[index].alloc_medium;
    name = medium->rsc.id.name;

    dev_ref = io_sched_search_in_use_medium(io_sched, name, &sched_ready);
    if (dev_ref) {
        *dev = *dev_ref;
        return 0;
    }

    *dev = dev_picker(io_sched->devices, PHO_DEV_OP_ST_UNSPEC,
                      select_empty_loaded_mount,
                      0, &NO_TAGS, medium, false);

    return 0;
}

static int find_write_device(struct io_scheduler *io_sched,
                             struct req_container *reqc,
                             struct lrs_dev **dev,
                             size_t index,
                             bool handle_error)
{
    pho_req_write_t *wreq = reqc->req->walloc;
    struct media_info **medium =
        &reqc->params.rwalloc.media[index].alloc_medium;
    device_select_func_t dev_select_policy;
    struct lrs_dev **dev_ref;
    struct tags tags;
    bool sched_ready;
    size_t size;
    int rc;

    /* Are we retrying to find a new device to an already chosen medium? */
    if (*medium)
        goto find_device;

    dev_select_policy = get_dev_policy();
    if (!dev_select_policy)
        LOG_RETURN(-EINVAL,
                   "Unable to get device select policy during write alloc");

    tags.n_tags = wreq->media[index]->n_tags;
    tags.tags = wreq->media[index]->tags;
    size = wreq->media[index]->size;

    /* 1a) is there a mounted filesystem with enough room? */
    *dev = dev_picker(io_sched->devices, PHO_DEV_OP_ST_MOUNTED,
                      dev_select_policy,
                      size, &tags, NULL, true);
    if (*dev)
        return 0;

    /* 1b) is there a loaded media with enough room? */
    *dev = dev_picker(io_sched->devices, PHO_DEV_OP_ST_LOADED,
                      dev_select_policy,
                      size, &tags, NULL, true);
    if (*dev)
        return 0;

    /* 2) For the next steps, we need a media to write on.
     * It will be loaded into a free drive.
     * Note: sched_select_media locks the media.
     */
    pho_verb("No loaded media with enough space found: selecting another one");
    rc = sched_select_medium(io_sched, medium, size,
                             wreq->family, &tags, reqc,
                             handle_error ? wreq->n_media : index,
                             index);
    if (rc)
        return rc;

    dev_ref = io_sched_search_in_use_medium(io_sched, (*medium)->rsc.id.name,
                                            &sched_ready);
    if (dev_ref && sched_ready) {
        *dev = *dev_ref;
        return 0;
    }

find_device:
    *dev = dev_picker(io_sched->devices, PHO_DEV_OP_ST_UNSPEC,
                      select_empty_loaded_mount,
                      0, &NO_TAGS, *medium, false);
    if (*dev)
        return 0;

    *dev = NULL;

    return 0;
}

/**
 * If *dev is NULL, the caller should reschedule the request later if at least
 * one compatible device exists or abort the request otherwise.
 *
 * If \p *dev is returned by dev_picker, ld_ongoing_io will be false and the
 * caller can safely use this device. Otherwise, the caller should check if the
 * device is available for scheduling.
 */
static int find_format_device(struct io_scheduler *io_sched,
                              struct req_container *reqc,
                              struct lrs_dev **dev)
{
    const char *name = reqc->req->format->med_id->name;
    struct lrs_dev **dev_ref;
    bool sched_ready;

    dev_ref = io_sched_search_in_use_medium(io_sched, name, &sched_ready);
    if (dev_ref) {
        *dev = *dev_ref;
        return 0;
    }

    *dev = dev_picker(io_sched->devices, PHO_DEV_OP_ST_UNSPEC,
                      select_empty_loaded_mount,
                      0, &NO_TAGS, reqc->params.format.medium_to_format, false);
    if (*dev)
        /* FIXME will be removed when read, write and format are handled by
         * io_sched API
         */
        (*dev)->ld_ongoing_scheduled = false;

    return 0;
}

static int fifo_get_device_medium_pair(struct io_scheduler *io_sched,
                                       struct req_container *reqc,
                                       struct lrs_dev **device,
                                       size_t *index,
                                       bool is_error)
{
    struct queue_element *elem;
    bool is_retry = false;
    GQueue *queue;
    int rc;

    queue = (GQueue *) io_sched->private_data;

    if (pho_request_is_read(reqc->req) &&
        *reqc_get_medium_to_alloc(reqc, *index)) {
        /* This is a retry on a medium previously allocated for this request. */
        media_info_free(*reqc_get_medium_to_alloc(reqc, *index));
        *reqc_get_medium_to_alloc(reqc, *index) = NULL;
    }

    if (!is_error) {
        elem = g_queue_peek_tail(queue);
        if (!is_reqc_the_first_element(queue, reqc))
            LOG_RETURN(-EINVAL,
                       "Request '%p' is not the first element of the queue",
                       reqc);

        if (pho_request_is_read(reqc->req)) {
            if (elem->num_media_allocated >= reqc->req->ralloc->n_med_ids)
                LOG_RETURN(-ERANGE, "get_device_medium_pair called too many "
                                    "times on the same request");


            if (elem->num_media_allocated != *index)
                is_retry = true;
        }
    }

    if (pho_request_is_read(reqc->req)) {
        rc = find_read_device(io_sched, reqc, device,
                              /* select the first non-failed medium */
                              is_error ? reqc->req->ralloc->n_required :
                              is_retry ? *index :
                              elem->num_media_allocated,
                              *index);
        if (!is_error)
            /* On error, elem is NULL since the request has already been removed
             */
            *index = elem->num_media_allocated++;
        else
            /* On error, we will try to allocate the first non-failed medium
             * at index n_required
             */
            *index = reqc->req->ralloc->n_required;

    } else if (pho_request_is_write(reqc->req)) {
        rc = find_write_device(io_sched, reqc, device, *index, is_error);
    } else if (pho_request_is_format(reqc->req)) {
        rc = find_format_device(io_sched, reqc, device);
    } else {
        rc = -EINVAL;
    }

    return rc;
}

static int fifo_add_device(struct io_scheduler *io_sched,
                           struct lrs_dev *new_device)
{
    bool found = false;
    int i;

    for (i = 0; i < io_sched->devices->len; i++) {
        struct lrs_dev *dev;

        dev = g_ptr_array_index(io_sched->devices, i);
        if (new_device == dev)
            found = true;
    }

    if (!found)
        g_ptr_array_add(io_sched->devices, new_device);

    return 0;
}

static struct lrs_dev **fifo_get_device(struct io_scheduler *io_sched,
                                       size_t i)
{
    return (struct lrs_dev **)&io_sched->devices->pdata[i];
}

static int fifo_remove_device(struct io_scheduler *io_sched,
                              struct lrs_dev *device)
{
    g_ptr_array_remove(io_sched->devices, device);

    return 0;
}

/* XXX This function could choose to return a device ready for another request.
 * i.e. empty, dev_is_sched_ready == true, ...
 *
 * But whether this is efficient in practice or not will probably depend on how
 * this function is called.
 */
static int fifo_reclaim_device(struct io_scheduler *io_sched,
                               const char *techno,
                               struct lrs_dev **device)
{
    int i;

    /* The FIFO algorithm doesn't do any optimization regarding the device
     * usage. We simply give the first device of the right techno.
     */
    for (i = 0; i < io_sched->devices->len; i++) {
        struct lrs_dev *dev = g_ptr_array_index(io_sched->devices, i);

        if (strcmp(dev->ld_technology, techno))
            continue;

        *device = dev;
        g_ptr_array_remove_index_fast(io_sched->devices, i);

        return 0;
    }

    return -ENODEV;
}

struct io_scheduler_ops IO_SCHED_FIFO_OPS = {
    .init                   = fifo_init,
    .fini                   = fifo_fini,
    .push_request           = fifo_push_request,
    .remove_request         = fifo_remove_request,
    .requeue                = fifo_requeue,
    .peek_request           = fifo_peek_request,
    .get_device_medium_pair = fifo_get_device_medium_pair,
    .add_device             = fifo_add_device,
    .get_device             = fifo_get_device,
    .remove_device          = fifo_remove_device,
    .reclaim_device         = fifo_reclaim_device,
};
