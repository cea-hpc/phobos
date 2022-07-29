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
 * \brief  LRS utility functions and data structures
 */
#ifndef _PHO_LRS_UTILS_H
#define _PHO_LRS_UTILS_H

/* Contains every information needed for any component of the LRS to take and
 * update locks
 */
struct lock_handle {
    struct dss_handle *dss;           /**< Reference to the DSS handle of this
                                        *  LRS.
                                        */
    const char        *lock_hostname; /**< Lock hostname for this lrs_sched */
    int                lock_owner;    /**< Lock owner(pid) for this lrs_sched */
};

int lock_handle_init(struct lock_handle *lock_handle, struct dss_handle *dss);

#endif
