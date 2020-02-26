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
 * \brief  Phobos Administration interface
 */
#ifndef _PHO_ADMIN_H
#define _PHO_ADMIN_H

#include "pho_types.h"

#include "pho_comm.h"
#include "pho_dss.h"

/**
 * Phobos admin handle.
 */
struct admin_handle {
    struct dss_handle dss;          /**< DSS handle, configured from conf. */
    struct pho_comm_info comm;      /**< Communication socket info. */
    bool daemon_is_online;          /**< True if phobosd is online. */
};

/**
 * Release the admin handler.
 *
 * \param[in/out]   adm             Admin handler.
 */
void phobos_admin_fini(struct admin_handle *adm);

/**
 * Initialize the admin handler.
 *
 * \param[out]      adm             Admin handler.
 * \param[in]       lrs_required    True if the LRS is required.
 *
 * \return                          0     on success,
 *                                 -errno on failure.
 */
int phobos_admin_init(struct admin_handle *adm, const bool lrs_required);

/**
 * Inform the LRS that it needs to reload device information following
 * given operation on the DSS.
 *
 * \FIXME -- In an upcoming patch, this function will do the following:
 * Add a device to the DSS and inform the LRS that it needs to load
 * information of this new device.
 *
 * \param[in]       adm             Admin module handler.
 * \param[in]       family          Device family.
 * \param[in]       name            Device name.
 *
 * \return                          0     on success,
 *                                 -errno on failure.
 */
int phobos_admin_device_add(struct admin_handle *adm, enum rsc_family family,
                            const char *name);

/**
 * Update the administrative state of the given devices to 'unlocked' and
 * inform the LRS devices state has changed.
 *
 * \param[in]       adm             Admin module handler.
 * \param[in]       dev_ids         Device IDs to unlock.
 * \param[in]       num_dev         Number of device to unlock.
 *
 * \return                          0     on success,
 *                                 -errno on failure.
 */
int phobos_admin_device_unlock(struct admin_handle *adm, struct pho_id *dev_ids,
                               const int num_dev);

/**
 * Load and format a medium to the given fs type.
 *
 * \param[in]       adm             Admin module handler.
 * \param[in]       id              Medium ID for the medium to format.
 * \param[in]       fs              Filesystem type.
 * \param[in]       unlock          Unlock tape if succesfully formatted.
 *
 * \return                          0     on success,
 *                                 -errno on failure.
 */
int phobos_admin_format(struct admin_handle *adm, const struct pho_id *id,
                        enum fs_type fs, bool unlock);

#endif
