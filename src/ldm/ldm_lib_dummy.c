/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 * Copyright 2015 CEA/DAM. All Rights Reserved.
 */
/**
 * \brief  Phobos Local Device Manager: dummy library.
 *
 * Dummy library for devices that are always online.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "pho_ldm.h"
#include "pho_common.h"

/**
 * Return drive info for an online device.
 */
static int dummy_drive_lookup(struct lib_handle *lib, const char *drive_serial,
                            struct lib_drv_info *drv_info)
{
    const char  *sep = strchr(drive_serial, ':');
    ENTRY;

    if (sep == NULL)
        return -EINVAL;

    drv_info->ldi_addr.lia_type = MED_LOC_DRIVE;
    drv_info->ldi_addr.lia_addr = 0;
    drv_info->ldi_full = true;
    drv_info->ldi_media_id.type = PHO_DEV_DIR; /** FIXME we don't care.
                                                   Could be disk or other... */
    media_id_set(&drv_info->ldi_media_id, sep + 1);
    return 0;
}

/**
 * Extract path from drive identifier which consists of <host>:<path>.
 */

static int dummy_media_lookup(struct lib_handle *lib, const char *media_label,
                            struct lib_item_addr *med_addr)
{
    ENTRY;

    med_addr->lia_type = MED_LOC_DRIVE; /* always in drive */
    med_addr->lia_addr = 0;
    return 0;
}

/** Exported library adapater */
struct lib_adapter lib_adapter_dummy = {
    .lib_open  = NULL,
    .lib_close = NULL,
    .lib_drive_lookup = dummy_drive_lookup,
    .lib_media_lookup = dummy_media_lookup,
    .lib_media_move = NULL,
};
