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


static int fifo_init(struct request_handler *handler)
{
    handler->private_data = (void *)g_queue_new();
    if (!handler->private_data)
        return -ENOMEM;

    return 0;
}

static void fifo_fini(struct request_handler *handler)
{
    g_queue_free((GQueue *) handler->private_data);
    return;
}

static int fifo_push_request(struct request_handler *handler,
                             struct req_container *req)
{
    g_queue_push_head((GQueue *)handler->private_data, req);
    return 0;
}

static int fifo_remove_request(struct request_handler *handler,
                               struct req_container *reqc)
{
    // TODO
    return 0;
}

static int fifo_requeue(struct request_handler *handler,
                        struct req_container *reqc)
{
    // TODO
    return 0;
}

static int fifo_peek_request(struct request_handler *handler,
                             struct req_container **req)
{
    *req = g_queue_peek_tail((GQueue *) handler->private_data);
    return 0;
}

static int fifo_get_device_medium_pair(struct request_handler *handler,
                                       struct req_container *reqc,
                                       struct lrs_dev **device,
                                       size_t *index,
                                       bool is_error)
{
    // TODO
    return 0;
}

static int fifo_add_device(struct request_handler *handler,
                           struct lrs_dev *device)
{
    // TODO
    return 0;
}

static int fifo_remove_device(struct request_handler *handler,
                              struct lrs_dev *device)
{
    // TODO
    return 0;
}

static int fifo_reclaim_device(struct request_handler *handler,
                               struct lrs_dev **device)
{
    // TODO
    return 0;
}

struct pho_io_scheduler_ops IO_SCHED_FIFO_OPS = {
    .init                   = fifo_init,
    .fini                   = fifo_fini,
    .push_request           = fifo_push_request,
    .remove_request         = fifo_remove_request,
    .requeue                = fifo_requeue,
    .peek_request           = fifo_peek_request,
    .get_device_medium_pair = fifo_get_device_medium_pair,
    .add_device             = fifo_add_device,
    .remove_device          = fifo_remove_device,
    .reclaim_device         = fifo_reclaim_device,
};
