/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 * Copyright 2015 CEA/DAM. All Rights Reserved.
 */
/**
 * \brief  LDM functions for SCSI tape libraries.
 *
 * Implements SCSI calls to tape libraries.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "pho_ldm.h"
#include "scsi_api.h"
#include "pho_common.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

struct status_array {
    struct element_status *items;
    int  count;
    bool loaded;
};

struct lib_descriptor {
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

/** clear the cache of library element adresses */
static void lib_addrs_clear(struct lib_descriptor *lib)
{
    memset(&lib->msi, 0, sizeof(lib->msi));
    lib->msi_loaded = false;
}

/**
 * Load addresses of elements in library.
 * @return 0 if the mode sense info is successfully loaded, or already loaded.
 */
static int lib_addrs_load(struct lib_descriptor *lib)
{
    int rc;

    /* msi is already loaded */
    if (lib->msi_loaded)
        return 0;

    if (lib->fd < 0)
        return -EBADF;

    rc = mode_sense(lib->fd, &lib->msi);
    if (rc)
        LOG_RETURN(rc, "MODE_SENSE failed");

    lib->msi_loaded = true;
    return 0;
}

/** clear the cache of library elements status */
static void lib_status_clear(struct lib_descriptor *lib)
{
    element_status_list_free(lib->arms.items);
    element_status_list_free(lib->slots.items);
    element_status_list_free(lib->impexp.items);
    element_status_list_free(lib->drives.items);
    memset(&lib->arms, 0, sizeof(lib->arms));
    memset(&lib->slots, 0, sizeof(lib->slots));
    memset(&lib->impexp, 0, sizeof(lib->impexp));
    memset(&lib->drives, 0, sizeof(lib->drives));
}

/** load status of elements of the given type */
static int lib_status_load(struct lib_descriptor *lib,
                           enum element_type_code type)
{
    int rc;

    /* address of elements are required */
    rc = lib_addrs_load(lib);
    if (rc)
        return rc;

    if ((type == SCSI_TYPE_ALL || type == SCSI_TYPE_ARM) && !lib->arms.loaded) {
        rc = element_status(lib->fd, SCSI_TYPE_ARM, lib->msi.arms.first_addr,
                            lib->msi.arms.nb, false, &lib->arms.items,
                            &lib->arms.count);
        if (rc)
            LOG_RETURN(rc, "element_status failed for type 'arms'");

        lib->arms.loaded = true;
    }

    if ((type == SCSI_TYPE_ALL || type == SCSI_TYPE_SLOT)
        && !lib->slots.loaded) {
        rc = element_status(lib->fd, SCSI_TYPE_SLOT, lib->msi.slots.first_addr,
                            lib->msi.slots.nb, false, &lib->slots.items,
                            &lib->slots.count);
        if (rc)
            LOG_RETURN(rc, "element_status failed for type 'slots'");

        lib->slots.loaded = true;
    }

    if ((type == SCSI_TYPE_ALL || type == SCSI_TYPE_IMPEXP)
        && !lib->impexp.loaded) {
        rc = element_status(lib->fd, SCSI_TYPE_IMPEXP,
                            lib->msi.impexp.first_addr, lib->msi.impexp.nb,
                            false, &lib->impexp.items, &lib->impexp.count);
        if (rc)
            LOG_RETURN(rc, "element_status failed for type 'impexp'");

        lib->impexp.loaded = true;
    }

    if ((type == SCSI_TYPE_ALL || type == SCSI_TYPE_DRIVE)
        && !lib->drives.loaded) {
        rc = element_status(lib->fd, SCSI_TYPE_DRIVE,
                            lib->msi.drives.first_addr, lib->msi.drives.nb,
                            false, &lib->drives.items, &lib->drives.count);
        if (rc)
            LOG_RETURN(rc, "element_status failed for type 'drives'");

        lib->drives.loaded = true;
    }

    return 0;
}

static int lib_scsi_open(struct lib_handle *hdl, const char *dev)
{
    struct lib_descriptor *lib;
    int                    rc;
    ENTRY;

    lib = calloc(1, sizeof(struct lib_descriptor));
    if (!lib)
        return -ENOMEM;

    hdl->lh_lib = lib;

    lib->fd = open(dev, O_RDWR | O_NONBLOCK);
    if (lib->fd < 0)
        LOG_GOTO(err_clean, rc = -errno, "Failed to open '%s'", dev);

    return 0;

err_clean:
    free(lib);
    hdl->lh_lib = NULL;
    return rc;
}

static int lib_scsi_close(struct lib_handle *hdl)
{
    struct lib_descriptor *lib;
    ENTRY;

    if (!hdl)
        return -EINVAL;

    lib = hdl->lh_lib;
    if (!lib) /* already closed */
        return -EBADF;

    lib_status_clear(lib);
    lib_addrs_clear(lib);

    if (lib->fd >= 0)
        close(lib->fd);

    free(lib);
    hdl->lh_lib = NULL;
    return 0;
}

/**
 * Match a drive serial number vs. the requested S/N.
 * @return true if serial matches, else false.
 */
static inline bool match_serial(const char *drv_descr, const char *req_sn)
{
    const char *sn;

    /* Matching depends on library type:
     * some librairies only return the SN as drive id,
     * whereas some return a full description like:
     * "VENDOR   MODEL   SERIAL".
     * To match both, we match the last part of the serial.
     */
    sn = strrchr(drv_descr, ' ');
    if (!sn) /* only contains the SN */
        sn = drv_descr;
    else /* first char after last space */
        sn++;

    /* return true on match */
    return !strcmp(sn, req_sn);
}

/** get drive info with the given serial number */
static struct element_status *drive_info_from_serial(struct lib_descriptor *lib,
                                                     const char *serial)
{
    int i;

    for (i = 0; i < lib->drives.count; i++) {
        struct element_status *drv = &lib->drives.items[i];

        if (match_serial(drv->dev_id, serial)) {
            pho_debug("Found drive matching serial '%s': address=%#hx, id='%s'",
                      serial, drv->address, drv->dev_id);
            return drv;
        }
    }
    pho_warn("No drive matching serial '%s'", serial);
    return NULL;
}

/** get media info with the given label */
static struct element_status *media_info_from_label(struct lib_descriptor *lib,
                                                    const char *label)
{
    struct element_status *med;
    int                    i;

    /* search in slots */
    for (i = 0; i < lib->slots.count; i++) {
        med = &lib->slots.items[i];

        if (med->full && !strcmp(med->vol, label)) {
            pho_debug("Found volume matching label '%s' in slot %#hx", label,
                      med->address);
            return med;
        }
    }

    /* search in drives */
    for (i = 0; i < lib->drives.count; i++) {
        med = &lib->drives.items[i];

        if (med->full && !strcmp(med->vol, label)) {
            pho_debug("Found volume matching label '%s' in drive %#hx", label,
                      med->address);
            return med;
        }
    }

    /* search in arm */
    for (i = 0; i < lib->arms.count; i++) {
        med = &lib->arms.items[i];

        if (med->full && !strcmp(med->vol, label)) {
            pho_debug("Found volume matching label '%s' in arm %#hx", label,
                      med->address);
            return med;
        }
    }

    /* search in imp/exp slots */
    for (i = 0; i < lib->impexp.count; i++) {
        med = &lib->impexp.items[i];

        if (med->full && !strcmp(med->vol, label)) {
            pho_debug("Found volume matching label '%s' in import/export slot"
                      " %#hx", label, med->address);
            return med;
        }
    }
    pho_warn("No media matching label '%s'", label);
    return NULL;
}

/** Convert SCSI element type to LDM media location type */
static inline enum med_location scsi2ldm_loc_type(enum element_type_code type)
{
    switch (type) {
    case SCSI_TYPE_ARM:    return MED_LOC_ARM;
    case SCSI_TYPE_SLOT:   return MED_LOC_SLOT;
    case SCSI_TYPE_IMPEXP: return MED_LOC_IMPEXP;
    case SCSI_TYPE_DRIVE:  return MED_LOC_DRIVE;
    default:          return MED_LOC_UNKNOWN;
    }
}

/** Implements phobos LDM lib device lookup */
static int lib_scsi_drive_info(struct lib_handle *hdl,
                                   const char *drv_serial,
                                   struct lib_drv_info *ldi)
{
    struct element_status *drv;
    int rc;
    ENTRY;

    /* load status for drives */
    rc = lib_status_load(hdl->lh_lib, SCSI_TYPE_DRIVE);
    if (rc)
        return rc;

    /* search for the given drive serial */
    drv = drive_info_from_serial(hdl->lh_lib, drv_serial);
    if (!drv)
        return -ENOENT;

    memset(ldi, 0, sizeof(*ldi));
    ldi->ldi_addr.lia_type = MED_LOC_DRIVE;
    ldi->ldi_addr.lia_addr = drv->address;
    if (drv->full) {
        ldi->ldi_full = true;
        ldi->ldi_media_id.type = PHO_DEV_TAPE;
        media_id_set(&ldi->ldi_media_id, drv->vol);
    }
    return 0;
}

/** Implements phobos LDM lib media lookup */
static int lib_scsi_media_info(struct lib_handle *hdl, const char *med_label,
                               struct lib_item_addr *lia)
{
    struct element_status *tape;
    int rc;
    ENTRY;

    /* load all possible tape locations */
    rc = lib_status_load(hdl->lh_lib, SCSI_TYPE_ALL);
    if (rc)
        return rc;

    /* search for the given tape */
    tape = media_info_from_label(hdl->lh_lib, med_label);
    if (!tape)
        return -ENOENT;

    memset(lia, 0, sizeof(*lia));
    lia->lia_type = scsi2ldm_loc_type(tape->type);
    lia->lia_addr = tape->address;

    return 0;
}

/** return information about the element at the given address. */
static const struct element_status *
    element_from_addr(const struct lib_descriptor *lib,
                      const struct lib_item_addr *addr)
{
    int i;

    if (addr->lia_type == MED_LOC_UNKNOWN
        || addr->lia_type == MED_LOC_DRIVE) {
        /* search in drives */
        for (i = 0; i < lib->drives.count; i++) {
            pho_debug("looking for %#lx: drive #%d (addr=%#hx)",
                      addr->lia_addr, i, lib->drives.items[i].address);
            if (lib->drives.items[i].address == addr->lia_addr)
                return &lib->drives.items[i];
        }
    }

    if (addr->lia_type == MED_LOC_UNKNOWN
        || addr->lia_type == MED_LOC_SLOT) {
        /* search in slots */
        for (i = 0; i < lib->slots.count; i++) {
            pho_debug("looking for %#lx: slot #%d (addr=%#hx)",
                      addr->lia_addr, i, lib->slots.items[i].address);
            if (lib->slots.items[i].address == addr->lia_addr)
                return &lib->slots.items[i];
        }
    }

    if (addr->lia_type == MED_LOC_UNKNOWN
        || addr->lia_type == MED_LOC_IMPEXP) {
        /* search in import/export slots */
        for (i = 0; i < lib->impexp.count; i++) {
            pho_debug("looking for %#lx: impexp #%d (addr=%#hx)",
                      addr->lia_addr, i, lib->impexp.items[i].address);
            if (lib->impexp.items[i].address == addr->lia_addr)
                return &lib->impexp.items[i];
        }
    }

    if (addr->lia_type == MED_LOC_UNKNOWN
        || addr->lia_type == MED_LOC_ARM) {
        /* search in arms */
        for (i = 0; i < lib->arms.count; i++) {
            pho_debug("looking for %#lx: arm #%d (addr=%#hx)",
                      addr->lia_addr, i, lib->arms.items[i].address);
            if (lib->arms.items[i].address == addr->lia_addr)
                return &lib->arms.items[i];
        }
    }

    /* not found */
    return NULL;
}

/**
 * Select a target slot for move operation.
 * @param[in,out] lib       Library handler.
 * @param[in]     src_lia   Pointer to lib_item_addr of the source element.
 * @param[out]    tgt_addr  Pointer to the address of the selected target.
 *
 * @return 0 on success, -errno on failure.
 */
static int select_target_addr(struct lib_descriptor *lib,
                              const struct lib_item_addr *src_lia,
                              uint16_t *tgt_addr)
{
    const struct element_status *element;
    int rc;

    /* load all info */
    rc = lib_status_load(lib, SCSI_TYPE_ALL);
    if (rc)
        return rc;

    element = element_from_addr(lib, src_lia);
    if (!element)
        LOG_RETURN(-EINVAL, "No element at address %#lx",
                   src_lia->lia_addr);

    /* if there is a source addr, use it */
    if (element->src_addr_is_set) {
        *tgt_addr = element->src_addr;
        pho_debug("No target address specified. "
                  "Using element source address %#hx.", *tgt_addr);
        return 0;
    } else {
        /* @TODO else, select a free slot */
        LOG_RETURN(-EINVAL, "No target address specified and element "
                   "has no source address.");
    }
}

/** Implements phobos LDM lib media move */
static int lib_scsi_move(struct lib_handle *hdl,
                         const struct lib_item_addr *src_addr,
                         const struct lib_item_addr *tgt_addr)
{
    struct lib_descriptor *lib;
    uint16_t tgt;
    int rc;
    ENTRY;

    lib = hdl->lh_lib;
    if (!lib) /* already closed */
        return -EBADF;

    if (tgt_addr->lia_type == MED_LOC_UNKNOWN
        && tgt_addr->lia_addr == 0) {
        rc = select_target_addr(lib, src_addr, &tgt);
        if (rc)
            return rc;
    } else {
        tgt = tgt_addr->lia_addr;
    }

    /* arm = 0 for default transport element */
    return move_medium(lib->fd, 0, src_addr->lia_addr, tgt);
}

/** lib_scsi_adapter exported to upper layers */
struct lib_adapter lib_adapter_scsi = {
    .lib_open         = lib_scsi_open,
    .lib_close        = lib_scsi_close,
    .lib_drive_lookup = lib_scsi_drive_info,
    .lib_media_lookup = lib_scsi_media_info,
    .lib_media_move   = lib_scsi_move,
    .lib_hdl          = {NULL},
};
