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
struct req_container *fifo_next_request(struct pho_io_sched *io_sched,
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
