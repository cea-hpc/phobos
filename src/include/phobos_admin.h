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

#include <jansson.h>

#include "pho_types.h"

#include "pho_comm.h"
#include "pho_dss.h"
#include "pho_ldm.h"

/**
 * Phobos admin handle.
 */
struct admin_handle {
    struct pho_comm_info phobosd_comm;  /**< Phobosd Communication socket
                                          *   info.
                                          */
    struct dss_handle dss;          /**< DSS handle, configured from conf. */
    bool phobosd_is_online;         /**< True if phobosd is online. */
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
 * \param[out]      adm                     Admin handler.
 * \param[in]       lrs_required            True if the LRS is required.
 * \param[in]       phobos_context_handle   To set phobos_context (don't used
 *                                          if NULL).
 *
 * \return                          0     on success,
 *                                 -errno on failure.
 *
 * This must be called using the following order:
 *   phobos_init -> phobos_admin_init -> ... -> phobos_admin_fini -> phobos_fini
 */
int phobos_admin_init(struct admin_handle *adm, bool lrs_required,
                      void *phobos_context_handle);

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
                            unsigned int num_dev, bool keep_locked,
                            const char *library);

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
 * Update the configuration of the local daemon
 *
 * \param[in]  adm      Admin module handler.
 * \param[in]  sections Name of the sections of configuration
 * \param[in]  keys     Name of the keys to update
 * \param[in]  values   Values to set
 * \param[in]  n        Number of configuration elements to update
 *
 * \return     0     on success,
 *            -errno on failure.
 */
int phobos_admin_sched_conf_set(struct admin_handle *adm,
                                const char **sections,
                                const char **keys,
                                const char **values,
                                size_t n);

/**
 * Read the configuration from the local daemon
 *
 * \param[in]  adm      Admin module handler.
 * \param[in]  sections Name of the sections of to read
 * \param[in]  keys     Name of the keys to read
 * \param[in]  values   Allocated list of values (must be passed to free as well
 *                      as each element of the list)
 * \param[in]  n        number of configuration elements to read
 *
 * \return     0     on success,
 *            -errno on failure.
 */
int phobos_admin_sched_conf_get(struct admin_handle *adm,
                                const char **sections,
                                const char **keys,
                                const char **values,
                                size_t n);

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
 * \param[in]       library          New library of devices.
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
                               const char *library,
                               unsigned int *num_migrated_dev);

/**
 * Release the scsi reservation of given devices.
 *
 * A device can only be released by the same host that reserved it.
 *
 * \param[in]   adm              Admin module handler.
 * \param[in]   dev_ids          Device IDs to release.
 * \param[in]   num_dev          Number of devices to release.
 * \param[out]  num_released_dev Number of released device.
 *
 * \return                       0      on succes,
 *                               -errno on failure.
 *
 * This must be called with an admin_handle initialized with phobos_admin_init.
 */
int phobos_admin_drive_scsi_release(struct admin_handle *adm,
                                    struct pho_id *dev_ids,
                                    int num_dev, int *num_released_dev);
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

/**
 * Repack a tape.
 *
 * Live extents of the source medium are copied to another medium.
 * Source extents state are set to ORPHAN.
 *
 * \param[in]       adm             Admin module handle.
 * \param[in]       source          Source medium ID.
 * \param[in]       tags            Tags for the destination medium.
 *
 * \return                          0     on success,
 *                                 -errno on failure.
 *
 * This must be called with an admin_handle initialized with phobos_admin_init.
 */
int phobos_admin_repack(struct admin_handle *adm, const struct pho_id *source,
                        struct string_array *tags);

/*
 * Ping the lrs phobosd daemon to check if it is online or not.
 *
 * \param[in]       adm             Admin module handler.
 *
 * \return                          0     on success,
 *                                 -errno on failure.
 *
 * This must be called with an admin_handle initialized with phobos_admin_init.
 */
int phobos_admin_ping_lrs(struct admin_handle *adm);

/*
 * Ping the TLC daemon to check if it is online or not.
 *
 * \param[in]       library         TLC library to ping
 * \param[out]      library_is_up   Set to true if TLC successfully requests the
 *                                  library, set to false otherwise.
 *
 * \return                          0     on success,
 *                                 -errno on failure.
 */
int phobos_admin_ping_tlc(const char *library, bool *library_is_up);

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
 * \param[in]       library         Single library filter.
 * \param[in]       copy_name       Single copy name filter.
 * \param[in]       orphan          Orphan state filter.
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
                             const char *library, const char *copy_name,
                             bool orphan, struct layout_info **layouts,
                             int *n_layouts, struct dss_sort *sort);

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
 * @param[in]   lock_ids        List of ids of the locks to clean.
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
 * Adds a list of media into the DSS with the "blank" fs_status
 *
 * @param[in]  adm          Admin handle.
 * @param[in]  med_ls       List of media to add.
 * @param[in]  med_cnt      Number of media to add.
 *
 * @return                  0 on success
 *                          -errno on failure
 */
int phobos_admin_media_add(struct admin_handle *adm, struct media_info *med_ls,
                           int med_cnt);

/**
 * Removes a list of media from the database.
 *
 * All requested media which are not currently used by a daemon or does not
 * contain any extents will be removed from the phobos system.
 *
 * The first encountered error is returned once all media are processed.
 *
 * \param[in]      adm             Admin module handler.
 * \param[in]      med_ids         List of media to remove.
 * \param[in]      num_med         Number of media to remove.
 * \param[out]     num_removed_med Number of removed media.
 *
 * \return                         0      on success,
 *                                 -errno on failure.
 *
 * This must be called with an admin_handle initialized with phobos_admin_init.
 */
int phobos_admin_media_delete(struct admin_handle *adm, struct pho_id *med_ids,
                              int num_med, int *num_removed_med);
/**
 * Imports non-empty media into the DSS without formatting them thus preserving
 * the data on the device.
 *
 * @param[in]  adm          Admin handle.
 * @param[in]  med_ls       List of media to import.
 * @param[in]  n_ids        Number of media to import.
 * @param[in]  check_hash   Bool parameter to indicate if hashs must be
 *                          recalculated and compared with the hashs from
 *                          the extended attributes of the file.
 *
 * @return                  0 on success
 *                          -errno on failure
 */
int phobos_admin_media_import(struct admin_handle *adm,
                              struct media_info *med_ls, int med_cnt,
                              bool check_hash);

/**
 * Change the library of media.
 *
 * \param[in]      adm             Admin module handler.
 * \param[in]      med_ids         List of media to rename.
 * \param[in]      num_med         Number of media to rename.
 * \param[in]      library         New library of media.
 * \param[out]     num_removed_med Number of renamed media.
 *
 * \return                         0      on success,
 *                                 -errno on failure.
 *
 * This must be called with an admin_handle initialized with phobos_admin_init.
 */
int phobos_admin_media_library_rename(struct admin_handle *adm,
                                      struct pho_id *med_ids,
                                      int num_med, const char *library,
                                      int *num_renamed_med);

/**
 * Open and scan a library, and generate a json array with unstructured
 * information. Output information may vary, depending on the library.
 *
 * @param[in] lib_type          Type of the library to scan
 * @param[in] refresh           If true, the library module must refresh its
 *                              cache from the library before answering the scan
 * @param[in] library           Library to scan
 * @param[in,out] lib_data      json object allocated by ldm_lib_scan,
 *                              json_decref must be called later on to
 *                              deallocate it properly
 *
 * @return                      0 on success
 *                              -errno on failure
 *
 * This must be called with an admin_handle initialized with phobos_admin_init.
 */
int phobos_admin_lib_scan(enum lib_type lib_type, const char *library,
                          bool refresh, json_t **lib_data);

/**
 * Refresh the library internal cache
 *
 * @param[in] lib_type          Type of the library to refresh
 * @param[in] library           Library to refresh
 *
 * @return                      0 on success
 *                              -errno on failure
 */
int phobos_admin_lib_refresh(enum lib_type lib_type, const char *library);

/**
 * Dump a list of logs to a given file.
 *
 * If any field of \p log_filter is non-NULL, it will be used to filter the
 * entries to dump.
 *
 * @param[in]   adm            Admin module handler.
 * @param[in]   file           File descriptor where the logs should be dumped.
 * @param[in]   log_filter     Filter for the logs to clear.
 *
 * @return 0 if success, -errno if an error occurs
 */
int phobos_admin_dump_logs(struct admin_handle *adm, int fd,
                           struct pho_log_filter *log_filter);

/**
 * Clear a list of logs.
 *
 * If any field of \p log_filter is non-NULL, it will be used to filter the
 * entries to clear.
 *
 * @param[in]   adm            Admin module handler.
 * @param[in]   log_filter     Filter for the logs to clear.
 * @param[in]   clear_all      True to clear every log, ignored if any of the
 *                             other parameters except \p adm is given
 *
 * @return 0 if success, -errno if an error occurs
 */
int phobos_admin_clear_logs(struct admin_handle *adm,
                            struct pho_log_filter *log_filter, bool clear_all);

/**
 * Lookup a drive from its serial number
 *
 * @param[in]       adm         Admin module handler.
 * @param[in]       id          id.name could be serial number or path.
 *                              id.family must be PHO_RSC_TAPE
 * @param[in,out]   drive_info  Existing Drive info structure provided by the
 *                              caller, filled by this call.
 *
 * @return 0 if success, -errno if an error occurs
 */
int phobos_admin_drive_lookup(struct admin_handle *adm, struct pho_id *id,
                              struct lib_drv_info *drive_info);

/**
 * Load a tape in a drive
 *
 * @param[in]       adm             Admin module handler.
 * @param[in,out]   drive_id        drive_id.name could be serial number or path
 *                                  drive_id.family must be PHO_RSC_TAPE
 *                                  (if success, drive_id.name is set to serial)
 * @param[in]       tape_id         Tape to load (tape_id.family must be
 *                                  PHO_RSC_TAPE)
 *
 * @return 0 if success, -errno if an error occurs
 */
int phobos_admin_load(struct admin_handle *adm, struct pho_id *drive_id,
                      const struct pho_id *tape_id);

/**
 * Unload a tape from a drive
 *
 * @param[in]       adm             Admin module handler.
 * @param[in,out]   drive_id        drive_id.name could be serial number or path
 *                                  drive_id.family must be PHO_RSC_TAPE
 *                                  (if success, drive_id.name is set to serial)
 * @param[in]       tape_id         Tape to unload from the drive. Ignored if
 *                                  NULL, otherwise the drive is unloaded only
 *                                  if it contains this tape_id. (tape_id.family
 *                                  must be PHO_RSC_TAPE)
 *
 * @return 0 if success, -errno if an error occurs
 */
int phobos_admin_unload(struct admin_handle *adm, struct pho_id *drive_id,
                        const struct pho_id *tape_id);

/**
 * @param[in]  id        id of the medium locked by someone else
 * @param[in]  hostname  hostname of the LRS which holds the lock
 *
 * @return               0 on success
 *                       negative error code on error
 */
typedef int (*lock_conflict_handler_t)(const struct pho_id *id,
                                       const char *hostname);

static inline int
default_conflict_handler(const struct pho_id *id,
                         const char *hostname)
{
    pho_warn("Medium (family '%s', name '%s', library '%s') is locked by '%s', "
             "it will not be notified of the change",
             rsc_family2str(id->family), id->name, id->library, hostname);
    return 0;
}

/**
 * Send a notification to inform the update of a list of media.
 *
 * @param[in] adm          Valid admin handle
 * @param[in] ids          The list of media that where updated
 * @param[in] count        The number of media in \p media
 * @param[in] on_conflict  Callback run for each media that is locked by another
 *                         host and cannot be updated
 *
 * \p on_conflict will be called on all the conflicting media even if it fails
 * at some point to make sure that the caller is notified of all the conflicts.
 *
 * If \p on_conflict is NULL, use default_conflict_handler
 *
 * @return            0 on success,
 *                    negative POSIX error code on error
 */
int phobos_admin_notify_media_update(struct admin_handle *adm,
                                     struct pho_id *ids,
                                     size_t count,
                                     lock_conflict_handler_t on_conflict);

#endif
