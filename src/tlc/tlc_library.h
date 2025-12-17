/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2023 CEA/DAM.
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
 * \brief  TLC library interface
 */

#ifndef _PHO_TLC_LIBRARY_H
#define _PHO_TLC_LIBRARY_H

#include "pho_dss.h"
#include "pho_ldm.h"
#include "scsi_api.h"

struct status_array {
    struct element_status *items;
    int count;
    bool loaded;
};

#include "pho_types.h"

struct lib_descriptor {
    /* library name */
    char name[PHO_URI_MAX];

    /* List of lib devices */
    char **lib_devices;
    size_t nb_lib_device;

    /* file descriptor to SCSI lib device */
    int fd;

    /* Cache of library element address */
    struct mode_sense_info msi;
    bool msi_loaded;

    /* Cache of library element status */
    struct status_array arms;
    struct status_array slots;
    struct status_array impexp;
    struct status_array drives;
};

/**
 * Open the library and load current status
 *
 * @param[in,out]   lib             Already allocated library to fill with
 *                                  current status
 * @param[out]      json_message    Set to NULL, if no message. On error or
 *                                  success, could be set to a value different
 *                                  from NULL, containing a message which
 *                                  describes the actions and must be decref by
 *                                  the caller.
 *
 * @return              0 if success, else a negative error code
 */
int tlc_library_open(struct lib_descriptor *lib, json_t **json_message);

/**
 * Close and clean the library
 *
 * @param[in,out]   lib     Library to close and clean
 */
void tlc_library_close(struct lib_descriptor *lib);

/**
 * Get the location and the loaded medium (if any) of a device in library
 * from its serial number.
 *
 * @param[in]   lib             Library descriptor.
 * @param[in]   drive_serial    Serial number of the drive to lookup.
 * @param[out]  drv_info        Information about the drive.
 * @param[out]  json_error_message  Set to NULL on success. On error, could be
 *                                  set to a value different from NULL,
 *                                  containing a message which describes the
 *                                  error and must be decref by the caller.
 *
 * @return 0 on success, negative error code on failure.
 */
int tlc_library_drive_lookup(struct lib_descriptor *lib,
                             const char *drive_serial,
                             struct lib_drv_info *drv_info,
                             json_t **json_error_message);

/**
 * Load a medium into a drive
 *
 * @param[in]   dss             DSS handle.
 * @param[in]   lib             Library descriptor.
 * @param[in]   drive_serial    Serial number of the target drive.
 * @param[in]   tape_label      Label of the target tape.
 * @param[out]  json_message    Set to NULL, if no message. On error or success,
 *                              could be set to a value different from NULL,
 *                              containing a message which describes the actions
 *                              and must be decref by the caller.
 *
 * @return 0 on success, negative error code on failure.
 */
int tlc_library_load(struct dss_handle *dss, struct lib_descriptor *lib,
                     const char *drive_serial, const char *tape_label,
                     json_t **json_message);

/**
 * Unload a tape from a drive to a free slot
 *
 * If the source address is set and conforms to a free slot, we use it at
 * unload_addr otherwise we use any existing free slot.
 *
 * @param[in]  lib              Library descriptor.
 * @param[in]  drive_serial     Serial number of the target drive.
 * @param[in]  loaded_tape_label    If not NULL, drive is unloaded only if the
 *                                  loaded tape has this label
 * @param[out] unloaded_tape_label  Allocated unloaded tape label on success,
 *                                  must be freed by caller. NULL on error or
 *                                  if the target drive was empty.
 * @param[out] unload_addr      Returns on success the address where the
 *                              tape is unloaded.
 * @param[out] json_message     Set to NULL, if no message. On error or success,
 *                              could be set to a value different from NULL,
 *                              containing a message which describes the actions
 *                              and must be decref by the caller.
 *
 * @return 0 on success, negative error code on failure.
 */
int tlc_library_unload(struct dss_handle *dss, struct lib_descriptor *lib,
                       const char *drive_serial, const char *loaded_tape_label,
                       char **unloaded_tape_label,
                       struct lib_item_addr *unload_addr,
                       json_t **json_message);

/**
 * Build a json describing the library's current status
 *
 * @param[in]   lib             Library descriptor.
 * @param[out]  lib_data        Json allocated and filled by tlc_library_status
 *                              (must be decref by the caller). On error NULL is
 *                              returned.
 * @param[out] json_message     Set to NULL, if no message. On error or success,
 *                              could be set to a value different from NULL,
 *                              containing a message which describes the actions
 *                              and must be decref by the caller.
 *
 * @return 0 on success, negative error code on failure
 */
int tlc_library_status(struct lib_descriptor *lib, json_t **lib_data,
                       json_t **json_message);

/**
 * Refresh the library descriptor
 *
 * @param[in,out]   lib             Library descriptor.
 * @param[out]      json_message    Set to NULL, if no message. On error or
 *                                  success, could be set to a value different
 *                                  from NULL, containing a message which
 *                                  describes the actions and must be decref by
 *                                  the caller.
 *
 * @return 0 on success, negative error code on failure
 */
int tlc_library_refresh(struct lib_descriptor *lib, json_t **json_message);


/**
 * Only exported for internal test purpose
 */
struct element_status *
drive_element_status_from_serial(struct lib_descriptor *lib,
                                 const char *serial);

struct element_status *
media_element_status_from_label(struct lib_descriptor *lib,
                                const char *label);

#endif /* _PHO_TLC_LIBRARY_H */
