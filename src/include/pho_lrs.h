/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2019 CEA/DAM.
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
 * \brief  Phobos Local Resource Scheduler (LRS) interface
 */
#ifndef _PHO_LRS_H
#define _PHO_LRS_H

#include <stdint.h>

#include "pho_comm.h"
#include "pho_types.h"
#include "../lrs/lrs_sched.h"

struct dss_handle;

/**
 * Local Resource Scheduler instance, composed of two parts:
 * - Scheduler: manages media and local devices for the actual IO
 *   to be performed
 * - Communication info: stores info related to the communication with Store
 */
struct lrs {
    struct lrs_sched        sched;     /*!< Scheduler part. */
    struct pho_comm_info    comm;      /*!< Communication part. */
};

/**
 * Identify medium-global error codes.
 * Typically useful to trigger custom procedures when a medium becomes
 * read-only.
 */
static inline bool is_medium_global_error(int errcode)
{
    return errcode == -ENOSPC || errcode == -EROFS || errcode == -EDQUOT;
}

/**
 * Initialize a new LRS bound to a given DSS.
 *
 * The LRS data structure is allocated in lrs_init()
 * and deallocated in lrs_fini().
 *
 * \param[in]   lrs         The LRS to be initialized.
 * \param[in]   dss         The DSS that will be used by \a lrs. Initialized
 *                          (dss_init), managed and deinitialized (dss_fini)
 *                          externally by the caller.
 * \param[in]   sock_path   The path to the communication socket.
 *
 * \return                  0 on success, -1 * posix error code on failure.
 */
int lrs_init(struct lrs *lrs, struct dss_handle *dss, const char *sock_path);

/**
 * Free all resources associated with this LRS except for the dss, which must be
 * deinitialized by the caller if necessary.
 *
 * The LRS data structure is allocated in lrs_init()
 * and deallocated in lrs_fini().
 *
 * \param[in]   lrs The LRS to be deinitialized.
 */
void lrs_fini(struct lrs *lrs);

/**
 * Load and format a media to the given fs type
 *
 * \param[in]   lrs     Initialized LRS.
 * \param[in]   id      Media ID for the media to format.
 * \param[in]   fs      Filesystem type (only PHO_FS_LTFS for now).
 * \param[in]   unlock  Unlock tape if successfully formated.
 * \return              0 on success, negative error code on failure.
 *
 * @TODO: this should be integrated into the LRS protocol
 */
int lrs_format(struct lrs *lrs, const struct media_id *id,
               enum fs_type fs, bool unlock);

/**
 * Make the LRS aware of a new device
 *
 * @FIXME This is a temporary API waiting for a transparent way to add devices
 * while running (to be done with the LRS protocol).
 */
int lrs_device_add(struct lrs *lrs, const struct dev_info *devi);

/**
 * Process pending requests from the unix socket and send the available
 * responses to clients.
 *
 * \param[in]       lrs         The LRS that will handle the requests.
 *
 * \return                      0 on succes, -errno on failure.
 */
int lrs_process(struct lrs *lrs);

#endif
