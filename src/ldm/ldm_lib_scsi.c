/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2017 CEA/DAM.
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

#include "pho_ldm.h"
#include "scsi_api.h"
#include "pho_common.h"
#include "pho_cfg.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <jansson.h>
#include <unistd.h>

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
static int lib_addrs_load(struct lib_descriptor *lib)
{
    int rc;

    /* msi is already loaded */
    if (lib->msi_loaded)
        return 0;

    if (lib->fd < 0)
        return -EBADF;

    rc = scsi_mode_sense(lib->fd, &lib->msi);
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
static int query_drive_sn(struct lib_descriptor *lib)
{
    struct element_status   *items = NULL;
    int                      count = 0;
    int                      i;
    int                      rc;

    /* query for drive serial number */
    rc = scsi_element_status(lib->fd, SCSI_TYPE_DRIVE,
                             lib->msi.drives.first_addr, lib->msi.drives.nb,
                             ESF_GET_DRV_ID, &items, &count);
    if (rc)
        LOG_RETURN(rc, "scsi_element_status() failed to get drive S/N");

    if (count != lib->drives.count) {
        free(items);
        LOG_RETURN(rc, "Wrong drive count returned by scsi_element_status()");
    }

    /* copy serial number to the library array (should be already allocated) */
    assert(lib->drives.items != NULL);

    for (i = 0; i < count; i++)
        strncpy(lib->drives.items[i].dev_id, items[i].dev_id,
                sizeof(lib->drives.items[i].dev_id) - 1);
    lib->drives.items[i].dev_id[sizeof(lib->drives.items[i].dev_id) - 1] = '\0';

    free(items);
    return 0;
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
        rc = scsi_element_status(lib->fd, SCSI_TYPE_ARM,
                                 lib->msi.arms.first_addr, lib->msi.arms.nb,
                                 ESF_GET_LABEL, /* to check if the arm holds
                                                   a tape */
                                 &lib->arms.items, &lib->arms.count);
        if (rc)
            LOG_RETURN(rc, "element_status failed for type 'arms'");

        lib->arms.loaded = true;
    }

    if ((type == SCSI_TYPE_ALL || type == SCSI_TYPE_SLOT)
        && !lib->slots.loaded) {
        rc = scsi_element_status(lib->fd, SCSI_TYPE_SLOT,
                                 lib->msi.slots.first_addr, lib->msi.slots.nb,
                                 ESF_GET_LABEL,
                                 &lib->slots.items, &lib->slots.count);
        if (rc)
            LOG_RETURN(rc, "element_status failed for type 'slots'");

        lib->slots.loaded = true;
    }

    if ((type == SCSI_TYPE_ALL || type == SCSI_TYPE_IMPEXP)
        && !lib->impexp.loaded) {
        rc = scsi_element_status(lib->fd, SCSI_TYPE_IMPEXP,
                                 lib->msi.impexp.first_addr, lib->msi.impexp.nb,
                                 ESF_GET_LABEL,
                                 &lib->impexp.items, &lib->impexp.count);
        if (rc)
            LOG_RETURN(rc, "element_status failed for type 'impexp'");

        lib->impexp.loaded = true;
    }

    if ((type == SCSI_TYPE_ALL || type == SCSI_TYPE_DRIVE)
        && !lib->drives.loaded) {
        enum elem_status_flags   flags;
        bool                     separate_query_sn;

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
                                 flags, &lib->drives.items, &lib->drives.count);
        if (rc)
            LOG_RETURN(rc, "element_status failed for type 'drives'");

        if (separate_query_sn) {
            /* query drive serial separately */
            rc = query_drive_sn(lib);
            if (rc)
                return rc;
        }

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
                              uint16_t *tgt_addr, bool *to_origin)
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
    if (*to_origin && element->src_addr_is_set) {

        /* check the slot is not already full */
        struct lib_item_addr slot_lia = {
            .lia_type = MED_LOC_SLOT,
            .lia_addr = element->src_addr,
        };
        const struct element_status *slot;

        slot = element_from_addr(lib, &slot_lia);
        if (!slot->full) {
            *tgt_addr = element->src_addr;
            pho_debug("No target address specified. "
                      "Using element source address %#hx.", *tgt_addr);
            return 0;
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
                         const struct lib_item_addr *tgt_addr)
{
    struct lib_descriptor *lib;
    uint16_t tgt;
    bool origin = false;
    int rc;
    ENTRY;

    lib = hdl->lh_lib;
    if (!lib) /* already closed */
        return -EBADF;

    if (tgt_addr == NULL
        || (tgt_addr->lia_type == MED_LOC_UNKNOWN
            && tgt_addr->lia_addr == 0)) {
        /* First try source slot. If not valid, try any free slot */
        origin = true;
        rc = select_target_addr(lib, src_addr, &tgt, &origin);
        if (rc)
            return rc;
    } else {
        tgt = tgt_addr->lia_addr;
    }

    /* arm = 0 for default transport element */
    rc = scsi_move_medium(lib->fd, 0, src_addr->lia_addr, tgt);

    /* was the source slot invalid? */
    if (rc == -EINVAL && origin) {
        pho_warn("Failed to move media to source slot, trying another one...");
        origin = false;
        rc = select_target_addr(lib, src_addr, &tgt, &origin);
        if (rc)
            return rc;
        rc = scsi_move_medium(lib->fd, 0, src_addr->lia_addr, tgt);
    }
    return rc;
}


/**
 *  \defgroup lib scan (those items are related to lib_scan implementation)
 *  @{
 */

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
 * Type for a scan callback function.
 *
 * The first argument is the private data of the callback, the second a json_t
 * object representing the lib element that has just been scanned.
 */
typedef void (*lib_scan_cb_t)(void *, json_t *);

#define JSON_OBJECT_SET_NEW(_json, _field, _json_type, ...)                  \
    do {                                                                     \
        json_t *_json_value = _json_type(__VA_ARGS__);                       \
        if (!_json_value)                                                    \
            pho_error(-EINVAL, "Failed to encode " #__VA_ARGS__ " as json"); \
        if (json_object_set_new(_json, _field, _json_value) != 0)            \
            pho_error(-ENOMEM, "Failed to set " _field " in json");          \
    } while (0)

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

    JSON_OBJECT_SET_NEW(root, "type", json_string, type2str(element->type));
    JSON_OBJECT_SET_NEW(root, "address", json_integer, element->address);

    if (element->type & (SCSI_TYPE_ARM | SCSI_TYPE_DRIVE | SCSI_TYPE_SLOT))
        JSON_OBJECT_SET_NEW(root, "full", json_boolean, element->full);

    if (element->full && element->vol[0])
        JSON_OBJECT_SET_NEW(root, "volume", json_string, element->vol);

    if (element->src_addr_is_set)
        JSON_OBJECT_SET_NEW(root, "source_address",
                            json_integer, element->src_addr);

    if (element->except) {
        JSON_OBJECT_SET_NEW(root, "error_code",
                            json_integer, element->error_code);
        JSON_OBJECT_SET_NEW(root, "error_code_qualifier",
                            json_integer, element->error_code_qualifier);
    }

    if (element->dev_id[0])
        JSON_OBJECT_SET_NEW(root, "device_id", json_string, element->dev_id);

    if (element->type == SCSI_TYPE_IMPEXP) {
        JSON_OBJECT_SET_NEW(root, "current_operation",
                            json_string, element->impexp ? "import" : "export");
        JSON_OBJECT_SET_NEW(root, "exp_enabled",
                            json_boolean, element->exp_enabled);
        JSON_OBJECT_SET_NEW(root, "imp_enabled",
                            json_boolean, element->imp_enabled);
    }

    /* Make "accessible" appear only when it is true */
    if (element->accessible) {
        JSON_OBJECT_SET_NEW(root, "accessible", json_true);
    }

    /* Inverted media is uncommon enough so that it can be omitted if false */
    if (element->invert) {
        JSON_OBJECT_SET_NEW(root, "invert", json_true);
    }

    scan_cb(udata, root);
    json_decref(root);
}

/** Implements phobos LDM lib scan  */
static int lib_scsi_scan(struct lib_handle *hdl, json_t **lib_data)
{
    struct lib_descriptor *lib;
    lib_scan_cb_t          json_array_append_cb;
    int                    rc, i = 0;

    json_array_append_cb = (lib_scan_cb_t)json_array_append;

    lib = hdl->lh_lib;
    if (!lib) /* closed or missing init */
        return -EBADF;

    *lib_data = json_array();

    /* Load everything */
    rc = lib_status_load(lib, SCSI_TYPE_ALL);
    if (rc)
        LOG_RETURN(rc, "Error loading scsi library status");

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

    if (rc) {
        json_decref(*lib_data);
        *lib_data = NULL;
    }
    return rc;
}

/** @}*/

/** lib_scsi_adapter exported to upper layers */
struct lib_adapter lib_adapter_scsi = {
    .lib_open         = lib_scsi_open,
    .lib_close        = lib_scsi_close,
    .lib_drive_lookup = lib_scsi_drive_info,
    .lib_media_lookup = lib_scsi_media_info,
    .lib_media_move   = lib_scsi_move,
    .lib_scan         = lib_scsi_scan,
    .lib_hdl          = {NULL},
};
