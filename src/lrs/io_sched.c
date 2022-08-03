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

const struct pho_config_item cfg_io_sched[] = {
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
};

static int io_sched_init(struct pho_io_sched *io_sched)
{
    int rc;

    io_sched->read.io_sched = io_sched;
    rc = io_sched->read.ops.init(&io_sched->read);
    if (rc)
        return rc;
    io_sched->read.devices = g_ptr_array_new();

    io_sched->write.io_sched = io_sched;
    rc = io_sched->write.ops.init(&io_sched->write);
    if (rc)
        goto read_fini;
    io_sched->write.devices = g_ptr_array_new();

    io_sched->format.io_sched = io_sched;
    rc = io_sched->format.ops.init(&io_sched->format);
    if (rc)
        goto write_fini;
    io_sched->format.devices = g_ptr_array_new();

    return 0;

write_fini:
    g_ptr_array_free(io_sched->write.devices, TRUE);
    io_sched->write.ops.fini(&io_sched->write);
read_fini:
    g_ptr_array_free(io_sched->read.devices, TRUE);
    io_sched->read.ops.fini(&io_sched->read);
    return rc;
}

void io_sched_fini(struct pho_io_sched *io_sched)
{
    io_sched->read.ops.fini(&io_sched->read);
    g_ptr_array_free(io_sched->read.devices, TRUE);

    io_sched->write.ops.fini(&io_sched->write);
    g_ptr_array_free(io_sched->write.devices, TRUE);

    io_sched->format.ops.fini(&io_sched->format);
    g_ptr_array_free(io_sched->format.devices, TRUE);
}

int io_sched_dispatch_devices(struct pho_io_sched *io_sched, GPtrArray *devices)
{
    return io_sched->dispatch_devices(io_sched, devices);
}

int io_sched_push_request(struct pho_io_sched *io_sched,
                          struct req_container *reqc)
{
    if (pho_request_is_read(reqc->req)) {
        pho_debug("lrs received read allocation request (%p)", reqc->req);
        return io_sched->read.ops.push_request(&io_sched->read, reqc);
    } else if (pho_request_is_write(reqc->req)) {
        pho_debug("lrs received write allocation request (%p)", reqc->req);
        return io_sched->write.ops.push_request(&io_sched->write, reqc);
    } else if (pho_request_is_format(reqc->req)) {
        pho_debug("lrs received format request (%p)", reqc->req);
        return io_sched->format.ops.push_request(&io_sched->format, reqc);
    }

    LOG_RETURN(-EINVAL, "Invalid request type for I/O scheduler");
}

int io_sched_requeue(struct pho_io_sched *io_sched,
                     struct req_container *reqc)
{
    if (pho_request_is_read(reqc->req))
        return io_sched->read.ops.requeue(&io_sched->read, reqc);
    else if (pho_request_is_write(reqc->req))
        return io_sched->write.ops.requeue(&io_sched->write, reqc);
    else if (pho_request_is_format(reqc->req))
        return io_sched->format.ops.requeue(&io_sched->format, reqc);

    LOG_RETURN(-EINVAL, "Invalid request type for I/O scheduler");
}

int io_sched_remove_request(struct pho_io_sched *io_sched,
                         struct req_container *reqc)
{
    if (pho_request_is_read(reqc->req))
        return io_sched->read.ops.remove_request(&io_sched->read, reqc);
    else if (pho_request_is_write(reqc->req))
        return io_sched->write.ops.remove_request(&io_sched->write, reqc);
    else if (pho_request_is_format(reqc->req))
        return io_sched->format.ops.remove_request(&io_sched->format, reqc);

    LOG_RETURN(-EINVAL, "Invalid request type for I/O scheduler");
}

int io_sched_peek_request(struct pho_io_sched *io_sched,
                          struct req_container **reqc)
{
    struct req_container *requests[3];
    int rc;

    rc = io_sched->read.ops.peek_request(&io_sched->read, &requests[0]);
    if (rc)
        return rc;

    rc = io_sched->write.ops.peek_request(&io_sched->write, &requests[1]);
    if (rc)
        return rc;

    rc = io_sched->format.ops.peek_request(&io_sched->format, &requests[2]);
    if (rc)
        return rc;

    *reqc = io_sched->next_request(io_sched, requests[0], requests[1],
                                   requests[2]);

    return 0;
}

int io_sched_get_device_medium_pair(struct pho_io_sched *io_sched,
                                    struct req_container *reqc,
                                    struct lrs_dev **dev,
                                    size_t *index,
                                    bool is_error)
{
    struct request_handler *handler;

    if (pho_request_is_read(reqc->req))
        handler = &io_sched->read;
    else if (pho_request_is_write(reqc->req))
        handler = &io_sched->write;
    else if (pho_request_is_format(reqc->req))
        handler = &io_sched->format;
    else
        LOG_RETURN(-EINVAL, "Invalid request type: '%s'",
                   pho_srl_request_kind_str(reqc->req));

    return handler->ops.get_device_medium_pair(handler, reqc, dev, index,
                                               is_error);
}

int io_sched_remove_device(struct pho_io_sched *io_sched,
                           struct lrs_dev *device)
{
    int rc2;
    int rc;

    rc = io_sched->read.ops.remove_device(&io_sched->read, device);

    rc2 = io_sched->write.ops.remove_device(&io_sched->write, device);
    if (rc2)
        rc =  rc ? : rc2;

    rc2 = io_sched->format.ops.remove_device(&io_sched->format, device);
    if (rc2)
        rc = rc ? : rc2;

    return rc;
}

static enum io_schedulers str2io_sched(const char *value)
{
    if (!strcmp(value, "fifo"))
        return IO_SCHED_FIFO;

    return IO_SCHED_INVAL;
}

static int get_io_sched(struct pho_io_sched *io_sched,
                        enum io_schedulers type,
                        enum io_request_type request_type)
{
    struct pho_io_scheduler_ops *ops;

    switch (request_type) {
    case IO_REQ_READ:
        ops = &io_sched->read.ops;
        break;
    case IO_REQ_WRITE:
        ops = &io_sched->write.ops;
        break;
    case IO_REQ_FORMAT:
        ops = &io_sched->format.ops;
        break;
    default:
        return -EINVAL;
    }

    switch (type) {
    case IO_SCHED_FIFO:
        *ops = IO_SCHED_FIFO_OPS;
        break;
    default:
        return -EINVAL;
    }

    return 0;
}

static enum io_schedulers
get_io_sched_from_config(enum pho_cfg_params_io_sched type)
{
    const char *value;

    value = _pho_cfg_get(PHO_IO_SCHED_FIRST, PHO_IO_SCHED_LAST,
                         type, cfg_io_sched);
    if (!value)
        return -ENODATA;

    return str2io_sched(value);
}

int load_io_schedulers_from_config(struct pho_io_sched *io_sched)
{
    int rc;

    rc = get_io_sched(io_sched,
                      get_io_sched_from_config(PHO_IO_SCHED_read_algo),
                      IO_REQ_READ);
    if (rc)
        LOG_RETURN(rc, "Failed to read 'read_algo' from config");

    rc = get_io_sched(io_sched,
                      get_io_sched_from_config(PHO_IO_SCHED_write_algo),
                      IO_REQ_WRITE);
    if (rc)
        LOG_RETURN(rc, "Failed to read 'write_algo' from config");

    rc = get_io_sched(io_sched,
                      get_io_sched_from_config(PHO_IO_SCHED_format_algo),
                      IO_REQ_FORMAT);
    if (rc)
        LOG_RETURN(rc, "Failed to read 'format_algo' from config");

    /* TODO load these from the configuration */
    io_sched->next_request = fifo_next_request;
    io_sched->dispatch_devices = no_dispatch;

    return io_sched_init(io_sched);
}
