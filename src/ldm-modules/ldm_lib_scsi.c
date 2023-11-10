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
 * \brief  LDM functions for SCSI tape libraries.
 *
 * Implements SCSI calls to tape libraries.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "scsi_api.h"

#include "pho_common.h"
#include "pho_cfg.h"
#include "pho_ldm.h"
#include "pho_module_loader.h"

#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <jansson.h>
#include <unistd.h>

#define PLUGIN_NAME     "scsi"
#define PLUGIN_MAJOR    0
#define PLUGIN_MINOR    1

static struct module_desc LA_SCSI_MODULE_DESC = {
    .mod_name  = PLUGIN_NAME,
    .mod_major = PLUGIN_MAJOR,
    .mod_minor = PLUGIN_MINOR,
};

/** List of SCSI library configuration parameters */
enum pho_cfg_params_libscsi {
    /** Query the S/N of a drive in a separate ELEMENT_STATUS request
     * (e.g. for IBM TS3500). */
    PHO_CFG_LIB_SCSI_sep_sn_query,

    /* Delimiters, update when modifying options */
    PHO_CFG_LIB_SCSI_FIRST = PHO_CFG_LIB_SCSI_sep_sn_query,
    PHO_CFG_LIB_SCSI_LAST  = PHO_CFG_LIB_SCSI_sep_sn_query,
};

/** Definition and default values of SCSI library configuration parameters */
const struct pho_config_item cfg_lib_scsi[] = {
    [PHO_CFG_LIB_SCSI_sep_sn_query] = {
        .section = "lib_scsi",
        .name    = "sep_sn_query",
        .value   = "0", /* no */
    },
};

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
static int lib_addrs_load(struct lib_descriptor *lib, json_t *message)
{
    int rc;

    /* msi is already loaded */
    if (lib->msi_loaded)
        return 0;

    if (lib->fd < 0)
        return -EBADF;

    rc = scsi_mode_sense(lib->fd, &lib->msi, message);
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

/** Retrieve drive serial numbers in a separate ELEMENT_STATUS request. */
static int query_drive_sn(struct lib_descriptor *lib, json_t *message)
{
    struct element_status   *items = NULL;
    int                      count = 0;
    int                      i;
    int                      rc;

    /* query for drive serial number */
    rc = scsi_element_status(lib->fd, SCSI_TYPE_DRIVE,
                             lib->msi.drives.first_addr, lib->msi.drives.nb,
                             ESF_GET_DRV_ID, &items, &count, message);
    if (rc)
        LOG_GOTO(err_free, rc, "scsi_element_status() failed to get drive S/N");

    if (count != lib->drives.count)
        LOG_GOTO(err_free, rc = -EIO,
                 "Wrong drive count returned by scsi_element_status()");

    /* copy serial number to the library array (should be already allocated) */
    assert(lib->drives.items != NULL);

    for (i = 0; i < count; i++)
        strncpy(lib->drives.items[i].dev_id, items[i].dev_id,
                sizeof(lib->drives.items[i].dev_id));

err_free:
    free(items);
    return rc;
}

/** load status of elements of the given type */
static int lib_status_load(struct lib_descriptor *lib,
                           enum element_type_code type, json_t *message)
{
    json_t *lib_load_json;
    json_t *status_json;
    int rc;

    lib_load_json = json_object();

    /* address of elements are required */
    rc = lib_addrs_load(lib, lib_load_json);
    if (rc) {
        if (json_object_size(lib_load_json) != 0)
            json_object_set_new(message,
                                SCSI_OPERATION_TYPE_NAMES[LIBRARY_LOAD],
                                lib_load_json);

        return rc;
    }

    destroy_json(lib_load_json);
    status_json = json_object();

    if ((type == SCSI_TYPE_ALL || type == SCSI_TYPE_ARM) && !lib->arms.loaded) {
        rc = scsi_element_status(lib->fd, SCSI_TYPE_ARM,
                                 lib->msi.arms.first_addr, lib->msi.arms.nb,
                                 ESF_GET_LABEL, /* to check if the arm holds
                                                   a tape */
                                 &lib->arms.items, &lib->arms.count,
                                 status_json);
        if (rc) {
            if (json_object_size(status_json) != 0)
                json_object_set_new(message,
                                    SCSI_OPERATION_TYPE_NAMES[ARMS_STATUS],
                                    status_json);

            LOG_RETURN(rc, "element_status failed for type 'arms'");
        }

        lib->arms.loaded = true;
        json_object_clear(status_json);
    }

    if ((type == SCSI_TYPE_ALL || type == SCSI_TYPE_SLOT)
        && !lib->slots.loaded) {
        rc = scsi_element_status(lib->fd, SCSI_TYPE_SLOT,
                                 lib->msi.slots.first_addr, lib->msi.slots.nb,
                                 ESF_GET_LABEL,
                                 &lib->slots.items, &lib->slots.count,
                                 status_json);
        if (rc) {
            if (json_object_size(status_json) != 0)
                json_object_set_new(message,
                                    SCSI_OPERATION_TYPE_NAMES[SLOTS_STATUS],
                                    status_json);

            LOG_RETURN(rc, "element_status failed for type 'slots'");
        }

        lib->slots.loaded = true;
        json_object_clear(status_json);
    }

    if ((type == SCSI_TYPE_ALL || type == SCSI_TYPE_IMPEXP)
        && !lib->impexp.loaded) {
        rc = scsi_element_status(lib->fd, SCSI_TYPE_IMPEXP,
                                 lib->msi.impexp.first_addr, lib->msi.impexp.nb,
                                 ESF_GET_LABEL,
                                 &lib->impexp.items, &lib->impexp.count,
                                 status_json);
        if (rc) {
            if (json_object_size(status_json) != 0)
                json_object_set_new(message,
                                    SCSI_OPERATION_TYPE_NAMES[IMPEXP_STATUS],
                                    status_json);

            LOG_RETURN(rc, "element_status failed for type 'impexp'");
        }

        lib->impexp.loaded = true;
        json_object_clear(status_json);
    }

    if ((type == SCSI_TYPE_ALL || type == SCSI_TYPE_DRIVE)
        && !lib->drives.loaded) {
        enum elem_status_flags flags;
        bool separate_query_sn;

        /* separate S/N query? */
        separate_query_sn = PHO_CFG_GET_INT(cfg_lib_scsi, PHO_CFG_LIB_SCSI,
                                            sep_sn_query, 0);

        /* IBM TS3500 can't get both volume label and drive in the same request.
         * So, first get the tape label and 'full' indication, then query
         * the drive ID.
         */
        if (separate_query_sn)
            flags = ESF_GET_LABEL;
        else /* default: get both */
            flags = ESF_GET_LABEL | ESF_GET_DRV_ID;

        rc = scsi_element_status(lib->fd, SCSI_TYPE_DRIVE,
                                 lib->msi.drives.first_addr, lib->msi.drives.nb,
                                 flags, &lib->drives.items, &lib->drives.count,
                                 status_json);
        if (rc) {
            if (json_object_size(status_json) != 0)
                json_object_set_new(message,
                                    SCSI_OPERATION_TYPE_NAMES[DRIVES_STATUS],
                                    status_json);

            LOG_RETURN(rc, "element_status failed for type 'drives'");
        }

        json_object_clear(status_json);

        if (separate_query_sn) {
            /* query drive serial separately */
            rc = query_drive_sn(lib, status_json);
            if (rc) {
                if (json_object_size(status_json) != 0)
                    json_object_set_new(
                        message, SCSI_OPERATION_TYPE_NAMES[DRIVES_STATUS],
                        status_json);

                return rc;
            }
            json_object_clear(status_json);
        }

        lib->drives.loaded = true;
    }

    destroy_json(status_json);

    return 0;
}

static int lib_scsi_open(struct lib_handle *hdl, const char *dev,
                         json_t *message)
{
    struct lib_descriptor *lib;
    int                    rc;
    ENTRY;

    MUTEX_LOCK(&phobos_context()->ldm_lib_scsi_mutex);

    lib = calloc(1, sizeof(struct lib_descriptor));
    if (!lib)
        GOTO(unlock, rc = -ENOMEM);

    hdl->lh_lib = lib;

    lib->fd = open(dev, O_RDWR | O_NONBLOCK);
    if (lib->fd < 0)
        LOG_GOTO(err_clean, rc = -errno, "Failed to open '%s'", dev);

    GOTO(unlock, rc = 0);

err_clean:
    free(lib);
    hdl->lh_lib = NULL;
    json_insert_element(message, "Action",
                        json_string("Open device controller"));
    json_insert_element(message, "Error",
                        json_string("Failed to open device controller"));
unlock:
    MUTEX_UNLOCK(&phobos_context()->ldm_lib_scsi_mutex);
    return rc;
}

static int lib_scsi_close(struct lib_handle *hdl)
{
    struct lib_descriptor *lib;
    int rc;
    ENTRY;

    MUTEX_LOCK(&phobos_context()->ldm_lib_scsi_mutex);

    if (!hdl)
        GOTO(unlock, rc = -EINVAL);

    lib = hdl->lh_lib;
    if (!lib) /* already closed */
        GOTO(unlock, rc = -EBADF);

    lib_status_clear(lib);
    lib_addrs_clear(lib);

    if (lib->fd >= 0)
        close(lib->fd);

    free(lib);
    hdl->lh_lib = NULL;
    rc = 0;

unlock:
    MUTEX_UNLOCK(&phobos_context()->ldm_lib_scsi_mutex);
    return rc;
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
static int lib_scsi_drive_info(struct lib_handle *hdl, const char *drv_serial,
                               struct lib_drv_info *ldi, json_t *message)
{
    struct element_status *drv;
    int rc;
    ENTRY;

    MUTEX_LOCK(&phobos_context()->ldm_lib_scsi_mutex);

    /* load status for drives */
    rc = lib_status_load(hdl->lh_lib, SCSI_TYPE_DRIVE, message);
    if (rc)
        goto unlock;

    /* search for the given drive serial */
    drv = drive_info_from_serial(hdl->lh_lib, drv_serial);
    if (!drv)
        GOTO(unlock, rc = -ENOENT);

    memset(ldi, 0, sizeof(*ldi));
    ldi->ldi_addr.lia_type = MED_LOC_DRIVE;
    ldi->ldi_addr.lia_addr = drv->address;
    ldi->ldi_first_addr =
        ((struct lib_descriptor *)hdl->lh_lib)->msi.drives.first_addr;
    if (drv->full) {
        ldi->ldi_full = true;
        ldi->ldi_medium_id.family = PHO_RSC_TAPE;
        pho_id_name_set(&ldi->ldi_medium_id, drv->vol);
    }

    rc = 0;

unlock:
    MUTEX_UNLOCK(&phobos_context()->ldm_lib_scsi_mutex);
    return rc;
}

/** Implements phobos LDM lib media lookup */
static int lib_scsi_media_info(struct lib_handle *hdl, const char *med_label,
                               struct lib_item_addr *lia, json_t *message)
{
    struct element_status *tape;
    int rc;
    ENTRY;

    MUTEX_LOCK(&phobos_context()->ldm_lib_scsi_mutex);

    /* load all possible tape locations */
    rc = lib_status_load(hdl->lh_lib, SCSI_TYPE_ALL, message);
    if (rc)
        goto unlock;

    /* search for the given tape */
    tape = media_info_from_label(hdl->lh_lib, med_label);
    if (!tape)
        GOTO(unlock, rc = -ENOENT);

    memset(lia, 0, sizeof(*lia));
    lia->lia_type = scsi2ldm_loc_type(tape->type);
    lia->lia_addr = tape->address;

    rc = 0;

unlock:
    MUTEX_UNLOCK(&phobos_context()->ldm_lib_scsi_mutex);
    return rc;
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

/** Search for a free slot in the library */
static int get_free_slot(struct lib_descriptor *lib, uint16_t *slot_addr)
{
    struct element_status *slot;
    int                    i;

    for (i = 0; i < lib->slots.count; i++) {
        slot = &lib->slots.items[i];

        if (!slot->full) {
            *slot_addr = slot->address;
            return 0;
        }
    }
    return -ENOENT;
}

/**
 * Convert a scsi element type code to a human readable string
 * @param [in] code  element type code
 *
 * @return the converted result as a string
 */
static const char *type2str(enum element_type_code code)
{
    switch (code) {
    case SCSI_TYPE_ARM:    return "arm";
    case SCSI_TYPE_SLOT:   return "slot";
    case SCSI_TYPE_IMPEXP: return "import/export";
    case SCSI_TYPE_DRIVE:  return "drive";
    default:               return "(unknown)";
    }
}

/**
 * Select a target slot for move operation.
 * @param[in,out] lib       Library handler.
 * @param[in]     src_lia   Pointer to lib_item_addr of the source element.
 * @param[out]    tgt_addr  Pointer to the address of the selected target.
 * @param[in,out] to_origin As input, indicate whether to favor source slot.
 *                          If false, try to get any free slot.
 *                          As output, it indicates if the source slot was
 *                          actually selected.
 *
 * @return 0 on success, -errno on failure.
 */
static int select_target_addr(struct lib_descriptor *lib,
                              const struct lib_item_addr *src_lia,
                              uint16_t *tgt_addr, bool *to_origin,
                              json_t *message)
{
    const struct element_status *element;
    int rc;

    /* load all info */
    rc = lib_status_load(lib, SCSI_TYPE_ALL, message);
    if (rc)
        return rc;

    element = element_from_addr(lib, src_lia);
    if (!element)
        LOG_RETURN(-EINVAL, "No element at address %#lx",
                   src_lia->lia_addr);

    /* if there is a source addr, use it */
    if (*to_origin && element->src_addr_is_set) {

        /* check the slot is not already full */
        struct lib_item_addr slot_lia = {
            .lia_type = MED_LOC_UNKNOWN,
            .lia_addr = element->src_addr,
        };
        const struct element_status *slot;

        slot = element_from_addr(lib, &slot_lia);
        if (!slot) {
            pho_error(-EADDRNOTAVAIL, "Source address '%#hx' of %s element at "
                      "address '%#hx' does not correspond to any existing "
                      "element. We will search a free address to move.",
                      element->src_addr, type2str(element->type),
                      element->address);
        } else if (slot->type != SCSI_TYPE_SLOT) {
            pho_warn("Source address of %s element at address '%#hx' "
                     "corresponds to a %s element. We do not move to a source "
                     "element different from %s. We will search a free address "
                     "to move.",
                     type2str(element->type), element->address,
                     type2str(slot->type), type2str(SCSI_TYPE_SLOT));
        } else if (!slot->full) {
            *tgt_addr = element->src_addr;
            pho_debug("No target address specified. Using element source "
                      "address '%#hx'.", *tgt_addr);
            return 0;
        } else {
            pho_verb("Source address '%#hx' of element %s at address '%#hx' "
                     "is full. We will search a free address to move.",
                     element->src_addr, type2str(element->type),
                     element->address);
        }
    }

    /* search a free slot to target */
    rc = get_free_slot(lib, tgt_addr);
    if (rc)
        LOG_RETURN(rc, "No Free slot to unload tape");

    *to_origin = (element->src_addr_is_set && (element->src_addr == *tgt_addr));

    pho_verb("Unloading tape to free slot %#hx", *tgt_addr);
    return 0;
}

/** Implements phobos LDM lib media move */
static int lib_scsi_move(struct lib_handle *hdl,
                         const struct lib_item_addr *src_addr,
                         const struct lib_item_addr *tgt_addr,
                         json_t *message)
{
    enum scsi_operation_type type;
    struct lib_descriptor *lib;
    json_t *target_json;
    bool origin = false;
    json_t *move_json;
    uint16_t tgt;
    int rc;
    ENTRY;

    MUTEX_LOCK(&phobos_context()->ldm_lib_scsi_mutex);

    lib = hdl->lh_lib;
    if (!lib) /* already closed */
        GOTO(unlock, rc = -EBADF);

    if (tgt_addr == NULL
        || (tgt_addr->lia_type == MED_LOC_UNKNOWN
            && tgt_addr->lia_addr == 0)) {
        /* First try source slot. If not valid, try any free slot */
        origin = true;
        target_json = json_object();
        rc = select_target_addr(lib, src_addr, &tgt, &origin, target_json);
        if (rc) {
            if (json_object_size(target_json) != 0)
                json_object_set_new(message, "Target selection", target_json);

            goto unlock;
        }

        destroy_json(target_json);
        type = UNLOAD_MEDIUM;
    } else {
        tgt = tgt_addr->lia_addr;
        type = LOAD_MEDIUM;
    }

    move_json = json_object();
    /* arm = 0 for default transport element */
    rc = scsi_move_medium(lib->fd, 0, src_addr->lia_addr, tgt, move_json);

    /* was the source slot invalid? */
    if (rc == -EINVAL && origin) {
        pho_warn("Failed to move media to source slot, trying another one...");

        origin = false;
        target_json = json_object();
        rc = select_target_addr(lib, src_addr, &tgt, &origin, target_json);
        if (rc) {
            if (json_object_size(target_json) != 0)
                json_object_set_new(message, "Target selection", target_json);

            goto unlock;
        }

        destroy_json(target_json);
        json_object_clear(move_json);
        rc = scsi_move_medium(lib->fd, 0, src_addr->lia_addr, tgt, move_json);
    }

    if (json_object_size(move_json) != 0)
        json_object_set_new(message, SCSI_OPERATION_TYPE_NAMES[type],
                            move_json);
    else
        destroy_json(move_json);

unlock:
    MUTEX_UNLOCK(&phobos_context()->ldm_lib_scsi_mutex);
    return rc;
}


/**
 *  \defgroup lib scan (those items are related to lib_scan implementation)
 *  @{
 */

/**
 * Type for a scan callback function.
 *
 * The first argument is the private data of the callback, the second a json_t
 * object representing the lib element that has just been scanned.
 */
typedef void (*lib_scan_cb_t)(void *, json_t *);

/**
 * Calls a lib_scan_cb_t callback on a scsi element
 *
 * @param[in]     element   element to be scanned
 * @param[in]     scan_cb   callback to be called on the element
 * @param[in,out] udata     argument to be passed to scan_cb
 *
 * @return nothing (void function)
 */
static void scan_element(const struct element_status *element,
                         lib_scan_cb_t scan_cb, void *udata)
{
    json_t *root = json_object();
    if (!root) {
        pho_error(-ENOMEM, "Failed to create json root");
        return;
    }

    json_insert_element(root, "type", json_string(type2str(element->type)));
    json_insert_element(root, "address", json_integer(element->address));

    if (element->type & (SCSI_TYPE_ARM | SCSI_TYPE_DRIVE | SCSI_TYPE_SLOT))
        json_insert_element(root, "full", json_boolean(element->full));

    if (element->full && element->vol[0])
        json_insert_element(root, "volume", json_string(element->vol));

    if (element->src_addr_is_set)
        json_insert_element(root, "source_address",
                            json_integer(element->src_addr));

    if (element->except) {
        json_insert_element(root, "error_code",
                            json_integer(element->error_code));
        json_insert_element(root, "error_code_qualifier",
                            json_integer(element->error_code_qualifier));
    }

    if (element->dev_id[0])
        json_insert_element(root, "device_id", json_string(element->dev_id));

    if (element->type == SCSI_TYPE_IMPEXP) {
        json_insert_element(root, "current_operation",
                            json_string(element->impexp ? "import" : "export"));
        json_insert_element(root, "exp_enabled",
                            json_boolean(element->exp_enabled));
        json_insert_element(root, "imp_enabled",
                            json_boolean(element->imp_enabled));
    }

    /* Make "accessible" appear only when it is true */
    if (element->accessible) {
        json_insert_element(root, "accessible", json_true());
    }

    /* Inverted media is uncommon enough so that it can be omitted if false */
    if (element->invert) {
        json_insert_element(root, "invert", json_true());
    }

    scan_cb(udata, root);
    json_decref(root);
}

/** Implements phobos LDM lib scan  */
static int lib_scsi_scan(struct lib_handle *hdl, json_t **lib_data,
                         json_t *message)
{
    lib_scan_cb_t json_array_append_cb;
    struct lib_descriptor *lib;
    int i = 0;
    int rc;

    MUTEX_LOCK(&phobos_context()->ldm_lib_scsi_mutex);

    json_array_append_cb = (lib_scan_cb_t)json_array_append;

    lib = hdl->lh_lib;
    if (!lib) /* closed or missing init */
        GOTO(unlock, rc = -EBADF);

    *lib_data = json_array();

    /* Load everything */
    rc = lib_status_load(lib, SCSI_TYPE_ALL, message);
    if (rc) {
        json_decref(*lib_data);
        *lib_data = NULL;
        LOG_GOTO(unlock, rc, "Error loading scsi library status");
    }

    /* scan arms */
    for (i = 0; i < lib->arms.count ; i++)
        scan_element(&lib->arms.items[i], json_array_append_cb, *lib_data);

    /* scan slots */
    for (i = 0; i < lib->slots.count ; i++)
        scan_element(&lib->slots.items[i], json_array_append_cb, *lib_data);

    /* scan import exports */
    for (i = 0; i < lib->impexp.count ; i++)
        scan_element(&lib->impexp.items[i], json_array_append_cb, *lib_data);

    /* scan drives */
    for (i = 0; i < lib->drives.count ; i++)
        scan_element(&lib->drives.items[i], json_array_append_cb, *lib_data);

    rc = 0;

unlock:
    MUTEX_UNLOCK(&phobos_context()->ldm_lib_scsi_mutex);
    return rc;
}

/** @}*/

/** lib_scsi_adapter exported to upper layers */
static struct pho_lib_adapter_module_ops LA_SCSI_OPS = {
    .lib_open         = lib_scsi_open,
    .lib_close        = lib_scsi_close,
    .lib_drive_lookup = lib_scsi_drive_info,
    .lib_media_lookup = lib_scsi_media_info,
    .lib_media_move   = lib_scsi_move,
    .lib_scan         = lib_scsi_scan,
};

/** Lib adapter module registration entry point */
int pho_module_register(void *module, void *context)
{
    struct lib_adapter_module *self = (struct lib_adapter_module *) module;

    phobos_module_context_set(context);

    self->desc = LA_SCSI_MODULE_DESC;
    self->ops = &LA_SCSI_OPS;

    /*
     * WARNING : this mutex will never be freed because we have no cleaning at
     * module unload.
     */
    pthread_mutex_init(&phobos_context()->ldm_lib_scsi_mutex, NULL);

    return 0;
}
