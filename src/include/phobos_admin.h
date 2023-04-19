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
 *
 * This must be called using the following order:
 *   phobos_init -> phobos_admin_init -> ... -> phobos_admin_fini -> phobos_fini
 */
void phobos_admin_fini(struct admin_handle *adm);

/**
 * Initialize the admin handler.
 *
 * The handler must be released using phobos_admin_fini.
 *
 * \param[out]      adm             Admin handler.
 * \param[in]       lrs_required    True if the LRS is required.
 *
 * \return                          0     on success,
 *                                 -errno on failure.
 *
 * This must be called using the following order:
 *   phobos_init -> phobos_admin_init -> ... -> phobos_admin_fini -> phobos_fini
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
 *
 * This must be called with an admin_handle initialized with phobos_admin_init.
 */
int phobos_admin_device_add(struct admin_handle *adm, struct pho_id *dev_ids,
                            unsigned int num_dev, bool keep_locked);

/**
 * Remove the given devices from the database.
 *
 * All requested devices which are not currently used by a daemon will be
 * removed from the phobos system.
 *
 * The first encountered error is returned once all drives are processed.
 *
 * \param[in]       adm             Admin module handler.
 * \param[in]       dev_ids         Device IDs to remove.
 * \param[in]       num_dev         Number of device to remove.
 * \param[out]      num_removed_dev Number of removed devices.
 *
 * \return                          0     on success,
 *                                 -errno on failure.
 *
 * This must be called with an admin_handle initialized with phobos_admin_init.
 */
int phobos_admin_device_delete(struct admin_handle *adm, struct pho_id *dev_ids,
                               int num_dev, int *num_removed_dev);

/**
 * Update the administrative state of the given devices to 'locked' and
 * inform the LRS devices state has changed.
 *
 * \param[in]       adm             Admin module handler.
 * \param[in, out]  dev_ids         Device IDs to lock. (As input
 *                                  dev_ids[x].name could refer to device path
 *                                  or device id name. As ouput, dev_ids[x].name
 *                                  will refer to device id name.)
 * \param[in]       num_dev         Number of device to lock.
 * \param[in]       is_forced       true if forced lock is requested.
 *
 * \return                          0     on success,
 *                                 -errno on failure.
 *
 * This must be called with an admin_handle initialized with phobos_admin_init.
 */
int phobos_admin_device_lock(struct admin_handle *adm, struct pho_id *dev_ids,
                             int num_dev, bool is_forced);

/**
 * Update the administrative state of the given devices to 'unlocked' and
 * inform the LRS devices state has changed.
 *
 * \param[in]       adm             Admin module handler.
 * \param[in, out]  dev_ids         Device IDs to unlock. (As input
 *                                  dev_ids[x].name could refer to device path
 *                                  or device id name. As ouput, dev_ids[x].name
 *                                  will refer to device id name.)
 * \param[in]       num_dev         Number of device to unlock.
 * \param[in]       is_forced       true if forced unlock is requested.
 *
 * \return                          0     on success,
 *                                 -errno on failure.
 *
 * This must be called with an admin_handle initialized with phobos_admin_init.
 */
int phobos_admin_device_unlock(struct admin_handle *adm, struct pho_id *dev_ids,
                               int num_dev, bool is_forced);

/**
 * Query the status of devices by asking the LRS for it's internal state.
 *
 * @param[in]  adm     Admin module handler.
 * @param[in]  family  targeted family
 * @param[in]  status  allocated JSON string containing status information
 *
 * @return             0 on success, negative error on failure.
 *
 * This must be called with an admin_handle initialized with phobos_admin_init.
 */
int phobos_admin_device_status(struct admin_handle *adm, enum rsc_family family,
                               char **status);

/**
 * Migrate given devices to a new host.
 *
 * The migration can only occur if devices are not currently used by an LRS.
 *
 * \param[in]       adm              Admin module handler.
 * \param[in, out]  dev_ids          Device IDs to update. As input
 *                                   dev_ids[x].name could refer to device path
 *                                   or device id name. As output,
 *                                   dev_ids[x].name will refer to device id
 *                                   name.
 * \param[in]       num_dev          Number of devices to update.
 * \param[in]       host             New host of devices.
 * \param[out]      num_migrated_dev Number of updated device.
 *
 * \return                           0     on success,
 *                                  -errno on failure.
 *
 * This function will process all devices. In case of failure, only the first
 * encountered error is returned once the processing is done.
 *
 * This must be called with an admin_handle initialized with phobos_admin_init.
 */
int phobos_admin_drive_migrate(struct admin_handle *adm, struct pho_id *dev_ids,
                               unsigned int num_dev, const char *host,
                               unsigned int *num_migrated_dev);

/**
 * Load and format media to the given fs type.
 *
 * \param[in]       adm             Admin module handler.
 * \param[in]       ids             List of Medium ID for the media to format.
 * \param[in]       n_ids           Number of format requested.
 * \param[in]       nb_streams      Number of concurrent format requests sent,
 *                                  0 means all requests are sent at once.
 * \param[in]       fs              Filesystem type.
 * \param[in]       unlock          Unlock medium if succesfully formatted.
 * \param[in]       force           Format whatever their status.
 *
 * \return                          0     on success,
 *                                 -errno on failure.
 *
 * This must be called with an admin_handle initialized with phobos_admin_init.
 */
int phobos_admin_format(struct admin_handle *adm, const struct pho_id *ids,
                        int n_ids, int nb_streams, enum fs_type fs,
                        bool unlock, bool force);

/*
 * Ping the daemon to check if it is online or not.
 *
 * \param[in]       adm             Admin module handler.
 *
 * \return                          0     on success,
 *                                 -errno on failure.
 *
 * This must be called with an admin_handle initialized with phobos_admin_init.
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
 *
 * This must be called with an admin_handle initialized with phobos_admin_init.
 */
int phobos_admin_layout_list(struct admin_handle *adm, const char **res,
                             int n_res, bool is_pattern, const char *medium,
                             struct layout_info **layouts, int *n_layouts);

/**
 * Release the list of layouts retrieved using phobos_admin_layout_list().
 *
 * \param[in]       layouts            List of layouts to release.
 * \param[in]       n_layouts          Number of layouts to release.
 *
 * This must be called with an admin_handle initialized with phobos_admin_init.
 */
void phobos_admin_layout_list_free(struct layout_info *layouts, int n_layouts);

/**
 * Retrieve the name of the node which holds a medium or NULL if any node can
 * access this media.
 *
 * @param[in]   adm         Admin module handler.
 * @param[in]   medium_id   ID of the medium to locate.
 * @param[out]  node_name   Name of the node which holds \p medium_id or NULL.
 * @return                  0 on success (\p *node_name can be NULL),
 *                         -errno on failure.
 *
 * This must be called with an admin_handle initialized with phobos_admin_init.
 */
int phobos_admin_medium_locate(struct admin_handle *adm,
                               const struct pho_id *medium_id,
                               char **node_name);

/**
 * Clean locks
 *
 * Delete locks depending on the given parameters.
 *
 * Parameters describing the locks to delete are optional.
 *
 * If global is false, only locks with localhost should be cleaned.
 *
 * If global is true and force is false -EPERM is returned.
 *
 * If force is false and the deamon is running on the current node,
 * -EPERM is returned.
 *
 * If global is true and type, family and id are all null,
 * the function will attempt to clean all locks.
 *
 * If a given type or family is not valid or supported, return -EINVAL.
 *
 * If a valid family is given without a type or with a type different from
 * 'device', 'media_update' or 'media', an -EINVAL error is returned.
 *
 * @param[in]   handle          Admin handle.
 * @param[in]   global          Bool parameter indicating if all locks should be
 *                              clean or not.
 * @param[in]   force           Bool parameter indicating if the operation must
 *                              ignore phobosd's online status or not.
 * @param[in]   lock_hostname   Hostname owning the locks to clean.
 * @param[in]   lock_type       Type of the ressources to clean.
 * @param[in]   dev_family      Family of the devices to clean.
 * @param[in]   lock_ids        List of ids of the ressources to clean.
 * @param[in]   n_ids           Number of ids.
 *
 * @return                      0 on success
 *                              -errno on failure
 *
 * This must be called with an admin_handle initialized with phobos_admin_init.
 */
int phobos_admin_clean_locks(struct admin_handle *adm, bool global,
                             bool force, enum dss_type lock_type,
                             enum rsc_family dev_family,
                             char **lock_ids, int n_ids);

/**
 * Open and scan a library, and generate a json array with unstructured
 * information. Output information may vary, depending on the library.
 *
 * @param[in] lib_type          Type of the library to scan
 * @param[in] lib_dev           Path of the library to scan
 * @param[in,out] lib_data      json object allocated by ldm_lib_scan,
 *                              json_decref must be called later on to
 *                              deallocate it properly
 *
 * @return                      0 on success
 *                              -errno on failure
 *
 * This must be called with an admin_handle initialized with phobos_admin_init.
 */
int phobos_admin_lib_scan(enum lib_type lib_type, const char *lib_dev,
                          json_t **lib_data);

#endif
