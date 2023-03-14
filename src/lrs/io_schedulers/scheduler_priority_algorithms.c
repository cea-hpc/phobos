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
 * \brief  Set of algorithms used to choose priority between I/O schedulers
 */
#include "io_sched.h"
#include "schedulers.h"
#include "lrs_sched.h"

#define xor(a, b) ((!!a) ^ (!!b))

static const struct req_container *oldest_request(const struct req_container *a,
                                                  const struct req_container *b)
{
    if (!a && !b)
        return NULL;

    if (xor(a, b))
        /* if one of them is NULL, return the non NULL */
        return a ? : b;

    if (cmp_timespec(&a->received_at, &b->received_at) == -1)
        return a;
    else
        return b;
}

/* Fetch the oldest request from the 3 queues */
struct req_container *fifo_next_request(struct io_sched_handle *io_sched_hdl,
                                        struct req_container *read,
                                        struct req_container *write,
                                        struct req_container *format)
{
    if (oldest_request(read, write) == read) {
        if (read && oldest_request(read, format) == read)
            return read;
        else
            return format;
    } else {
        if (write && oldest_request(write, format) == write)
            return write;
        else
            return format;
    }
}

static enum io_request_type
next_scheduler(enum io_request_type current_scheduler)
{
    switch (current_scheduler) {
    case IO_REQ_READ:
        return IO_REQ_WRITE;
    case IO_REQ_WRITE:
        return IO_REQ_FORMAT;
    case IO_REQ_FORMAT:
        return IO_REQ_READ;
    }

    /* programming error */
    assert(false);
}

struct req_container *round_robin(struct io_sched_handle *io_sched_hdl,
                                  struct req_container *read,
                                  struct req_container *write,
                                  struct req_container *format)
{
    static __thread enum io_request_type current_scheduler = IO_REQ_READ;

    if (!read && !write && !format)
        return NULL;

    if (current_scheduler == IO_REQ_READ) {
        current_scheduler = next_scheduler(current_scheduler);
        if (read)
            return read;
    }

    if (current_scheduler == IO_REQ_WRITE) {
        current_scheduler = next_scheduler(current_scheduler);
        if (write)
            return write;
    }

    if (current_scheduler == IO_REQ_FORMAT) {
        current_scheduler = next_scheduler(current_scheduler);
        if (format)
            return format;
    }

    return NULL;
}
