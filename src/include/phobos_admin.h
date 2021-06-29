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
    struct pho_comm_info comm;      /**< Communication socket info. */
    struct dss_handle dss;          /**< DSS handle, configured from conf. */
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
int phobos_admin_init(struct admin_handle *adm, bool lrs_required);

/**
 * Add the given devices to the database and inform the LRS that it needs to
 * load the given devices information.
 *
 * \param[in]       adm             Admin module handler.
 * \param[in]       dev_ids         Device IDs to add.
 * \param[in]       num_dev         Number of device to add.
 * \param[in]       keep_locked     true if the device must be locked.
 *
 * \return                          0     on success,
 *                                 -errno on failure.
 */
int phobos_admin_device_add(struct admin_handle *adm, struct pho_id *dev_ids,
                            unsigned int num_dev, bool keep_locked);

/**
 * Update the administrative state of the given devices to 'locked' and
 * inform the LRS devices state has changed.
 *
 * \param[in]       adm             Admin module handler.
 * \param[in]       dev_ids         Device IDs to lock.
 * \param[in]       num_dev         Number of device to lock.
 * \param[in]       is_forced       true if forced lock is requested.
 *
 * \return                          0     on success,
 *                                 -errno on failure.
 */
int phobos_admin_device_lock(struct admin_handle *adm, struct pho_id *dev_ids,
                             int num_dev, bool is_forced);

/**
 * Update the administrative state of the given devices to 'unlocked' and
 * inform the LRS devices state has changed.
 *
 * \param[in]       adm             Admin module handler.
 * \param[in]       dev_ids         Device IDs to unlock.
 * \param[in]       num_dev         Number of device to unlock.
 * \param[in]       is_forced       true if forced unlock is requested.
 *
 * \return                          0     on success,
 *                                 -errno on failure.
 */
int phobos_admin_device_unlock(struct admin_handle *adm, struct pho_id *dev_ids,
                               int num_dev, bool is_forced);

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

/*
 * Ping the daemon to check if it is online or not.
 *
 * \param[in]       adm             Admin module handler.
 *
 * \return                          0     on success,
 *                                 -errno on failure.
 */
int phobos_admin_ping(struct admin_handle *adm);

/**
 * Retrieve layouts of objects whose IDs match the given name or pattern.
 *
 * The caller must release the list calling phobos_admin_layout_list_free().
 *
 * \param[in]       adm             Admin module handler.
 * \param[in]       res             Objids or patterns, depending on
 *                                  \a is_pattern.
 * \param[in]       n_res           Number of resources requested.
 * \param[in]       is_pattern      True if search done using POSIX pattern.
 * \param[in]       medium          Single medium filter.
 * \param[out]      layouts         Retrieved layouts.
 * \param[out]      n_layouts       Number of retrieved items.
 *
 * \return                          0     on success,
 *                                 -errno on failure.
 */
int phobos_admin_layout_list(struct admin_handle *adm, const char **res,
                             int n_res, bool is_pattern, const char *medium,
                             struct layout_info **layouts, int *n_layouts);

/**
 * Release the list of layouts retrieved using phobos_admin_layout_list().
 *
 * \param[in]       layouts            List of layouts to release.
 * \param[in]       n_layouts          Number of layouts to release.
 */
void phobos_admin_layout_list_free(struct layout_info *layouts, int n_layouts);

/**
 * Retrieve the name of the node which holds a medium.
 *
 * @param[in]   adm         Admin module handler.
 * @param[in]   medium_id   ID of the medium to locate.
 * @param[out]  node_name   Name of the node which holds \p medium_id.
 * @return                  0 on success,
 *                         -errno on failure.
 */
int phobos_admin_medium_locate(struct admin_handle *adm,
                               const struct pho_id *medium_id,
                               char **node_name);

#endif
