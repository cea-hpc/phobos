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
 * \brief  public functions and schedulers operations
 */
#ifndef _PHO_SCHEDULERS_H
#define _PHO_SCHEDULERS_H

#include "io_sched.h"

extern struct io_scheduler_ops IO_SCHED_FIFO_OPS;
extern struct io_scheduler_ops IO_SCHED_GROUPED_READ_OPS;

/********************************
 * Device dispatcher algorithms *
 ********************************/

/**
 * Other possible algorithms:
 * - dispatch devices to I/O schedulers given a percentage:
 *   (e.g. 40% to read, 50% to write and 10% to format)
 * - dynamically dispatch devices depending on the load, we could have a basic
 *   repartition like in the previous algorithm and move some devices around as
 *   needed
 */

/**
 * Do not dispatch devices, simply copy every device in each I/O scheduler.
 */
int no_dispatch(struct io_sched_handle *io_sched_hdl,
                GPtrArray *devices);

/**
 * Dispatch devices so that the LRS handles different types of requests fairly.
 *
 * This algorithm will look at the load of the system (currently the proportion
 * of read, write and format requests) and allocate devices to I/O schedulers
 * based on the relative proportion of requests (i.e. if we have 40% of reads,
 * the read scheduler will use 40% of the available devices).
 *
 * This repartition is bounded by a min/max per tape technology. As long as an
 * I/O scheduler has at least one request to handle, it will get the minimum
 * number of devices.
 *
 * If an I/O scheduler has reached the maximum number of devices it can get and
 * its share of the devices is less than the share of its requests, the
 * remaining devices will be allocated to other schedulers.
 */
int fair_share_number_of_requests(struct io_sched_handle *io_sched_hdl,
                                  GPtrArray *devices);

/*********************************
 * Scheduler priority algorithms *
 *********************************/

/*
 * Other possible algorithm:
 * - return read Pr% of the time, write Pw% of the time and format Pf% of
 *   the time.
 */

/* Return the oldest request out of the 3.
 */
struct req_container *fifo_next_request(struct io_sched_handle *io_sched_hdl,
                                        struct req_container *read,
                                        struct req_container *write,
                                        struct req_container *format);

/**
 * Round robin: return read then write then format. If the request that should
 * be returned is NULL, return the next one instead. If they are all NULL,
 * return NULL.
 */
struct req_container *round_robin(struct io_sched_handle *io_sched_hdl,
                                  struct req_container *read,
                                  struct req_container *write,
                                  struct req_container *format);

#endif
