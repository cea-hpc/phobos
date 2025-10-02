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
 * \brief  LRS Grouped Read I/O Scheduler: group read request per medium.
 */
#include "io_sched.h"
#include "lrs_sched.h"
#include "lrs_utils.h"
#include "pho_cfg.h"
#include "pho_common.h"
#include "pho_types.h"
#include "schedulers.h"

/* Principle of the algorithm:
 *
 * This algorithm will try to associate queues of requests which target the same
 * medium to devices. Each device will have a queue associated to it until it is
 * emptied.
 *
 * On push, we look at each media that can be used for this read. For each of
 * these media, we push the request into its corresponding queue. If the queue
 * doesn't exist, we create it and search if the medium is already in a device.
 * If so, the queue is immediately associated to the device.
 *
 * On peek_request, we look for the first device whose first request in it's
 * associated queue can be allocate (i.e. there is enough free devices that can
 * handle the request). If we can't find any device with an associated queue, we
 * try to allocate a new queue and return the first request.
 *
 * For example:
 *
 * Free queues:                Devices:
 *   M1: r1, r2                  D1: M5: r3, r4
 *   M2: r1, r2                  D2: X
 *   M3: r3, r4                  D3: X
 *   M4: r3, r5
 *
 * On get_device_medium_pair, we will try to see if r3 can be allocated since it
 * is the first request we will find. If r3 requires 3 medium or less, it can be
 * allocated and will be returned.
 *
 * If it cannot be allocated, we will search in the free queues
 * (grouped_data::request_queues) and pick M1 for example. We can then allocate
 * it to a device, D2 for example and return the first element r1.
 *
 * On remove_request, the request is removed from all the queues it belongs to.
 * If any of these queues are empty, it is removed from its associated device
 * and freed.
 */

struct request_queue;

struct list_pair {
    GList *used;                 /* list of queue_element previously used */
    GList *free;                 /* list of unused queue_element */
};

struct queue_element {
    struct req_container *reqc;  /* reference to the corresponding req_container
                                  */
    struct request_queue *queue; /* queue this element belongs to */
    struct list_pair     *pair;  /* pointer to a pair of lists shared between
                                  * each queue_element of the same request.
                                  */
};

struct device;

struct request_queue {
    GQueue            *queue;  /* queue containing read queue_element */
    struct device     *device; /* device which will handle requests from this
                                * queue
                                */
    struct pho_id      medium_id; /* Id of the medium targeted by requests of
                                   * this queue
                                   */
    struct media_info *medium_info;
                           /* DSS information about the medium of this queue.
                            * This acts as a cached information since it is
                            * fetched when the queue is first created.
                            * It is copied into rwalloc_params::media in
                            * grouped_get_device_medium_pair.
                            */
};

struct device {
    struct lrs_dev       *device;
    struct request_queue *queue;
};

static void associate_queue_to_device(struct device *device,
                                      struct request_queue *queue)
{
    device->queue = queue;
    queue->device = device;
}

static void remove_queue_from_device(struct device *device,
                                     struct request_queue *queue)
{
    device->queue = NULL;
    if (queue->device)
        queue->device = NULL;
}

struct grouped_data {
    GHashTable *request_queues; /* hashtable containing pointers to struct
                                 * request_queue. Key is the medium_id
                                 */
    struct queue_element *current_elem;
};

/* Iterate over all the element in the GList \p list. \p var is used as the
 * name of the current element in the iteration.
 */
#define glist_foreach(var, list) \
    for (GList *var = list; var; var = var->next)

static ssize_t reqc_get_medium_index_from_medium_id(struct req_container *reqc,
                                                    struct pho_id *medium_id)
{
    int i;

    for (i = 0; i < reqc->req->ralloc->n_med_ids; i++) {
        struct pho_id req_medium_id;

        reqc_pho_id_from_index(reqc, i, &req_medium_id);
        if (pho_id_equal(&req_medium_id, medium_id))
            return i;
    }

    return -1;
}

static int grouped_init(struct io_scheduler *io_sched)
{
    struct grouped_data *data;
    int rc;

    data = xmalloc(sizeof(*data));

    data->request_queues = g_hash_table_new(g_pho_id_hash, g_pho_id_equal);
    if (!data->request_queues)
        GOTO(free_data, rc = -ENOMEM);

    io_sched->private_data = data;

    return 0;

free_data:
    free(data);

    return rc;
}

static void grouped_fini(struct io_scheduler *io_sched)
{
    struct grouped_data *data = io_sched->private_data;

    g_hash_table_destroy(data->request_queues);
    free(data);
}

static struct device *
find_compatible_device(GPtrArray *devices, struct media_info *medium,
                       bool *compatible_device_found)
{
    int i;

    *compatible_device_found = false;

    for (i = 0; i < devices->len; i++) {
        struct device *dev;
        bool is_compatible;

        dev = g_ptr_array_index(devices, i);

        if (tape_drive_compat(medium, dev->device, &is_compatible))
            continue;

        if (!is_compatible)
            continue;

        *compatible_device_found = true;

        if (!dev_is_sched_ready(dev->device))
            continue;

        if (!dev->queue)
            return dev;
    }

    return NULL;
}

struct find_compatible_context {
    struct device      *device;  /* result of the search (can be NULL) */
    GPtrArray          *devices; /* list of devices owned by this scheduler */
    GPtrArray          *incompatible_queues; /* List of struct request_queue
                                              * that cannot be allocated since
                                              * there aren't any compatible
                                              * devices.
                                              */
    size_t              available_devices; /* Number of devices without an
                                            * associated queue. A queue cannot
                                            * be allocated if this number is
                                            * lower than the number of required
                                            * media ralloc->n_required.
                                            */
    struct io_scheduler *io_sched;
};

static int exchange_device(struct io_scheduler *io_sched,
                           enum io_request_type type,
                           struct lrs_dev *device_to_exchange);

static struct device *find_device_from_lrs_dev(struct io_scheduler *io_sched,
                                               struct lrs_dev *dev);

/* This function is called on each entry of the table
 * grouped_data::request_queues. It will stop at the first queue which has a
 * device compatible with the queue and available for scheduling. If any queue
 * that cannot be allocated (i.e. no device compatible with the medium) are
 * found, they are stored in find_compatible_context::incompatible_queues and
 * removed later since one cannot remove an entry during g_hash_table_foreach.
 *
 * ctxt::device will be set to the device found for the current queue if any.
 * Once ctxt::device is not NULL, the search is stopped.
 */
/* TODO we can add a parameter which will tell the function to search for the
 * queue with the most requests that can be allocated now.
 */
static gboolean glib_stop_at_first_compatible(gpointer _queue_name,
                                              gpointer _queue,
                                              gpointer _compat_ctxt)
{
    struct find_compatible_context *ctxt = _compat_ctxt;
    struct request_queue *queue = _queue;
    struct lrs_dev *dev_with_medium;
    bool compatible_device_found;
    struct queue_element *elem;
    bool sched_ready;

    (void) _queue_name;

    if (queue->device)
        /* we are looking for a new queue to allocate to a device */
        return FALSE;

    dev_with_medium = search_in_use_medium(
        ctxt->io_sched->io_sched_hdl->global_device_list,
        queue->medium_id.name, queue->medium_id.library, &sched_ready);
    if (dev_with_medium && !sched_ready)
        return FALSE;

    if (dev_with_medium) {
        ctxt->device = find_device_from_lrs_dev(ctxt->io_sched,
                                                dev_with_medium);
        if (!ctxt->device) {
            int rc = exchange_device(ctxt->io_sched, IO_REQ_READ,
                                     dev_with_medium);
            if (rc)
                return FALSE;

            if (!(dev_with_medium->ld_io_request_type & IO_REQ_READ))
                return FALSE;

            ctxt->device = find_device_from_lrs_dev(ctxt->io_sched,
                                                    dev_with_medium);

            /* we have just exchanged the device, we must own it. */
            assert(ctxt->device);
            return TRUE;
        }

        /* dev_with_medium is loaded and owned by this I/O scheduler, return
         * it.
         */
        return TRUE;
    }

    elem = g_queue_peek_tail(queue->queue);
    /* Once a queue is empty, it is removed so this should not happen */
    assert(elem);

    ctxt->device = find_compatible_device(ctxt->devices, queue->medium_info,
                                          &compatible_device_found);
    if (!compatible_device_found)
        /* we cannot remove during g_hash_table_foreach, so save it for later */
        g_ptr_array_add(ctxt->incompatible_queues, queue);

    if (elem->reqc->req->ralloc->n_required > ctxt->available_devices)
        return FALSE;

    return ctxt->device != NULL;
}

static int
request_queue_alloc(struct io_scheduler *io_sched,
                    struct queue_element *elem,
                    size_t index,
                    struct request_queue **queue)
{
    struct grouped_data *data = io_sched->private_data;
    int rc;

    *queue = xmalloc(sizeof(**queue));

    rc = fetch_and_check_medium_info(io_sched->io_sched_hdl->lock_handle,
                                     elem->reqc, &(*queue)->medium_id, index,
                                     &(*queue)->medium_info);
    if (rc)
        GOTO(free_g_queue, rc);

    (*queue)->device = NULL;
    (*queue)->queue = g_queue_new();

    g_hash_table_insert(data->request_queues, &(*queue)->medium_id, *queue);

    return 0;

free_g_queue:
    free(*queue);

    return rc;
}

static void delete_queue(struct grouped_data *data,
                         struct request_queue *queue)
{
    g_hash_table_remove(data->request_queues, &queue->medium_id);

    if (queue->device)
        remove_queue_from_device(queue->device, queue);

    lrs_medium_release(queue->medium_info);
    g_queue_free(queue->queue);
    free(queue);
}

static void queue_element_free(struct queue_element *elem, bool last)
{
    if (last) {
        /* this is the last element, free both lists */
        g_list_free(elem->pair->free);
        g_list_free(elem->pair->used);
        free(elem->pair);
    }
    free(elem);
}

static void remove_element_from_queue(struct grouped_data *data,
                                      struct queue_element *elem)
{
    g_queue_remove(elem->queue->queue, elem);
    if (g_queue_get_length(elem->queue->queue) == 0)
        delete_queue(data, elem->queue);
}

/* Delete every element in \p list except \p to_ignore. Each removed element is
 * also removed from its associated queue.
 *
 * \param[in/out]  data       global data of this scheduler
 * \param[in]      list       list of struct queue_element
 * \param[in]      to_ignore  element to keep in the list
 */
static void delete_elements_in_list(struct grouped_data *data, GList *list,
                                    struct queue_element *to_ignore)
{
    glist_foreach(iter, list) {
        struct queue_element *e;

        e = iter->data;
        if (e == to_ignore)
            continue;

        remove_element_from_queue(data, e);
        queue_element_free(e, false);
    }
}

static void cancel_request(struct io_scheduler *io_sched,
                           struct queue_element *elem)
{
    struct grouped_data *data = io_sched->private_data;

    /* we send a ENODEV error since this function is called when there are not
     * enough devices to handle a request.
     */
    queue_error_response(io_sched->io_sched_hdl->response_queue, -ENODEV,
                         elem->reqc);

    /* remove each element from the its queue */
    delete_elements_in_list(data, elem->pair->used, elem);
    delete_elements_in_list(data, elem->pair->free, elem);

    queue_element_free(elem, true);
    io_sched->io_sched_hdl->io_stats.nb_reads--;
}

/* After a search through all the queues, we can identify which queues cannot be
 * allocated. If the scheduler doesn't have a compatible device for this queue,
 * the request cannot be allocated.
 *
 * Each of those queues will be removed. Each queue_element will be removed from
 * its associated pair->used or pair->free list. Finally, if the request
 * elem->reqc cannot be allocated (i.e. the number of elements of the request is
 * lower than ralloc->n_required), the request will be canceled, an error
 * -ENODEV will be sent to the client and each queue_element associated to reqc
 *  will be removed from its queue.
 */
static void empty_incompatible_queue(struct io_scheduler *io_sched,
                                     struct request_queue *queue)
{
    struct queue_element *elem;

    pho_warn("No device compatible with (family '%s', name '%s', library '%s) "
             "can be found",
             rsc_family2str(queue->medium_id.family), queue->medium_id.name,
             queue->medium_id.library);

    while ((elem = g_queue_pop_tail(queue->queue)) != NULL) {
        size_t num_elements;

        /* remove it from both lists, it will be in only one of them */
        elem->pair->used = g_list_remove(elem->pair->used, elem);
        elem->pair->free = g_list_remove(elem->pair->free, elem);

        num_elements = g_list_length(elem->pair->used) +
            g_list_length(elem->pair->free);

        if (elem->reqc->req->ralloc->n_required > num_elements)
            cancel_request(io_sched, elem);
        else
            queue_element_free(elem, num_elements == 1);
    }

    delete_queue(io_sched->private_data, queue);
}

static struct request_queue *
find_and_allocate_queue(struct io_scheduler *io_sched,
                        size_t available_devices)
{
    struct grouped_data *data = io_sched->private_data;
    struct find_compatible_context ctxt = {
        .device              = NULL,
        .devices             = io_sched->devices,
        .incompatible_queues = g_ptr_array_new(),
        .available_devices   = available_devices,
        .io_sched            = io_sched,
    };
    struct request_queue *queue = NULL;
    int i;

    queue = g_hash_table_find(data->request_queues,
                              glib_stop_at_first_compatible,
                              &ctxt);
    if (queue) {
        assert(ctxt.device);

        associate_queue_to_device(ctxt.device, queue);
    }

    for (i = 0; i < ctxt.incompatible_queues->len; i++) {
        struct request_queue *queue;

        queue = g_ptr_array_index(ctxt.incompatible_queues, i);
        empty_incompatible_queue(io_sched, queue);
    }

    g_ptr_array_free(ctxt.incompatible_queues, TRUE);

    return queue;
}

/* count the number of devices that are ready for scheduling and that don't
 * already have a queue associated.
 */
static size_t count_available_devices(GPtrArray *devices)
{
    size_t count = 0;
    int i;

    for (i = 0; i < devices->len; i++) {
        struct device *device;

        device = g_ptr_array_index(devices, i);

        if (!device->queue && dev_is_sched_ready(device->device))
            count++;
    }

    return count;
}

/* find a device that can be allocated now */
static struct lrs_dev *find_free_device(GPtrArray *devices)
{
    int i;

    for (i = 0; i < devices->len; i++) {
        struct device *d;

        d = g_ptr_array_index(devices, i);
        if (dev_is_sched_ready(d->device) && !d->queue)
            return d->device;
    }

    return NULL;
}

static int exchange_device(struct io_scheduler *io_sched,
                           enum io_request_type type,
                           struct lrs_dev *device_to_exchange)
{
    union io_sched_claim_device_args args;
    struct lrs_dev *free_device;

    free_device = find_free_device(io_sched->devices);
    if (!free_device)
        /* No free device to give back, cannot schedule this request yet. */
        return 0;

    args.exchange.desired_device = device_to_exchange;
    args.exchange.unused_device = free_device;

    return io_sched_claim_device(io_sched, IO_SCHED_EXCHANGE, &args);
}

static struct device *find_device(struct io_scheduler *io_sched,
                                  struct lrs_dev *dev)
{
    int i;

    for (i = 0; i < io_sched->devices->len; i++) {
        struct device *d;

        d = g_ptr_array_index(io_sched->devices, i);

        if (d->device == dev)
            return d;
    }

    return NULL;
}

static int try_exchange_extra_devices(struct io_scheduler *io_sched,
                                      struct lrs_dev **extra_devices,
                                      size_t len)
{
    struct grouped_data *data = io_sched->private_data;
    size_t i;

    for (i = 0; i < len; i++) {
        int rc;

        rc = exchange_device(io_sched, IO_REQ_READ, extra_devices[i]);
        if (rc) {
            pho_error(rc, "Failed to exchange devices");
            return rc;
        }

        if (extra_devices[i]->ld_io_request_type & IO_REQ_READ) {
            struct media_info *medium;
            struct device *d;

            /* do not increment available_devices since we exchange a free
             * device with a device that we can use.
             */
            d = find_device(io_sched, extra_devices[i]);
            medium = atomic_dev_medium_get(d->device);
            if (!medium)
                /* Race with device thread, the medium was probably just
                 * unloaded. Just ignore this device.
                 */
                continue;

            d->queue = g_hash_table_lookup(data->request_queues,
                                           &medium->rsc.id);
            d->queue->device = d;
            lrs_medium_release(medium);
        }
    }

    return 0;
}

/**
 * Return true if there are enough available devices to handle \p reqc.
 *
 * \param[in] available_devices  Number of devices that are ready for scheduling
 *                               and don't have a queue associated. It is given
 *                               as a parameter to avoid recomputing it for each
 *                               request.
 *
 *  Note: available_devices is the number of devices that are "sched_ready" and
 *  don't have a queue associated to them. There is no guaranty that they are
 *  all compatible with the media of the request. This is not an issue since
 *  get_device_medium_pair will check for compatibility.
 */
static bool request_can_be_allocated(struct io_scheduler *io_sched,
                                     struct grouped_data *data,
                                     struct req_container *reqc,
                                     size_t available_devices)
{
    size_t n_required = reqc->req->ralloc->n_required;
    struct lrs_dev **extra_devices;
    struct lrs_dev **dev_iter;
    bool res = false;
    int rc;
    int i;

    if (available_devices >= n_required)
        return true;

    extra_devices = xmalloc(sizeof(*extra_devices) * n_required);
    dev_iter = extra_devices;

    for (i = 0; i < reqc->req->ralloc->n_med_ids; i++) {
        struct request_queue *queue;
        struct pho_id medium_id;

        reqc_pho_id_from_index(reqc, i, &medium_id);
        queue = g_hash_table_lookup(data->request_queues, &medium_id);
        assert(queue);

        if (queue->device &&
            dev_is_sched_ready(queue->device->device)) {
            available_devices++;

            if (available_devices >= n_required)
                GOTO(free_list, res = true);
        }

        if (!queue->device) {
            struct lrs_dev *dev;
            bool sched_ready;

            /* Search if someone else has the device. */
            dev = search_in_use_medium(
                io_sched->io_sched_hdl->global_device_list,
                queue->medium_id.name, queue->medium_id.library, &sched_ready);
            if (dev && sched_ready &&
                !(dev->ld_io_request_type & IO_REQ_READ)) {
                *dev_iter++ = dev;
            }
        }
    }

    rc = try_exchange_extra_devices(io_sched, extra_devices,
                                    dev_iter - extra_devices);
    if (rc) {
        pho_error(rc, "Failed to exchanged devices");
        res = false;
    }

free_list:
    free(extra_devices);

    return res;
}

static int grouped_peek_request(struct io_scheduler *io_sched,
                                struct req_container **reqc)
{
    size_t available_devices = count_available_devices(io_sched->devices);
    struct grouped_data *data = io_sched->private_data;
    int i;

    *reqc = NULL;

    /* search for a device containing a queue whose first request can be
     * allocated
     */
    for (i = 0; i < io_sched->devices->len; i++) {
        struct queue_element *elem;
        struct media_info *medium;
        struct device *device;

        device = g_ptr_array_index(io_sched->devices, i);
        medium = atomic_dev_medium_get(device->device);
        if (medium && !device->queue) {
            struct request_queue *queue;

            /* If a device moves from one scheduler to another, it can contain
             * a medium that was not found when the request was first pushed.
             */
            queue = g_hash_table_lookup(data->request_queues, &medium->rsc.id);
            if (queue)
                /* queue can be NULL if, for example, a medium is already loaded
                 * when the LRS starts. If no request for this medium has been
                 * pushed yet, the medium is in device->device but no queue
                 * exists.
                 */
                associate_queue_to_device(device, queue);
        }
        lrs_medium_release(medium);

        if (!dev_is_sched_ready(device->device) || !device->queue)
            continue;

        elem = g_queue_peek_tail(device->queue->queue);
        /* Only grouped_get_device_medium_pair can add elements to
         * elem->pair->used. Once the caller has finished using
         * grouped_get_device_medium_pair, the request should either be requeued
         * and elem->pair->used is emptied or remove_request is called and the
         * request is removed from the queues. We should never find an element
         * with elem->pair->used not empty in this function.
         */
        assert(g_list_length(elem->pair->used) == 0);

        if (request_can_be_allocated(io_sched, data, elem->reqc,
                                     available_devices)) {
            *reqc = elem->reqc;
            data->current_elem = elem;

            return 0;
        }
    }

    /* If we are here, we didn't find any queue to use. If there are available
     * devices, try to find another queue to allocate.
     */
    if (available_devices > 0) {
        /* no request allocated but some devices don't have a queue yet */
        struct request_queue *queue;
        struct queue_element *elem;

        /* no device has a queue associated to it, find a new one */
        queue = find_and_allocate_queue(io_sched, available_devices);
        if (!queue)
            /* no more work to do */
            return 0;

        elem = g_queue_peek_tail(queue->queue);
        /* cf. the other assert above */
        assert(g_list_length(elem->pair->used) == 0);
        *reqc = elem->reqc;
        data->current_elem = elem;
    }

    return 0;
}

static struct device *find_device_from_lrs_dev(struct io_scheduler *io_sched,
                                               struct lrs_dev *dev)
{
    int i;

    for (i = 0; i < io_sched->devices->len; i++) {
        struct device *d;

        d = g_ptr_array_index(io_sched->devices, i);
        if (d->device == dev)
            return d;
    }

    return NULL;
}

static int allocate_queue_if_loaded(struct io_scheduler *io_sched,
                                    struct request_queue *queue)
{
    struct device *device;
    struct lrs_dev *d;

    d = search_loaded_medium(io_sched->io_sched_hdl->global_device_list,
                             queue->medium_id.name, queue->medium_id.library);
    /* If the device belongs to another scheduler, the request will be pushed to
     * the queue in the hash table. The device will be associated to the queue
     * when it is exchanged with the I/O scheduler that owns it.
     */
    if (!d || !(d->ld_io_request_type & io_sched->type))
        return 0;

    device = find_device_from_lrs_dev(io_sched, d);
    associate_queue_to_device(device, queue);

    return 0;
}

/**
 * Compare two queue elems using the qos and priority of their request.
 *
 * A request with a lower QOS is lower.
 * If two requests has the same QOS, the one with the lowest priority will be
 * the lowest.
 *
 * This internal function is to order the queue. Lower here means from the queue
 * order.
 *
 * This function is used as a parameter of the g_queue_insert_sorted function.
 */
static gint qos_priority_request_compare(gconstpointer _queue_elem_a,
                                         gconstpointer _queue_elem_b,
                                         gpointer user_data)
{
    struct queue_element *queue_elem_a = (struct queue_element *)_queue_elem_a;
    struct queue_element *queue_elem_b = (struct queue_element *)_queue_elem_b;
    pho_req_t *req_a = queue_elem_a->reqc->req;
    pho_req_t *req_b = queue_elem_b->reqc->req;

    if (req_a->qos < req_b->qos)
        return -1;
    else if (req_a->qos > req_b->qos)
        return 1;

    if (req_a->priority < req_b->priority)
        return -1;
    else if (req_a->priority > req_b->priority)
        return 1;

    return 0;
}

static inline bool cfg_ordered_grouped_read(enum rsc_family family)
{
    struct pho_config_item cfg_ordered_item =
        cfg_io_sched[PHO_IO_SCHED_ordered_grouped_read];
    static bool already_set[PHO_RSC_LAST] = {false};
    static bool res[PHO_RSC_LAST];
    const char *value;
    char *section;
    int rc;

    if (already_set[family])
        return res[family];

    res[family] = cfg_ordered_item.value;

    rc = io_sched_cfg_section_name(family, &section);
    if (rc)
        return res[family];

    rc = pho_cfg_get_val(section, cfg_ordered_item.name, &value);
    if (rc)
        goto free_section;

    if (!strcmp(value, "true"))
        res[family] = true;
    else if (!strcmp(value, "false"))
        res[family] = false;
    else
        pho_warn("ordered_grouped_read value must be \"true\" or \"false\", "
                 "and not \"%s\", the default value \"%s\" is taken instead",
                 value, cfg_ordered_item.value ? "true" : "false");

    already_set[family] = true;

free_section:
    free(section);
    return res[family];
}

static inline void queue_insert(struct request_queue *queue,
                                struct queue_element *elem)
{
    if (cfg_ordered_grouped_read(queue->medium_id.family))
        g_queue_insert_sorted(queue->queue, elem, qos_priority_request_compare,
                              NULL);
    else
        g_queue_push_head(queue->queue, elem);
}

static int insert_request_in_medium_queue(struct io_scheduler *io_sched,
                                          struct queue_element *elem,
                                          size_t index)
{
    struct grouped_data *data = io_sched->private_data;
    struct request_queue *queue;
    struct pho_id medium_id;

    reqc_pho_id_from_index(elem->reqc, index, &medium_id);
    queue = g_hash_table_lookup(data->request_queues, &medium_id);
    if (!queue) {
        int rc;

        rc = request_queue_alloc(io_sched, elem, index, &queue);
        if (rc)
            return rc;

        allocate_queue_if_loaded(io_sched, queue);
    }

    queue_insert(queue, elem);
    elem->queue = queue;

    return 0;
}

static int grouped_push_request(struct io_scheduler *io_sched,
                                struct req_container *reqc)
{
    struct grouped_data *data = io_sched->private_data;
    GList *request_list = NULL; /* empty list */
    struct list_pair *pair;
    int rc = 0;
    int i;

    pair = xmalloc(sizeof(*pair));

    for (i = 0; i < reqc->req->ralloc->n_med_ids; i++) {
        struct queue_element *elem;
        int rc;

        elem = xmalloc(sizeof(*elem));

        elem->reqc = reqc;
        elem->pair = pair;

        rc = insert_request_in_medium_queue(io_sched, elem, i);
        if (rc) {
            free(elem);
            GOTO(free_elems, rc);
        }

        /* insert in reverse order to avoid traversing the list on each insert
         */
        request_list = g_list_prepend(request_list, elem);
    }

    /* XXX this list could be sorted by some heuristic. */
    request_list = g_list_reverse(request_list);

    glist_foreach(iter, request_list) {
        struct queue_element *elem = iter->data;

        elem->pair->free = request_list;
        elem->pair->used = NULL;
    }

    pho_debug("Request %p pushed to grouped read scheduler", reqc);

    return 0;

free_elems:
    /* elements are inserted in the list only if they are allocated successfully
     */
    glist_foreach(iter, request_list) {
        remove_element_from_queue(data, iter->data);
        queue_element_free(iter->data, false);
    }
    free(pair);
    g_list_free(request_list);

    return rc;
}

static void remove_elements_from_list(struct grouped_data *data,
                                      struct queue_element *to_ignore,
                                      GList *list)
{
    glist_foreach(iter, list) {
        struct queue_element *elem = iter->data;

        if (elem == to_ignore)
            /* do not free to_ignore yet */
            continue;

        remove_element_from_queue(data, elem);
        queue_element_free(elem, false);
    }
}

static gint glib_match_reqc(gconstpointer _elem,
                            gconstpointer _reqc)
{
    const struct req_container *reqc = _reqc;
    const struct queue_element *elem = _elem;

    if (elem->reqc == reqc)
        return 0;

    return 1;
}

static int grouped_remove_request(struct io_scheduler *io_sched,
                                  struct req_container *reqc)
{
    struct grouped_data *data = io_sched->private_data;
    struct request_queue *queue;
    struct queue_element *elem;
    struct pho_id medium_id;
    GList *link;

    pho_debug("Request %p will be removed from grouped read scheduler", reqc);

    if (data->current_elem && data->current_elem->reqc != reqc)
        /* We should only call remove for requests that just have been returned
         * by peek_request.
         */
        return -EINVAL;

    reqc_pho_id_from_index(reqc, 0, &medium_id);
    queue = g_hash_table_lookup(data->request_queues, &medium_id);
    assert(queue);

    /* find the element corresponding to the first medium in reqc */
    link = g_queue_find_custom(queue->queue, reqc, glib_match_reqc);
    assert(link);

    elem = link->data;
    remove_elements_from_list(data, elem, elem->pair->used);
    remove_elements_from_list(data, elem, elem->pair->free);

    remove_element_from_queue(data, elem);
    queue_element_free(elem, true);

    data->current_elem = NULL;

    return 0;
}

static int grouped_requeue(struct io_scheduler *io_sched,
                           struct req_container *reqc)
{
    struct grouped_data *data = io_sched->private_data;
    int i;

    pho_debug("Request %p will be requeued from grouped read scheduler", reqc);

    if (data->current_elem && data->current_elem->reqc != reqc)
        /* We should only call requeue for requests that just have been returned
         * by peek_request.
         */
        return -EINVAL;

    for (i = 0; i < reqc->req->ralloc->n_med_ids; i++) {
        struct request_queue *queue;
        struct queue_element *elem;
        struct pho_id medium_id;

        reqc_pho_id_from_index(reqc, i, &medium_id);
        queue = g_hash_table_lookup(data->request_queues, &medium_id);
        if (!queue)
            continue;

        elem = g_queue_peek_tail(queue->queue);
        if (elem && elem->reqc == reqc) {
            g_queue_pop_tail(queue->queue);

            /* FIXME After the retry modifications, some elements may have
             * disappeared since n_med_ids may be decreased on error, it is
             * probably best to rebuild all the elements.
             */
            elem->pair->free = g_list_concat(elem->pair->free,
                                             elem->pair->used);
            elem->pair->used = NULL;
            queue_insert(queue, elem);
        }
    }

    data->current_elem = NULL;

    return 0;
}

static struct device *find_unallocated_device(GPtrArray *devices,
                                              struct request_queue *queue)
{
    int i;

    for (i = 0; i < devices->len; i++) {
        struct device *dev;
        bool is_compatible;

        dev = g_ptr_array_index(devices, i);

        if (!dev_is_sched_ready(dev->device))
            continue;

        if (dev->queue)
            continue;

        if (tape_drive_compat(queue->medium_info, dev->device, &is_compatible))
            continue;

        if (is_compatible)
            return dev;
    }

    return NULL;
}

/* GLib callback for g_list_find_custom. Find the first element of the list
 * which is first in its associated queue. The queue of this element will be
 * allocated to a new device, so it must not already be associated to a device.
 */
static gint glib_is_reqc_first_in_queue(gconstpointer _elem,
                                        gconstpointer _queue_ptr)
{
    struct request_queue **queue = (struct request_queue **) _queue_ptr;
    const struct queue_element *elem = _elem;
    struct queue_element *first_elem;

    first_elem = g_queue_peek_tail(elem->queue->queue);
    /* the queue must not be empty */
    assert(first_elem);

    if (first_elem->reqc == elem->reqc) {
        if (elem->queue->device) {
            /* stop as soon as we find a queue already associated to a device */
            *queue = elem->queue;
            return 0;
        }

        if (!*queue) /* keep the first queue found */
            *queue = elem->queue;

        /* A good queue has been found but continue to search for one that
         * is already allocated to a device.
         */
        return 1;
    }

    return 1;
}

/**
 * This function will return a queue whose first element contains reqc and is
 * the best choice according to some heuristic which is for now the first queue
 * associated to a device found. If no queue is associated to a device, get the
 * first one.
 *
 * Simple heuristics can sort queues by:
 * - decreasing number of requests (Nr)
 * - decreasing Nr / T where T is the total estimated time to execute all the
 *   requests (which is optimal with only one drive)
 *   -> this requires, of course, to be able to estimate the total time T.
 *      It could be done with the RAO for example.
 */
static struct request_queue *
find_next_queue_for_request(struct grouped_data *data,
                            struct queue_element *elem)
{
    struct request_queue *queue = NULL;

    if (g_list_length(elem->pair->free) == 0)
        return NULL;

    /* XXX we could use g_list_sort to use some heuristic to chose the best
     * medium, for now choose the first one.
     */
    g_list_find_custom(elem->pair->free, &queue, glib_is_reqc_first_in_queue);

    return queue;
}

static void queue_element_set_used(struct queue_element *elem,
                                   struct queue_element *allocated)
{
    GList *list_elem;

    list_elem = g_list_find(elem->pair->free, allocated);

    /* move the allocated element in the allocated list */
    elem->pair->free = g_list_remove_link(elem->pair->free, list_elem);
    elem->pair->used = g_list_concat(list_elem, elem->pair->used);
}

static int read_req_get_medium_index(struct req_container *reqc,
                                     struct pho_id *medium_id)
{
    int i;

    for (i = 0; i < reqc->req->ralloc->n_med_ids; i++) {
        struct pho_id req_medium_id;

        reqc_pho_id_from_index(reqc, i, &req_medium_id);
        if (pho_id_equal(medium_id, &req_medium_id))
            return i;
    }

    return -EINVAL;
}

static struct device *
find_device_by_queue_medium_id(GPtrArray *devices,
                               const struct pho_id *medium_id)
{
    int i;

    for (i = 0; i < devices->len; i++) {
        struct device *device;

        device = g_ptr_array_index(devices, i);
        if (!device->queue)
            continue;

        if (pho_id_equal(&device->queue->medium_id, medium_id))
            return device;
    }

    return NULL;
}

static int grouped_get_device_medium_pair(struct io_scheduler *io_sched,
                                          struct req_container *reqc,
                                          struct lrs_dev **dev,
                                          size_t *index)
{
    struct grouped_data *data = io_sched->private_data;
    struct request_queue *queue;
    struct queue_element *elem;

    *dev = NULL;

    if (g_list_length(data->current_elem->pair->free) == 0)
        return -ERANGE;

    if (*reqc_get_medium_to_alloc(reqc, *index)) {
        /* This is a retry on a medium previously allocated for this request.
         * Unallocate the request if reqc is the first in the queue.
         * This prevents the scheduler from sticking to a bad choice.
         */
        struct device *device;

        device = find_device_by_queue_medium_id(io_sched->devices,
                     &reqc->params.rwalloc.media[*index].alloc_medium->rsc.id);
        if (device) {
            elem = g_queue_peek_tail(device->queue->queue);

            /* FIXME this does not work since we can have returned the medium
             * <medium_id> but the queue was already allocated previously...
             * I think the only solution is to have a boolean in the queue which
             * indicates that we've just allocated it.
             */
            if (elem && elem->reqc == reqc)
                remove_queue_from_device(device, device->queue);
        }
        lrs_medium_release(reqc->params.rwalloc.media[*index].alloc_medium);
        reqc->params.rwalloc.media[*index].alloc_medium = NULL;
    }

    /* no device with a queue whose next request is reqc */
    queue = find_next_queue_for_request(data, data->current_elem);
    if (!queue)
        return 0;

    if (!queue->device) {
        struct device *device;

        device = find_unallocated_device(io_sched->devices, queue);
        if (!device)
            return 0;

        associate_queue_to_device(device, queue);
    }

    elem = g_queue_peek_tail(queue->queue);
    *reqc_get_medium_to_alloc(elem->reqc, *index) =
        lrs_medium_acquire(&queue->medium_info->rsc.id);

    queue_element_set_used(data->current_elem, elem);
    assert(elem && elem->reqc == reqc);

    *dev = queue->device->device;
    *index = reqc_get_medium_index_from_medium_id(elem->reqc,
                                                  &elem->queue->medium_id);

    return 0;
}

/*
 * Since sreq->reqc has already been removed, the queues associated to its media
 * may have been removed. For each valid medium, we first try to find a queue
 * if it still exists.
 *
 * The best choice is to have a queue already associated to a device since it
 * will not trigger a load. If that's not possible we will use the longest
 * queue. /!\ in this case we don't respect the order of the requests in the
 * queue.
 *
 * If no queue is found (i.e. every queue of each media was removed), we will
 * default to the first valid media.
 */
static int grouped_retry(struct io_scheduler *io_sched,
                         struct sub_request *sreq,
                         struct lrs_dev **dev)
{
    struct media_info **medium = reqc_get_medium_to_alloc(sreq->reqc,
                                                          sreq->medium_index);
    struct grouped_data *data = io_sched->private_data;
    struct request_queue *queue_to_use = NULL;
    struct req_container *reqc = sreq->reqc;
    bool compatible_device_found;
    struct device *device;
    size_t max_length = 0;
    int n_medium_indices;
    int *medium_indices;
    struct pho_id m_id;
    int rc;
    int i;

    pho_debug("Try to reschedule sub request %lu for request %p "
              "in grouped read scheduler",
              sreq->medium_index, sreq->reqc);

    *dev = NULL;

    n_medium_indices = ((*medium)->health == 0 ? 0 : 1) +
        (reqc->req->ralloc->n_med_ids - reqc->req->ralloc->n_required);

    medium_indices = xmalloc(n_medium_indices * sizeof(*medium_indices));

    if ((*medium)->health > 0) {
        medium_indices[0] = sreq->medium_index;
        for (i = 1; i < n_medium_indices; i++)
            medium_indices[i] = reqc->req->ralloc->n_required + (i - 1);
    } else {
        for (i = 0; i < n_medium_indices; i++)
            medium_indices[i] = reqc->req->ralloc->n_required + i;
    }

    for (i = 0; i < n_medium_indices; i++) {
        struct request_queue *queue;

        reqc_pho_id_from_index(reqc, medium_indices[i], &m_id);
        queue = g_hash_table_lookup(data->request_queues, &m_id);
        if (!queue)
            continue;

        if (!queue_to_use) {
            if (queue->device && dev_is_sched_ready(queue->device->device))
                queue_to_use = queue;
        }

        /* XXX We are returning the index of the queue with the most requests.
         * It could be interesting to return the one with the least amount of
         * requests to increase the load balance between drives. But its also
         * interesting to use the biggest queue since we don't increase the
         * likelihood of using queues with a small number of requests.
         */
        if (g_queue_get_length(queue->queue) > max_length &&
            queue->device && dev_is_sched_ready(queue->device->device)) {
            queue_to_use = queue;
            max_length = g_queue_get_length(queue->queue);
            *dev = queue->device->device;
        }
    }
    free(medium_indices);

    if (!queue_to_use) {
        /* no medium in this request is in a queue, so just use the first free
         * medium or the one that was just tried if no error occured on the
         * medium.
         */
        if ((*medium)->health == 0) {
            /* release the previous reference in the request container */
            lrs_medium_release(*medium);
            sreq->medium_index = reqc->req->ralloc->n_required;
        }
        /* else: we use sreq->medium_index i.e. the index of the medium that we
         * failed to load.
         */
    } else {
        if (pho_id_equal(&queue_to_use->medium_id, &(*medium)->rsc.id))
            /* release the previous reference in the request container */
            lrs_medium_release(*medium);

        sreq->medium_index =
            read_req_get_medium_index(reqc, &queue_to_use->medium_id);
    }

    if (sreq->medium_index < 0)
        return sreq->medium_index;

    if (*dev)
        return 0;

    /* find a device for reqc->req->ralloc->med_ids[*index] */
    reqc_pho_id_from_index(reqc, sreq->medium_index, &m_id);
    *dev = search_in_use_medium(io_sched->io_sched_hdl->global_device_list,
                                m_id.name, m_id.library, NULL);
    if (*dev) {
        if (!((*dev)->ld_io_request_type & IO_REQ_READ)) {
            rc = exchange_device(io_sched, IO_REQ_READ, *dev);
            if (rc)
                return rc;
        }

        if ((*dev)->ld_io_request_type & IO_REQ_READ) {
            *dev = dev_is_sched_ready(*dev) ? *dev : NULL;
            return 0;
        }
    }

    /* On error, always fetch DSS information since the caller doesn't know if
     * the \p medium was just allocated or not. It cannot free it.
     */
    *medium = lrs_medium_acquire(&m_id);
    if (!*medium)
        return -errno;

    device = find_compatible_device(io_sched->devices, *medium,
                                    &compatible_device_found);
    if (device)
        *dev = device->device;

    return 0;
}

static void grouped_add_device(struct io_scheduler *io_sched,
                               struct lrs_dev *new_device)
{
    struct device *device;
    int i;

    for (i = 0; i < io_sched->devices->len; i++) {
        struct device *dev;

        dev = g_ptr_array_index(io_sched->devices, i);
        if (new_device == dev->device)
            return;
    }

    device = xmalloc(sizeof(*device));

    device->device = new_device;
    device->queue = NULL;

    g_ptr_array_add(io_sched->devices, device);
}

static struct lrs_dev **grouped_get_device(struct io_scheduler *io_sched,
                                           size_t i)
{
    struct device *device;

    device = g_ptr_array_index(io_sched->devices, i);

    return &device->device;
}

static int grouped_remove_device(struct io_scheduler *io_sched,
                                 struct lrs_dev *device)
{
    int i;

    for (i = 0; i < io_sched->devices->len; i++) {
        struct device *dev;

        dev = g_ptr_array_index(io_sched->devices, i);

        if (dev->device == device) {
            if (dev->queue)
                dev->queue->device = NULL;
            g_ptr_array_remove_index(io_sched->devices, i);
            free(dev);

            return 0;
        }
    }

    return 0;
}

static struct lrs_dev *find_device_to_remove(struct io_scheduler *io_sched,
                                             const char *techno)
{
    size_t shortest_queue = SIZE_MAX;
    struct device *device = NULL;
    int i;

    for (i = 0; i < io_sched->devices->len; i++) {
        struct device *iter = g_ptr_array_index(io_sched->devices, i);

        if (strcmp(iter->device->ld_technology, techno))
            continue;

        if (!iter->queue) {
            device = iter;
            break;
        }

        if (g_queue_get_length(iter->queue->queue) < shortest_queue) {
            device = iter;
            shortest_queue = g_queue_get_length(iter->queue->queue);
        }
    }

    return device->device;
}

static int grouped_exchange_device(struct io_scheduler *io_sched,
                                   union io_sched_claim_device_args *args)
{
    struct device *device_to_remove;
    struct lrs_dev *device_to_add;

    device_to_add = args->exchange.unused_device;
    device_to_remove = find_device(io_sched, args->exchange.desired_device);
    /* /!\ Since this is the device that we are asked for, it must be in
     * the list of devices. If not, this is likely a programming error.
     */
    assert(device_to_remove);

    if (!dev_is_sched_ready(device_to_add) ||
        (device_to_remove->queue &&
         g_queue_get_length(device_to_remove->queue->queue) > 0))
        /* do not give back a device whose queue is not empty */
        return 0;

    device_to_remove->device->ld_io_request_type &= ~io_sched->type;
    device_to_add->ld_io_request_type = io_sched->type;
    grouped_add_device(io_sched, device_to_add);
    g_ptr_array_remove(io_sched->devices, device_to_remove);
    free(device_to_remove);

    return 0;
}

static int grouped_claim_device(struct io_scheduler *io_sched,
                                enum io_sched_claim_device_type type,
                                union io_sched_claim_device_args *args)
{
    switch (type) {
    case IO_SCHED_EXCHANGE:
    case IO_SCHED_BORROW:
        return grouped_exchange_device(io_sched, args);
    case IO_SCHED_TAKE:
        args->take.device =
            find_device_to_remove(io_sched, args->take.technology);

        if (!args->take.device)
            return -ENODEV;

        grouped_remove_device(io_sched, args->take.device);

        return 0;
    default:
        return -EINVAL;
    }

    return 0;
}

struct io_scheduler_ops IO_SCHED_GROUPED_READ_OPS = {
    .init                   = grouped_init,
    .fini                   = grouped_fini,
    .push_request           = grouped_push_request,
    .remove_request         = grouped_remove_request,
    .requeue                = grouped_requeue,
    .peek_request           = grouped_peek_request,
    .get_device_medium_pair = grouped_get_device_medium_pair,
    .retry                  = grouped_retry,
    .add_device             = grouped_add_device,
    .get_device             = grouped_get_device,
    .remove_device          = grouped_remove_device,
    .claim_device           = grouped_claim_device,
};
