/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2026 CEA/DAM.
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
 * \brief  Internal structures of the LRS Grouped Read I/O Scheduler.
 *
 * These structures are internal to the grouped_read scheduler but they are
 * declared in this header so that the test suite can inspect the state of
 * the scheduler.
 */
#ifndef _PHO_GROUPED_READ_H
#define _PHO_GROUPED_READ_H

#include <glib.h>

#include "pho_types.h"

struct lrs_dev;
struct media_info;

struct request_queue;

struct device {
    struct lrs_dev       *device;
    struct request_queue *queue;
};

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

#endif
