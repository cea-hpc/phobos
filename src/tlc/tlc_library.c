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
 * \brief  TLC library interface implementation
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <fcntl.h>
#include <inttypes.h>
#include <jansson.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>
#include <unistd.h>

#include "pho_cfg.h"
#include "pho_common.h"
#include "pho_types.h"
#include "scsi_api.h"
#include "tlc_library.h"

/** List of SCSI library configuration parameters */
enum pho_cfg_params_libscsi {
    /** Query the S/N of a drive in a separate ELEMENT_STATUS request
     * (e.g. for IBM TS3500).
     */
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
        memcpy(lib->drives.items[i].dev_id, items[i].dev_id,
               sizeof(lib->drives.items[i].dev_id));

err_free:
    free(items);
    return rc;
}

/** load status of elements of the given type */
static int lib_status_load(struct lib_descriptor *lib,
                           enum element_type_code type, json_t **message)
{
    json_t *lib_load_json;
    json_t *status_json;
    int rc;

    *message = NULL;
    lib_load_json = json_object();

    /* address of elements are required */
    rc = lib_addrs_load(lib, lib_load_json);
    if (rc) {
        if (json_object_size(lib_load_json) != 0) {
            *message = json_object();
            if (*message)
                json_object_set_new(*message,
                                    SCSI_OPERATION_TYPE_NAMES[LIBRARY_LOAD],
                                    lib_load_json);
            else
                json_decref(lib_load_json);
        }

        return rc;
    }

    json_decref(lib_load_json);
    status_json = json_object();

    if ((type == SCSI_TYPE_ALL || type == SCSI_TYPE_ARM) && !lib->arms.loaded) {
        rc = scsi_element_status(lib->fd, SCSI_TYPE_ARM,
                                 lib->msi.arms.first_addr, lib->msi.arms.nb,
                                 /* to check if the arm holds a tape */
                                 ESF_GET_LABEL,
                                 &lib->arms.items, &lib->arms.count,
                                 status_json);
        if (rc) {
            if (json_object_size(status_json) != 0) {
                *message = json_object();
                if (*message)
                    json_object_set_new(*message,
                                        SCSI_OPERATION_TYPE_NAMES[ARMS_STATUS],
                                        status_json);
                else
                    json_decref(status_json);
            }

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
            if (json_object_size(status_json) != 0) {
                *message = json_object();
                if (*message)
                    json_object_set_new(*message,
                                        SCSI_OPERATION_TYPE_NAMES[SLOTS_STATUS],
                                        status_json);
                else
                    json_decref(status_json);
            }

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
            if (json_object_size(status_json) != 0) {
                *message = json_object();
                if (*message)
                    json_object_set_new(
                        *message, SCSI_OPERATION_TYPE_NAMES[IMPEXP_STATUS],
                        status_json);
                else
                    json_decref(status_json);
            }

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
            if (json_object_size(status_json) != 0) {
                *message = json_object();
                if (*message)
                    json_object_set_new(
                        *message, SCSI_OPERATION_TYPE_NAMES[DRIVES_STATUS],
                        status_json);
                else
                    json_decref(status_json);
            }

            LOG_RETURN(rc, "element_status failed for type 'drives'");
        }

        json_object_clear(status_json);

        if (separate_query_sn) {
            /* query drive serial separately */
            rc = query_drive_sn(lib, status_json);
            if (rc) {
                if (json_object_size(status_json) != 0) {
                    *message = json_object();
                    if (*message)
                        json_object_set_new(
                            *message, SCSI_OPERATION_TYPE_NAMES[DRIVES_STATUS],
                            status_json);
                    else
                        json_decref(status_json);
                }

                return rc;
            }
            json_object_clear(status_json);
        }

        lib->drives.loaded = true;
    }

    json_decref(status_json);
    return 0;
}

int tlc_library_open(struct lib_descriptor *lib, const char *dev,
                     json_t **json_message)
{
    int rc;

    *json_message = NULL;
    lib->fd = open(dev, O_RDWR | O_NONBLOCK);
    if (lib->fd < 0) {
        *json_message = json_pack("{s:s}", "LIB_OPEN_FAILURE", dev);
        LOG_RETURN(rc = -errno, "Failed to open '%s'", dev);
    }

    rc = lib_status_load(lib, SCSI_TYPE_ALL, json_message);
    if (rc)
        pho_error(rc, "Failed to load library status");

    return rc;
}

void tlc_library_close(struct lib_descriptor *lib)
{
    lib_status_clear(lib);
    lib_addrs_clear(lib);
    if (lib->fd >= 0) {
        close(lib->fd);
        lib->fd = 0;
    }
}

int tlc_library_refresh(struct lib_descriptor *lib, const char *dev,
                        json_t **json_message)
{
    *json_message = NULL;
    tlc_library_close(lib);
    return tlc_library_open(lib, dev, json_message);
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

struct element_status *drive_element_status_from_serial(
    struct lib_descriptor *lib, const char *serial)
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
struct element_status *media_element_status_from_label(
    struct lib_descriptor *lib, const char *label)
{
    struct element_status *med;
    int i;

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

int tlc_library_drive_lookup(struct lib_descriptor *lib,
                             const char *drive_serial,
                             struct lib_drv_info *ldi,
                             json_t **json_error_message)
{
    struct element_status *drv;

    *json_error_message = NULL;
    /* search for the given drive serial */
    drv = drive_element_status_from_serial(lib, drive_serial);
    if (!drv) {
        *json_error_message = json_pack("{s:s}",
                                        "DRIVE_SERIAL_UNKNOWN", drive_serial);
        return -ENOENT;
    }

    memset(ldi, 0, sizeof(*ldi));
    ldi->ldi_addr.lia_type = MED_LOC_DRIVE;
    ldi->ldi_addr.lia_addr = drv->address;
    ldi->ldi_first_addr = lib->msi.drives.first_addr;
    if (drv->full) {
        ldi->ldi_full = true;
        ldi->ldi_medium_id.family = PHO_RSC_TAPE;
        pho_id_name_set(&ldi->ldi_medium_id, drv->vol);
    } else {
        ldi->ldi_full = false;
    }

    return 0;
}

/*#** Convert SCSI element type to LDM media location type *#
 *static inline enum med_location scsi2ldm_loc_type(enum element_type_code type)
 *{
 *    switch (type) {
 *    case SCSI_TYPE_ARM:    return MED_LOC_ARM;
 *    case SCSI_TYPE_SLOT:   return MED_LOC_SLOT;
 *    case SCSI_TYPE_IMPEXP: return MED_LOC_IMPEXP;
 *    case SCSI_TYPE_DRIVE:  return MED_LOC_DRIVE;
 *    default:          return MED_LOC_UNKNOWN;
 *    }
 *}
 *
 *#** Implements phobos LDM lib media lookup *#
 *static int lib_scsi_media_info(struct lib_handle *hdl, const char *med_label,
 *                               struct lib_item_addr *lia, json_t *message)
 *{
 *    struct element_status *tape;
 *    int rc;
 *    ENTRY;
 *
 *    MUTEX_LOCK(&phobos_context()->ldm_lib_scsi_mutex);
 *
 *    #* load all possible tape locations *#
 *    rc = lib_status_load(hdl->lh_lib, SCSI_TYPE_ALL, message);
 *    if (rc)
 *        goto unlock;
 *
 *    #* search for the given tape *#
 *    tape = media_info_from_label(hdl->lh_lib, med_label);
 *    if (!tape)
 *        GOTO(unlock, rc = -ENOENT);
 *
 *    memset(lia, 0, sizeof(*lia));
 *    lia->lia_type = scsi2ldm_loc_type(tape->type);
 *    lia->lia_addr = tape->address;
 *
 *    rc = 0;
 *
 *unlock:
 *    MUTEX_UNLOCK(&phobos_context()->ldm_lib_scsi_mutex);
 *    return rc;
 *}
 */

/** return information about the element at the given address. */
static struct element_status *
element_from_addr(const struct lib_descriptor *lib,
                  const struct lib_item_addr *addr)
{
    int i;

    if (addr->lia_type == MED_LOC_UNKNOWN ||
        addr->lia_type == MED_LOC_DRIVE) {
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

static void move_tape_between_element_status(struct element_status *source,
                                             struct element_status *destination)
{
    source->full = false;
    source->src_addr_is_set = false;
    destination->full = true;
    destination->src_addr_is_set = true;
    destination->src_addr = source->address;
    memcpy(destination->vol, source->vol, VOL_ID_LEN);
}

static void tlc_log_init(const char *drive_serial, const char *tape_label,
                         enum operation_type operation_type,
                         struct pho_log *log, json_t **json_message)
{
    struct pho_id drive_id;
    struct pho_id tape_id;

    drive_id.family = PHO_RSC_TAPE;
    tape_id.family = PHO_RSC_TAPE;
    pho_id_name_set(&drive_id, drive_serial);
    pho_id_name_set(&tape_id, tape_label);

    init_pho_log(log, &drive_id, &tape_id, operation_type);
    log->message = json_object();
}

int tlc_library_load(struct dss_handle *dss, struct lib_descriptor *lib,
                     const char *drive_serial, const char *tape_label,
                     json_t **json_message)
{
    struct element_status *source_element_status;
    struct element_status *drive_element_status;
    struct pho_log log;
    int rc;

    *json_message = NULL;

    /* get device addr */
    drive_element_status = drive_element_status_from_serial(lib, drive_serial);
    if (!drive_element_status) {
        *json_message = json_pack("{s:s}",
                                  "DRIVE_SERIAL_UNKNOWN", drive_serial);
        return -ENOENT;
    }

    /* get medium addr */
    source_element_status = media_element_status_from_label(lib, tape_label);
    if (!source_element_status) {
        *json_message = json_pack("{s:s}",
                                  "MEDIA_LABEL_UNKNOWN", tape_label);
        return -ENOENT;
    }

    /* prepare SCSI log */
    tlc_log_init(drive_serial, tape_label, PHO_DEVICE_LOAD, &log, json_message);

    /* move medium to device */
    /* arm = 0 for default transport element */
    rc = scsi_move_medium(lib->fd, 0, source_element_status->address,
                          drive_element_status->address, log.message);
    emit_log_after_action(dss, &log, PHO_DEVICE_LOAD, rc);
    if (rc)
        LOG_RETURN(rc, "SCSI move failed for load of tape '%s' in drive '%s'",
                   tape_label, drive_serial);

    /* update element status lib cache */
    move_tape_between_element_status(source_element_status,
                                     drive_element_status);
    return 0;
}

/** Search for a free slot in the library */
static struct element_status *get_free_slot(struct lib_descriptor *lib)
{
    struct element_status *slot;
    int i;

    for (i = 0; i < lib->slots.count; i++) {
        slot = &lib->slots.items[i];

        if (!slot->full)
            return slot;
    }

    return NULL;
}

/**
 * Find a free slot from source if set or any
 *
 * @param[in]   lib             lib handle
 * @param[in]   drive           drive to unload
 * @param[out]  target          selected target to unload
 * @param[out]  unload_addr     selected addr to unload
 * @param[out]  json_message    message describing action or error
 *
 * @return 0 if success, else a negative error code
 */
static int
get_target_free_slot_from_source_or_any(struct lib_descriptor *lib,
                                        struct element_status *drive,
                                        struct element_status **target,
                                        struct lib_item_addr *unload_addr,
                                        json_t **json_message)
{
    unload_addr->lia_type = MED_LOC_UNKNOWN;
    unload_addr->lia_addr = 0;
    json_message = NULL;

    /* check drive source */
    if (drive->src_addr_is_set) {
        *target = element_from_addr(lib, unload_addr);
        if (!*target) {
            pho_error(-EADDRNOTAVAIL, "Source address '%#hx' of %s element at "
                      "address '%#hx' does not correspond to any existing "
                      "element. We will search a free slot address to move.",
                      drive->src_addr, type2str(drive->type), drive->address);
        } else if ((*target)->type != SCSI_TYPE_SLOT) {
            pho_warn("Source address of %s element at address '%#hx' "
                     "corresponds to a %s element. We do not move to a source "
                     "element different from %s. We will search a free slot "
                     "address to move.",
                     type2str(drive->type), drive->address,
                     type2str((*target)->type), type2str(SCSI_TYPE_SLOT));
        } else if (!(*target)->full) {
            /*
             * We change unload_addr->lia_type from UNKNOWN to SLOT to set we
             * find a valid slot.
             */
            unload_addr->lia_type = MED_LOC_SLOT;
            pho_debug("Using element source address '%#hx'.", drive->src_addr);
        } else {
            pho_verb("Source address '%#hx' of element %s at address '%#hx' "
                     "is full. We will search a free address to move.",
                     drive->src_addr, type2str(drive->type), drive->address);
        }
    }

    if (unload_addr->lia_type != MED_LOC_SLOT) {
        *target = get_free_slot(lib);
        if (!*target) {
            *json_message = json_pack("{s:s}",
                                      "NO_FREE_SLOT",
                                      "Unable to find a free slot to unload");
            return -ENOENT;
        }

        unload_addr->lia_type = MED_LOC_SLOT;
    }

    unload_addr->lia_addr = (uint64_t) (*target)->address;
    return 0;
}

int tlc_library_unload(struct dss_handle *dss, struct lib_descriptor *lib,
                       const char *drive_serial, const char *expected_tape,
                       char **unloaded_tape_label,
                       struct lib_item_addr *unload_addr, json_t **json_message)
{
    struct element_status *target_element_status = NULL;
    struct element_status *drive_element_status;
    struct pho_log log;
    int rc;

    unload_addr->lia_type = MED_LOC_UNKNOWN;
    unload_addr->lia_addr = 0;
    *json_message = NULL;
    *unloaded_tape_label = NULL;

    /* get device addr */
    drive_element_status = drive_element_status_from_serial(lib, drive_serial);
    if (!drive_element_status) {
        *json_message = json_pack("{s:s}",
                                  "DRIVE_UNKNOWN_SERIAL", drive_serial);
        return -ENOENT;
    }

    /* check if device is empty */
    if (drive_element_status->full == false) {
        if (expected_tape == NULL) {
            pho_verb("Was asked to unload an empty drive %s", drive_serial);
            return 0;
        } else {
            *json_message = json_pack("{s:s}",
                                      "EMPTY_DRIVE_DOES_NOT_CONTAIN",
                                      expected_tape);
            return -EINVAL;
        }
    }

    /* check or get loaded tape label */
    if (expected_tape) {
        if (strcmp(expected_tape, drive_element_status->vol)) {
            *json_message = json_pack("{s:s, s:s}",
                                      "EXPECTED_TAPE", expected_tape,
                                      "LOADED_TAPE", drive_element_status->vol);
            return -EINVAL;
        }
    }

    *unloaded_tape_label = xmalloc(sizeof(drive_element_status->vol) + 1);
    memcpy(*unloaded_tape_label, drive_element_status->vol,
           sizeof(drive_element_status->vol));
    (*unloaded_tape_label)[sizeof(drive_element_status->vol)] = 0;

    /* get target free slot from drive source or any */
    rc = get_target_free_slot_from_source_or_any(lib, drive_element_status,
                                                 &target_element_status,
                                                 unload_addr, json_message);
    if (rc)
        return rc;

    /* prepare SCSI log */
    tlc_log_init(drive_serial, *unloaded_tape_label, PHO_DEVICE_UNLOAD,
                 &log, json_message);

    /* move medium to device */
    /* arm = 0 for default transport element */
    rc = scsi_move_medium(lib->fd, 0, drive_element_status->address,
                          target_element_status->address, log.message);
    emit_log_after_action(dss, &log, PHO_DEVICE_UNLOAD, rc);
    if (rc) {
        free(*unloaded_tape_label);
        *unloaded_tape_label = NULL;
        LOG_RETURN(rc,
                   "SCSI move failed for unload of tape '%s' in drive '%s' to "
                   "address %#hx",
                   drive_element_status->vol, drive_serial,
                   target_element_status->address);
    }

    /* update element status lib cache */
    move_tape_between_element_status(drive_element_status,
                                     target_element_status);
    return 0;
}

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
    if (element->accessible)
        json_insert_element(root, "accessible", json_true());

    /* Inverted media is uncommon enough so that it can be omitted if false */
    if (element->invert)
        json_insert_element(root, "invert", json_true());

    scan_cb(udata, root);
    json_decref(root);
}

int tlc_library_status(struct lib_descriptor *lib, json_t **lib_data,
                       json_t **json_message)
{
    lib_scan_cb_t json_array_append_cb;
    int i = 0;

    *json_message = NULL;

    *lib_data = json_array();
    if (*lib_data == NULL) {
        *json_message = json_pack("{s:s}",
                                 "ALLOC_LIB_DATA",
                                 "TLC library was unable to build lib_data "
                                 "array");
        return -ENOMEM;
    }

    json_array_append_cb = (lib_scan_cb_t)json_array_append;

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

    return 0;
}
