/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 * Copyright 2014-2015 CEA/DAM. All Rights Reserved.
 */
/**
 * \brief  Application-friendly API to perform SCSI operations.
 */

#ifndef _SCSI_API_H
#define _SCSI_API_H

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
int mode_sense(int fd, struct mode_sense_info *info);


/* --------------- ELEMENT STATUS API ------------------ */

#define VOL_ID_LEN 37 /* standard: 36 + add 1 to ensure final '\0' */
#define DEV_ID_LEN 33 /* standard: 32 + add 1 to ensure final '\0' */

/** type of elements to retrieve with element_status() */
enum element_type_code {
    TYPE_ALL    = 0,
    TYPE_ARM    = 1, /**< medium transport element (arm) */
    TYPE_SLOT   = 2, /**< storage element (slot) */
    TYPE_IMPEXP = 3, /**< import/export element (impexp)*/
    TYPE_DRIVE  = 4  /**< data transport element (drive) */
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
    uint16_t src_addr; /**< source slot address of the media, previous
                              *   location */

    char vol[VOL_ID_LEN];    /**< volume id */

    char dev_id[DEV_ID_LEN]; /**< device id */
};

/** Call READ ELEMENT STATUS on the given device.
 * @param[in] fd            File descriptor of device changer.
 * @param[in] type          Type of element to query.
 * @param[in] start_addr    Address of first element to query (host endianess).
 * @param[in] nb            Number of elements to get.
 * @param[in] allow_motion  Allow a move of physical arm to perform the query.
 * @param[out] elmt_list    List of elements information (must be freed by the
 *                          caller).
 * @param[out] elmt_count   Number of elements in elmt_list.
 *
 * @return 0 on success, error code < 0 on failure.
 * */
int element_status(int fd, enum element_type_code type,
                   uint16_t start_addr, uint16_t nb, bool allow_motion,
                   struct element_status **elmt_list, int *elmt_count);

#endif
