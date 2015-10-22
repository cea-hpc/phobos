/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 * Copyright 2015 CEA/DAM. All Rights Reserved.
 */
/**
 * \brief  LDM functions for SCSI tape libraries.
 *
 * Prototypes for querying tape libraries.
 */
#include "pho_types.h"
#include "scsi_api.h"

/** library handle */
struct ldm_lib_handle {
    void  *lh_lib;
};

struct lib_dev_info {
    uint16_t             address;
    bool                 is_full;  /**< true if a media is in the drive */
    struct media_id      media_id; /**< media label, if drive is full */
};

enum med_location {
    MED_LOC_UNKNOWN = 0,
    MED_LOC_DRIVE   = 1,
    MED_LOC_SLOT    = 2,
    MED_LOC_ARM     = 3,
    MED_LOC_IMPEXP  = 4
};

struct lib_media_info {
    enum med_location    loc_type;
    uint16_t             address;
};

/** get a handle to scsi library */
int ldm_lib_scsi_open(struct ldm_lib_handle *hdl, const char *dev);

/** close handle to scsi library */
int ldm_lib_scsi_close(struct ldm_lib_handle *hdl);

/** Query drive information from a tape library
 * @param(in)  serial    Serial number of the tape drive to query.
 * @param(out) ldi       Information about the tape drive.
 */
int ldm_lib_scsi_drive_info(struct ldm_lib_handle *hdl, const char *serial,
                            struct lib_dev_info *ldi);

/** Query media information from a tape library
 * @param(in)  label     Label of the tape to query.
 * @param(out) lmi       Information about the tape.
 */
int ldm_lib_scsi_media_info(struct ldm_lib_handle *hdl, const char *label,
                            struct lib_media_info *lmi);
