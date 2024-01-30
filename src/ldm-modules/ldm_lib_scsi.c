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

#include "pho_cfg.h"
#include "pho_comm.h"
#include "pho_common.h"
#include "pho_ldm.h"
#include "pho_module_loader.h"
#include "pho_srl_tlc.h"

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
    PHO_CFG_LIB_SCSI_tlc_hostname,
    PHO_CFG_LIB_SCSI_tlc_port,

    /* Delimiters, update when modifying options */
    PHO_CFG_LIB_SCSI_FIRST = PHO_CFG_LIB_SCSI_sep_sn_query,
    PHO_CFG_LIB_SCSI_LAST  = PHO_CFG_LIB_SCSI_tlc_port,
};

/** Definition and default values of SCSI library configuration parameters */
const struct pho_config_item cfg_lib_scsi[] = {
    [PHO_CFG_LIB_SCSI_sep_sn_query] = {
        .section = "lib_scsi",
        .name    = "sep_sn_query",
        .value   = "0", /* no */
    },
    [PHO_CFG_LIB_SCSI_tlc_hostname] = TLC_HOSTNAME_CFG_ITEM,
    [PHO_CFG_LIB_SCSI_tlc_port] = TLC_PORT_CFG_ITEM,
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

    struct pho_comm_info tlc_comm;  /**< TLC Communication socket info. */
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

    json_decref(lib_load_json);
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

    json_decref(status_json);

    return 0;
}

static int lib_scsi_open(struct lib_handle *hdl, const char *dev,
                         json_t *message)
{
    union pho_comm_addr tlc_sock_addr;
    struct lib_descriptor *lib;
    int rc = 0;
    ENTRY;

    MUTEX_LOCK(&phobos_context()->ldm_lib_scsi_mutex);

    lib = xcalloc(1, sizeof(struct lib_descriptor));

    hdl->lh_lib = lib;

    lib->fd = open(dev, O_RDWR | O_NONBLOCK);
    if (lib->fd < 0) {
        rc = -errno;
        json_insert_element(message, "Action",
                            json_string("Open device controller"));
        json_insert_element(message, "Error",
                            json_string("Failed to open device controller"));
        LOG_GOTO(free_lib, rc, "Failed to open '%s'", dev);
    }

    /* TLC client connection */
    tlc_sock_addr.tcp.hostname = PHO_CFG_GET(cfg_lib_scsi, PHO_CFG_LIB_SCSI,
                                             tlc_hostname);
    tlc_sock_addr.tcp.port = PHO_CFG_GET_INT(cfg_lib_scsi, PHO_CFG_LIB_SCSI,
                                             tlc_port, 0);
    if (tlc_sock_addr.tcp.port == 0)
        LOG_GOTO(close_fd, rc = -EINVAL,
                 "Unable to get a valid integer TLC port value");

    if (tlc_sock_addr.tcp.port > 65536)
        LOG_GOTO(close_fd, rc = -EINVAL,
                 "TLC port value %d can not be greater than 65536",
                 tlc_sock_addr.tcp.port);

    rc = pho_comm_open(&lib->tlc_comm, &tlc_sock_addr, PHO_COMM_TCP_CLIENT);
    if (rc)
        LOG_GOTO(close_fd, rc, "Cannot contact 'TLC': will abort");

    goto unlock;

close_fd:
    close(lib->fd);
    lib->fd = -1;
free_lib:
    free(lib);
    hdl->lh_lib = NULL;
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

    rc = pho_comm_close(&lib->tlc_comm);
    if (rc)
        pho_error(rc, "Cannot close the TLC communication socket");

    free(lib);
    hdl->lh_lib = NULL;
    rc = 0;

unlock:
    MUTEX_UNLOCK(&phobos_context()->ldm_lib_scsi_mutex);
    return rc;
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
static int tlc_send_recv(struct pho_comm_info *tlc_comm, pho_tlc_req_t *req,
                         pho_tlc_resp_t **resp)
{
    struct pho_comm_data *data_resp = NULL;
    struct pho_comm_data data;
    int n_data_resp = 0;
    int rc;

    data = pho_comm_data_init(tlc_comm);
    pho_srl_tlc_request_pack(req, &data.buf);

    rc = pho_comm_send(&data);
    free(data.buf.buff);
    if (rc)
        LOG_RETURN(rc, "Error while sending request to TLC");

    rc = pho_comm_recv(tlc_comm, &data_resp, &n_data_resp);
    if (rc || n_data_resp != 1) {
        int i;

        for (i = 0; i < n_data_resp; i++)
                free(data_resp[i].buf.buff);

        free(data_resp);

        if (rc)
            LOG_RETURN(rc, "Cannot receive response from TLC");
        else
            LOG_RETURN(-EINVAL, "Received %d responses (expected 1) from TLC",
                       n_data_resp);
    }

    *resp = pho_srl_tlc_response_unpack(&data_resp->buf);
    free(data_resp);
    if (!*resp)
        LOG_RETURN(-EINVAL, "The received TLC response cannot be deserialized");

    return 0;
}

static int lib_tlc_drive_info(struct lib_handle *hdl, const char *drv_serial,
                              struct lib_drv_info *ldi)
{
    struct lib_descriptor *lib = hdl->lh_lib;
    pho_tlc_resp_t *resp;
    pho_tlc_req_t req;
    int rid = 1;
    int rc;

    ENTRY;

    /* drive lookup request to the tlc */
    pho_srl_tlc_request_drive_lookup_alloc(&req);
    req.id = rid;
    req.drive_lookup->serial = xstrdup(drv_serial);
    rc = tlc_send_recv(&lib->tlc_comm, &req, &resp);
    pho_srl_tlc_request_free(&req, false);
    if (rc)
        LOG_RETURN(rc,
                   "Unable to send/recv drive lookup request for drive '%s' to "
                   "tlc", drv_serial);

    /* manage tlc drive lookup response */
    if (pho_tlc_response_is_error(resp) && resp->req_id == rid) {
        rc = resp->error->rc;
        if (resp->error->message)
            LOG_GOTO(free_resp, rc,
                     "TLC failed to lookup the drive '%s': '%s'", drv_serial,
                     resp->error->message);
        else
            LOG_GOTO(free_resp, rc,
                     "TLC failed to lookup the drive '%s'", drv_serial);
    } else if (!(pho_tlc_response_is_drive_lookup(resp) &&
                 resp->req_id == rid)) {
        LOG_GOTO(free_resp, rc = -EPROTO,
                 "TLC answered an unexpected response (id %d) to drive lookup "
                 "load request for drive '%s'", resp->req_id, drv_serial);
    }

    /* update drive info */
    memset(ldi, 0, sizeof(*ldi));
    ldi->ldi_addr.lia_type = MED_LOC_DRIVE;
    ldi->ldi_addr.lia_addr = resp->drive_lookup->address;
    ldi->ldi_first_addr = resp->drive_lookup->first_address;
    if (resp->drive_lookup->medium_name) {
        ldi->ldi_full = true;
        ldi->ldi_medium_id.family = PHO_RSC_TAPE;
        pho_id_name_set(&ldi->ldi_medium_id, resp->drive_lookup->medium_name);
    }

    rc = 0;

free_resp:
    pho_srl_tlc_response_free(resp, true);
    return rc;
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

static int lib_tlc_load(struct lib_handle *hdl, const char *drive_serial,
                        const char *tape_label)
{
    struct lib_descriptor *lib;
    pho_tlc_resp_t *resp;
    pho_tlc_req_t req;
    int rid = 1;
    int rc;

    lib = hdl->lh_lib;

    /* load request to the tlc */
    pho_srl_tlc_request_load_alloc(&req);
    req.id = rid;
    req.load->drive_serial = xstrdup(drive_serial);
    req.load->tape_label = xstrdup(tape_label);

    rc = tlc_send_recv(&lib->tlc_comm, &req, &resp);
    pho_srl_tlc_request_free(&req, false);
    if (rc)
        LOG_RETURN(rc,
                   "Unable to send/recv load request for drive '%s' (tape "
                   "'%s') to tlc", drive_serial, tape_label);

    /* manage tlc load response */
    if (pho_tlc_response_is_error(resp) && resp->req_id == rid) {
        rc = resp->error->rc;
        if (resp->error->message)
            LOG_GOTO(free_resp, rc,
                     "TLC failed to load: '%s'", resp->error->message);
        else
            LOG_GOTO(free_resp, rc, "TLC failed to load: '%s'", tape_label);
    } else if (!(pho_tlc_response_is_load(resp) && resp->req_id == rid)) {
        LOG_GOTO(free_resp, rc = -EPROTO,
                 "TLC answered an unexpected response (id %d) to load request "
                 "for drive '%s' (tape '%s')",
                 resp->req_id, drive_serial, tape_label);
    }

    pho_debug("Successful load of '%s' into '%s'", tape_label, drive_serial);

free_resp:
    pho_srl_tlc_response_free(resp, true);
    return rc;
}

static int lib_tlc_unload(struct lib_handle *hdl, const char *drive_serial,
                          const char *tape_label)
{
    struct lib_descriptor *lib;
    pho_tlc_resp_t *resp;
    pho_tlc_req_t req;
    int rid = 1;
    int rc;

    lib = hdl->lh_lib;

    /* unload request to the tlc */
    pho_srl_tlc_request_unload_alloc(&req);
    req.id = rid;
    req.unload->drive_serial = xstrdup(drive_serial);
    req.unload->tape_label = xstrdup_safe(tape_label);

    rc = tlc_send_recv(&lib->tlc_comm, &req, &resp);
    pho_srl_tlc_request_free(&req, false);
    if (rc)
        LOG_RETURN(rc, "Unable to send/recv unload request for drive '%s'",
                   drive_serial);

    /* manage tlc load response */
    if (pho_tlc_response_is_error(resp) && resp->req_id == rid) {
        rc = resp->error->rc;
        if (resp->error->message)
            LOG_GOTO(free_resp, rc,
                     "TLC failed to unload: '%s'", resp->error->message);
        else
            LOG_GOTO(free_resp, rc,
                     "TLC failed to unload drive '%s'", drive_serial);
    } else if (!(pho_tlc_response_is_unload(resp) && resp->req_id == rid)) {
        LOG_GOTO(free_resp, rc = -EPROTO,
                 "TLC answered an unexpected response (id %d) to unload "
                 "request for drive '%s'", resp->req_id, drive_serial);
    }

    if (tape_label)
        pho_debug("Successful unload of '%s' from '%s'",
                  tape_label, drive_serial);
    else
        pho_debug("Successful unload of drive '%s'", drive_serial);

free_resp:
    pho_srl_tlc_response_free(resp, true);
    return rc;
}

/** @}*/

/** lib_scsi_adapter exported to upper layers */
static struct pho_lib_adapter_module_ops LA_SCSI_OPS = {
    .lib_open         = lib_scsi_open,
    .lib_close        = lib_scsi_close,
    .lib_drive_lookup = lib_tlc_drive_info,
    .lib_scan         = lib_scsi_scan,
    .lib_load         = lib_tlc_load,
    .lib_unload       = lib_tlc_unload,
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
