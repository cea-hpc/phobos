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
 * \brief  Application-friendly API to perform SCSI operations.
 */

#ifndef _SCSI_API_H
#define _SCSI_API_H

#include "pho_common.h"
#include "scsi_common.h"

#include <stdbool.h>

/* --------------- MODE SENSE API ------------------ */

/** element descriptor information for each type (host endianess) */
struct scsi_elt_descr {
    uint16_t first_addr; /**< first element address */
    uint16_t nb;         /**< number of elements */
};

/** useful information from MODE SENSE (host endianess) */
struct mode_sense_info {
    struct scsi_elt_descr  arms;   /**< medium transport elements */
    struct scsi_elt_descr  slots;  /**< storage elements */
    struct scsi_elt_descr  impexp; /**< import/export slots */
    struct scsi_elt_descr  drives; /**< data transfer elements */
};

/**
 * Call SCSI MODE SENSE request on the given device fd.
 * @param[out] info Allocated structure filled by the call.
 * @return 0 on success, error code < 0 on failure.
 */
int scsi_mode_sense(int fd, struct mode_sense_info *info);


/* --------------- ELEMENT STATUS API ------------------ */

#define VOL_ID_LEN 37 /* standard: 36 + add 1 to ensure final '\0' */
#define DEV_ID_LEN 37 /* standard: 36 + add 1 to ensure final '\0' */

/** type of elements to retrieve with element_status() */
enum element_type_code {
    SCSI_TYPE_ALL    = 0,
    SCSI_TYPE_ARM    = 1, /**< medium transport element (arm) */
    SCSI_TYPE_SLOT   = 2, /**< storage element (slot) */
    SCSI_TYPE_IMPEXP = 3, /**< import/export element (impexp)*/
    SCSI_TYPE_DRIVE  = 4  /**< data transport element (drive) */
};

struct element_status {
    enum element_type_code type;

    /** address of the element */
    uint16_t address;

    bool full;            /**< true if the arm/slot/drive holds a media */
    bool impexp;          /**< (imp/exp only) true for import, false for export */
    bool except; /**< false: normal state, true: anormal state
                  * (see error_code and error_code_qualifier in that case). */
    bool accessible;      /**< true if the element is accessible */
    bool exp_enabled;     /**< allow export */
    bool imp_enabled;     /**< allow import */
    bool invert; /**< 2-side media inverted during the transport operation */

    /** Error codes if exception bit is set */
    uint8_t error_code;
    uint8_t error_code_qualifier;

    bool src_addr_is_set; /**< true if src_addr is set */
    uint16_t src_addr;    /**< src slot addr of the media, previous location */
    char vol[VOL_ID_LEN]; /**< volume id */
    char dev_id[DEV_ID_LEN]; /**< device id */
};

/** option flags for scsi_element_status() */
enum elem_status_flags {
   ESF_ALLOW_MOTION = (1 << 0), /**< allow arm motion */
   ESF_GET_LABEL    = (1 << 1), /**< get volume label */
   ESF_GET_DRV_ID   = (1 << 2), /**< get drive identifier */
};

/** Call READ ELEMENT STATUS on the given device.
 * @param[in] fd            File descriptor of device changer.
 * @param[in] type          Type of element to query.
 * @param[in] start_addr    Address of first element to query (host endianess).
 * @param[in] nb            Number of elements to get.
 * @param[in] flags        Option flags.
 * @param[out] elmt_list    List of elements information
 *                          (must be released by the caller using
 *                           element_status_list_free()).
 * @param[out] elmt_count   Number of elements in elmt_list.
 *
 * @return 0 on success, error code < 0 on failure.
 * */
int scsi_element_status(int fd, enum element_type_code type,
                        uint16_t start_addr, uint16_t nb,
                        enum elem_status_flags flags,
                        struct element_status **elmt_list, int *elmt_count);

/**
 * Free a list allocated by element_status().
 * Calling element_status_list_free(NULL) is safe.
 * @param[in] elmt_list list to be released.
 */
void element_status_list_free(struct element_status *elmt_list);


/** Call MOVE MEDIUM on the given device.
 * @param[in] fd            File descriptor of device changer.
 * @param[in] arm_addr      Address of arm to use for the move.
 * @param[in] src_addr      Source address in library (drive, slot, ...)
 * @param[in] tgt_addr      Target address in library (drive, slot, ...)
 *
 * @return 0 on success, error code < 0 on failure.
 */
int scsi_move_medium(int fd, uint16_t arm_addr, uint16_t src_addr,
                     uint16_t tgt_addr, json_t *message);

/** function to handle scsi error codes in a PHO_RETRY_LOOP */
void scsi_retry_func(const char *fnname, int rc, int *retry_cnt,
                     struct scsi_error *err);

#endif
