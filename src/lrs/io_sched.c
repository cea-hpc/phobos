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
 * \brief  LRS I/O Scheduler Abstraction Implementation
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <glib.h>

#include "io_sched.h"
#include "pho_common.h"
#include "lrs_device.h"
#include "lrs_sched.h"
#include "io_schedulers/schedulers.h"

struct pho_config_item cfg_io_sched[] = {
    [PHO_IO_SCHED_read_algo] = {
        .section = "io_sched",
        .name    = "read_algo",
        .value   = "fifo"
    },
    [PHO_IO_SCHED_write_algo] = {
        .section = "io_sched",
        .name    = "write_algo",
        .value   = "fifo"
    },
    [PHO_IO_SCHED_format_algo] = {
        .section = "io_sched",
        .name    = "format_algo",
        .value   = "fifo"
    },
    [PHO_IO_SCHED_dispatch_algo] = {
        .section = "io_sched",
        .name    = "dispatch_algo",
        .value   = "none",
    },
    [PHO_IO_SCHED_ordered_grouped_read] = {
        .section = "io_sched",
        .name    = "ordered_grouped_read",
        .value   = "true",
    },
};

static int io_sched_init(struct io_sched_handle *io_sched_hdl)
{
    int rc;

    io_sched_hdl->read.io_sched_hdl = io_sched_hdl;
    rc = io_sched_hdl->read.ops.init(&io_sched_hdl->read);
    if (rc)
        return rc;
    io_sched_hdl->read.devices = g_ptr_array_new();

    io_sched_hdl->write.io_sched_hdl = io_sched_hdl;
    rc = io_sched_hdl->write.ops.init(&io_sched_hdl->write);
    if (rc)
        goto read_fini;
    io_sched_hdl->write.devices = g_ptr_array_new();

    io_sched_hdl->format.io_sched_hdl = io_sched_hdl;
    rc = io_sched_hdl->format.ops.init(&io_sched_hdl->format);
    if (rc)
        goto write_fini;
    io_sched_hdl->format.devices = g_ptr_array_new();

    return 0;

write_fini:
    g_ptr_array_free(io_sched_hdl->write.devices, TRUE);
    io_sched_hdl->write.ops.fini(&io_sched_hdl->write);
read_fini:
    g_ptr_array_free(io_sched_hdl->read.devices, TRUE);
    io_sched_hdl->read.ops.fini(&io_sched_hdl->read);
    return rc;
}

void io_sched_fini(struct io_sched_handle *io_sched_hdl)
{
    io_sched_hdl->read.ops.fini(&io_sched_hdl->read);
    g_ptr_array_free(io_sched_hdl->read.devices, TRUE);

    io_sched_hdl->write.ops.fini(&io_sched_hdl->write);
    g_ptr_array_free(io_sched_hdl->write.devices, TRUE);

    io_sched_hdl->format.ops.fini(&io_sched_hdl->format);
    g_ptr_array_free(io_sched_hdl->format.devices, TRUE);
}

int io_sched_dispatch_devices(struct io_sched_handle *io_sched_hdl,
                              GPtrArray *devices)
{
    return io_sched_hdl->dispatch_devices(io_sched_hdl, devices);
}

int io_sched_push_request(struct io_sched_handle *io_sched_hdl,
                          struct req_container *reqc)
{
    if (pho_request_is_read(reqc->req)) {
        io_sched_hdl->io_stats.nb_reads++;
        pho_debug("lrs received read allocation request (%p)", reqc->req);
        return io_sched_hdl->read.ops.push_request(&io_sched_hdl->read, reqc);
    } else if (pho_request_is_write(reqc->req)) {
        io_sched_hdl->io_stats.nb_writes++;
        pho_debug("lrs received write allocation request (%p)", reqc->req);
        return io_sched_hdl->write.ops.push_request(&io_sched_hdl->write, reqc);
    } else if (pho_request_is_format(reqc->req)) {
        io_sched_hdl->io_stats.nb_formats++;
        pho_debug("lrs received format request (%p)", reqc->req);
        return io_sched_hdl->format.ops.push_request(&io_sched_hdl->format,
                                                     reqc);
    }

    LOG_RETURN(-EINVAL, "Invalid request type for I/O scheduler");
}

int io_sched_requeue(struct io_sched_handle *io_sched_hdl,
                     struct req_container *reqc)
{
    if (pho_request_is_read(reqc->req))
        return io_sched_hdl->read.ops.requeue(&io_sched_hdl->read, reqc);
    else if (pho_request_is_write(reqc->req))
        return io_sched_hdl->write.ops.requeue(&io_sched_hdl->write, reqc);
    else if (pho_request_is_format(reqc->req))
        return io_sched_hdl->format.ops.requeue(&io_sched_hdl->format, reqc);

    LOG_RETURN(-EINVAL, "Invalid request type for I/O scheduler");
}

int io_sched_remove_request(struct io_sched_handle *io_sched_hdl,
                         struct req_container *reqc)
{
    if (pho_request_is_read(reqc->req)) {
        io_sched_hdl->io_stats.nb_reads--;
        return io_sched_hdl->read.ops.remove_request(&io_sched_hdl->read, reqc);
    } else if (pho_request_is_write(reqc->req)) {
        io_sched_hdl->io_stats.nb_writes--;
        return io_sched_hdl->write.ops.remove_request(&io_sched_hdl->write,
                                                      reqc);
    } else if (pho_request_is_format(reqc->req)) {
        io_sched_hdl->io_stats.nb_formats--;
        return io_sched_hdl->format.ops.remove_request(&io_sched_hdl->format,
                                                       reqc);
    }

    LOG_RETURN(-EINVAL, "Invalid request type for I/O scheduler");
}

int io_sched_peek_request(struct io_sched_handle *io_sched_hdl,
                          struct req_container **reqc)
{
    struct req_container *requests[3];
    int rc;

    rc = io_sched_hdl->read.ops.peek_request(&io_sched_hdl->read, &requests[0]);
    if (rc)
        return rc;

    rc = io_sched_hdl->write.ops.peek_request(&io_sched_hdl->write,
                                              &requests[1]);
    if (rc)
        return rc;

    rc = io_sched_hdl->format.ops.peek_request(&io_sched_hdl->format,
                                               &requests[2]);
    if (rc)
        return rc;

    *reqc = io_sched_hdl->next_request(io_sched_hdl, requests[0], requests[1],
                                   requests[2]);

    return 0;
}

int io_sched_get_device_medium_pair(struct io_sched_handle *io_sched_hdl,
                                    struct req_container *reqc,
                                    struct lrs_dev **dev,
                                    size_t *index)
{
    struct io_scheduler *io_sched;

    if (pho_request_is_read(reqc->req))
        io_sched = &io_sched_hdl->read;
    else if (pho_request_is_write(reqc->req))
        io_sched = &io_sched_hdl->write;
    else if (pho_request_is_format(reqc->req))
        io_sched = &io_sched_hdl->format;
    else
        LOG_RETURN(-EINVAL, "Invalid request type: '%s'",
                   pho_srl_request_kind_str(reqc->req));

    return io_sched->ops.get_device_medium_pair(io_sched, reqc, dev, index);
}

int io_sched_retry(struct io_sched_handle *io_sched_hdl,
                   struct sub_request *sreq,
                   struct lrs_dev **dev)
{
    struct io_scheduler *io_sched;

    if (pho_request_is_read(sreq->reqc->req))
        io_sched = &io_sched_hdl->read;
    else if (pho_request_is_write(sreq->reqc->req))
        io_sched = &io_sched_hdl->write;
    else if (pho_request_is_format(sreq->reqc->req))
        io_sched = &io_sched_hdl->format;
    else
        LOG_RETURN(-EINVAL, "Invalid request type: '%s'",
                   pho_srl_request_kind_str(sreq->reqc->req));

    return io_sched->ops.retry(io_sched, sreq, dev);
}

int io_sched_remove_device(struct io_sched_handle *io_sched_hdl,
                           struct lrs_dev *device)
{
    int rc2;
    int rc;

    rc = io_sched_hdl->read.ops.remove_device(&io_sched_hdl->read, device);

    rc2 = io_sched_hdl->write.ops.remove_device(&io_sched_hdl->write, device);
    if (rc2)
        rc = rc ? : rc2;

    rc2 = io_sched_hdl->format.ops.remove_device(&io_sched_hdl->format, device);
    if (rc2)
        rc = rc ? : rc2;

    return rc;
}

static struct io_scheduler *
io_type2scheduler(struct io_sched_handle *io_sched_hdl,
                  enum io_request_type type)
{
    switch (type) {
    case IO_REQ_READ:
        return &io_sched_hdl->read;
    case IO_REQ_WRITE:
        return &io_sched_hdl->write;
    case IO_REQ_FORMAT:
        return &io_sched_hdl->format;
    }

    assert(type == 0);
    return NULL;
}

int io_sched_claim_device(struct io_scheduler *io_sched,
                          enum io_sched_claim_device_type type,
                          union io_sched_claim_device_args *args)
{
    struct io_scheduler *target_sched;
    enum io_request_type target_type;
    struct lrs_dev *tmp;
    int rc;

    switch (type) {
    case IO_SCHED_BORROW:
        target_type = args->borrow.dev->ld_io_request_type;
        /* A scheduler must not claim a device it owns. Also, that device cannot
         * be shared between schedulers.
         */
        assert(!(io_sched->type & target_type) &&
               /* <= 1 because the device may belong to no scheduler */
               __builtin_popcount(IO_REQ_ALL & target_type) <= 1);
        break;
    case IO_SCHED_EXCHANGE:
        target_type = args->exchange.desired_device->ld_io_request_type;
        /* A scheduler must not claim a device it owns. Also, that device cannot
         * be shared between schedulers.
         */
        assert(!(io_sched->type & target_type) &&
               /* <= 1 because the device may belong to no scheduler */
               __builtin_popcount(IO_REQ_ALL & target_type) <= 1);
        break;
    case IO_SCHED_TAKE:
        target_type = io_sched->type;
        break;
    default:
        return -EINVAL;
    }

    if (!target_type && type == IO_SCHED_EXCHANGE) {
        /* The target device does not belong to a scheduler, just add it. It
         * will break the current repartition but this is a transient state and
         * will be corrected by the fair_share algorithm on the next iteration
         * of the scheduler thread.
         */
        io_sched->ops.add_device(io_sched, args->exchange.desired_device);
        args->exchange.desired_device->ld_io_request_type |= io_sched->type;

        return 0;
    }

    target_sched = io_type2scheduler(io_sched->io_sched_hdl, target_type);
    rc = target_sched->ops.claim_device(target_sched, type, args);

    if (type != IO_SCHED_EXCHANGE)
        return rc;

    if (args->exchange.desired_device->ld_io_request_type & target_sched->type)
        /* The device still belongs to target_sched. It means that target_sched
         * is still using it. Return now and try again later.
         */
        return rc;

    tmp = args->exchange.desired_device;
    args->exchange.desired_device = args->exchange.unused_device;
    args->exchange.unused_device = tmp;

    return io_sched->ops.claim_device(io_sched, type, args);
}

static enum io_schedulers str2io_sched(const char *value)
{
    if (!strcmp(value, "fifo"))
        return IO_SCHED_FIFO;
    else if (!strcmp(value, "grouped_read"))
        return IO_SCHED_GROUPED_READ;

    return IO_SCHED_INVAL;
}

static void set_static_cfg_section_names(struct pho_config_item *cfg,
                                         size_t len,
                                         char *section_name)
{
    size_t i;

    for (i = 0; i < len; i++)
        cfg[i].section = section_name;
}

int io_sched_cfg_section_name(enum rsc_family family, char **section_name)
{
    int rc;

    rc = asprintf(section_name, IO_SCHED_SECTION_TEMPLATE,
                  rsc_family2str(family));
    if (rc == -1)
        return -ENOMEM;

    return 0;
}

static int
io_sched_get_param_from_cfg(enum pho_cfg_params_io_sched type,
                            enum rsc_family family,
                            const char **value)
{
    char *section_name;
    int rc;

    rc = io_sched_cfg_section_name(family, &section_name);
    if (rc)
        return rc;

    set_static_cfg_section_names(cfg_io_sched, ARRAY_SIZE(cfg_io_sched),
                                 section_name);

    *value = _pho_cfg_get(PHO_IO_SCHED_FIRST, PHO_IO_SCHED_LAST,
                         type, cfg_io_sched);
    free(section_name);
    if (!*value)
        return -ENODATA;

    return 0;
}

static int get_io_sched(struct io_sched_handle *io_sched_hdl,
                        enum rsc_family family,
                        enum io_request_type request_type)
{
    enum pho_cfg_params_io_sched param_key;
    struct io_scheduler_ops *ops;
    const char *param_value;
    int rc;

    switch (request_type) {
    case IO_REQ_READ:
        ops = &io_sched_hdl->read.ops;
        param_key = PHO_IO_SCHED_read_algo;
        break;
    case IO_REQ_WRITE:
        ops = &io_sched_hdl->write.ops;
        param_key = PHO_IO_SCHED_write_algo;
        break;
    case IO_REQ_FORMAT:
        ops = &io_sched_hdl->format.ops;
        param_key = PHO_IO_SCHED_format_algo;
        break;
    default:
        return -EINVAL;
    }

    rc = io_sched_get_param_from_cfg(param_key, family, &param_value);
    if (rc)
        return rc;

    switch (str2io_sched(param_value)) {
    case IO_SCHED_FIFO:
        *ops = IO_SCHED_FIFO_OPS;
        break;
    case IO_SCHED_GROUPED_READ:
        *ops = IO_SCHED_GROUPED_READ_OPS;
        break;
    default:
        return -EINVAL;
    }

    return 0;
}

static int set_dispatch_algorithm(struct io_sched_handle *io_sched_hdl,
                                  enum rsc_family family)
{
    const char *value;
    int rc;

    rc = io_sched_get_param_from_cfg(PHO_IO_SCHED_dispatch_algo, family,
                                     &value);
    if (rc)
        return rc;

    if (!strcmp(value, "none")) {
        /* TODO load next_request from the configuration. For now, the dispatch
         * algo imposes the next_request one so this is fine.
         */
        io_sched_hdl->next_request = fifo_next_request;
        io_sched_hdl->dispatch_devices = no_dispatch;
    } else if (!strcmp(value, "fair_share")) {
        if (family != PHO_RSC_TAPE)
            LOG_RETURN(-EINVAL, "fair_share is only supported for tapes");

        io_sched_hdl->dispatch_devices = fair_share_number_of_requests;
        io_sched_hdl->next_request = round_robin;
    }

    return 0;
}

int io_sched_handle_load_from_config(struct io_sched_handle *io_sched_hdl,
                                     enum rsc_family family)
{
    int rc;

    io_sched_hdl->read.type = IO_REQ_READ;
    io_sched_hdl->write.type = IO_REQ_WRITE;
    io_sched_hdl->format.type = IO_REQ_FORMAT;

    rc = get_io_sched(io_sched_hdl, family, IO_REQ_READ);
    if (rc)
        LOG_RETURN(rc, "Failed to read 'read_algo' from config");

    rc = get_io_sched(io_sched_hdl, family, IO_REQ_WRITE);
    if (rc)
        LOG_RETURN(rc, "Failed to read 'write_algo' from config");

    rc = get_io_sched(io_sched_hdl, family, IO_REQ_FORMAT);
    if (rc)
        LOG_RETURN(rc, "Failed to read 'format_algo' from config");

    rc = set_dispatch_algorithm(io_sched_hdl, family);
    if (rc)
        LOG_RETURN(rc, "Failed to read 'dispatch_algo' from config");

    return io_sched_init(io_sched_hdl);
}

/* This function sets the weights as the proportion of requests in each
 * scheduler. We can implement other metrics in the future:
 * - size of I/O
 * - throughput
 * - flow time (time it takes to handle a request)
 * - a fixed weight from the configuration
 *
 * We can also look at the evolution of these metrics (e.g. if the average
 * throughput of reads decreases, add one device to the read I/O scheduler).
 *
 * Some metrics may require that we extend the protocol to give the size read
 * from a medium in read requests. Also, they don't apply to formats which means
 * that computing a meaningful weight for formats may be more complicated with
 * other metrics. The duration of a format may be a good alternative.
 *
 * We could also give a weight factor in the configuration giving more or less
 * importance to a given scheduler.
 */
int io_sched_compute_scheduler_weights(struct io_sched_handle *io_sched_hdl,
                                       struct io_sched_weights *weights)
{
    size_t total = io_sched_hdl->io_stats.nb_reads +
        io_sched_hdl->io_stats.nb_writes +
        io_sched_hdl->io_stats.nb_formats;

    if (total == 0) {
        /* if no request, distribute the devices equally */
        weights->read = weights->write = weights->format = 1.0 / 3.0;
        return 0;
    }

    weights->read = (double)io_sched_hdl->io_stats.nb_reads / (double)total;
    weights->write = (double)io_sched_hdl->io_stats.nb_writes / (double)total;
    weights->format = (double)io_sched_hdl->io_stats.nb_formats / (double)total;

    return 0;
}

size_t io_sched_count_device_per_techno(struct io_scheduler *io_sched,
                                        const char *techno)
{
    size_t count = 0;
    int i;

    for (i = 0; i < io_sched->devices->len; i++) {
        /* we can dereference the result of get_device since \p i is valid */
        struct lrs_dev *dev = *io_sched->ops.get_device(io_sched, i);

        if (!strcmp(dev->ld_technology, techno))
            count++;
    }

    return count;
}
