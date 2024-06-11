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

struct lib_descriptor {
    struct pho_comm_info tlc_comm;  /**< TLC Communication socket info. */
};

static int lib_scsi_open(struct lib_handle *hdl)
{
    union pho_comm_addr tlc_sock_addr;
    struct lib_descriptor *lib;
    int rc = 0;
    ENTRY;

    MUTEX_LOCK(&phobos_context()->ldm_lib_scsi_mutex);
    lib = xcalloc(1, sizeof(struct lib_descriptor));
    hdl->lh_lib = lib;

    /* TLC client connection */
    tlc_sock_addr.tcp.hostname = PHO_CFG_GET(cfg_lib_scsi, PHO_CFG_LIB_SCSI,
                                             tlc_hostname);
    tlc_sock_addr.tcp.port = PHO_CFG_GET_INT(cfg_lib_scsi, PHO_CFG_LIB_SCSI,
                                             tlc_port, 0);
    if (tlc_sock_addr.tcp.port == 0)
        LOG_GOTO(free_lib, rc = -EINVAL,
                 "Unable to get a valid integer TLC port value");

    if (tlc_sock_addr.tcp.port > 65536)
        LOG_GOTO(free_lib, rc = -EINVAL,
                 "TLC port value %d can not be greater than 65536",
                 tlc_sock_addr.tcp.port);

    rc = pho_comm_open(&lib->tlc_comm, &tlc_sock_addr, PHO_COMM_TCP_CLIENT);
    if (rc)
        LOG_GOTO(free_lib, rc, "Cannot contact 'TLC': will abort");

    goto unlock;

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
        pho_id_name_set(&ldi->ldi_medium_id, resp->drive_lookup->medium_name,
                        hdl->library);
    }

    rc = 0;

free_resp:
    pho_srl_tlc_response_free(resp, true);
    return rc;
}

static int lib_tlc_scan(struct lib_handle *hdl, bool refresh, json_t **lib_data,
                        json_t *message)
{
    struct lib_descriptor *lib;
    json_error_t json_error;
    pho_tlc_resp_t *resp;
    pho_tlc_req_t req;
    int rid = 1;
    int rc;

    lib = hdl->lh_lib;
    if (!lib) /* closed or missing init */
        return -EBADF;

    pho_srl_tlc_request_status_alloc(&req);
    req.id = rid;
    req.status->refresh = refresh;
    rc = tlc_send_recv(&lib->tlc_comm, &req, &resp);
    pho_srl_tlc_request_free(&req, false);
    if (rc)
        LOG_RETURN(rc, "Unable to send/recv status request to tlc");

    /* manage tlc status response */
    if (pho_tlc_response_is_error(resp) && resp->req_id == rid) {
        rc = resp->error->rc;
        if (resp->error->message)
            LOG_GOTO(free_resp, rc,
                     "TLC status failed: '%s'", resp->error->message);
        else
            LOG_GOTO(free_resp, rc, "TLC status failed");
    } else if (!(pho_tlc_response_is_status(resp) && resp->req_id == rid)) {
        LOG_GOTO(free_resp, rc = -EPROTO,
                 "TLC answered an unexpected response (id %d) to status "
                 "request", resp->req_id);
    }

    *lib_data = json_loads(resp->status->lib_data, 0, &json_error);
    if (!*lib_data)
        LOG_GOTO(free_resp, rc = -EPROTO,
                 "Received lib_data seems invalid (%s): '%s'",
                 json_error.text, resp->status->lib_data);

    rc = 0;

free_resp:
    pho_srl_tlc_response_free(resp, true);
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

    /* manage tlc unload response */
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

static int lib_tlc_refresh(struct lib_handle *hdl)
{
    struct lib_descriptor *lib;
    pho_tlc_resp_t *resp;
    pho_tlc_req_t req;
    int rid = 1;
    int rc;

    lib = hdl->lh_lib;

    /* refresh request to the tlc */
    pho_srl_tlc_request_refresh_alloc(&req);
    req.id = rid;

    rc = tlc_send_recv(&lib->tlc_comm, &req, &resp);
    pho_srl_tlc_request_free(&req, false);
    if (rc)
        LOG_RETURN(rc, "Unable to send/recv refresh request");

    /* manage tlc refresh response */
    if (pho_tlc_response_is_error(resp) && resp->req_id == rid) {
        rc = resp->error->rc;
        if (resp->error->message)
            LOG_GOTO(free_resp, rc,
                     "TLC failed to refresh: '%s'", resp->error->message);
        else
            LOG_GOTO(free_resp, rc,
                     "TLC failed to refresh");
    } else if (!(pho_tlc_response_is_refresh(resp) && resp->req_id == rid)) {
        LOG_GOTO(free_resp, rc = -EPROTO,
                 "TLC answered an unexpected response (id %d) to refresh",
                 resp->req_id);
    }

    pho_debug("Successful refresh of TLC");

free_resp:
    pho_srl_tlc_response_free(resp, true);
    return rc;
}

static int lib_tlc_ping(struct lib_handle *hdl, bool *library_is_up)
{
    struct lib_descriptor *lib;
    pho_tlc_resp_t *resp;
    pho_tlc_req_t req;
    int rid = 1;
    int rc;

    lib = hdl->lh_lib;

    /* refresh request to the tlc */
    pho_srl_tlc_request_ping_alloc(&req);
    req.id = rid;

    rc = tlc_send_recv(&lib->tlc_comm, &req, &resp);
    pho_srl_tlc_request_free(&req, false);
    if (rc)
        LOG_RETURN(rc, "Unable to send/recv ping request");

    /* manage tlc ping response */
    if (pho_tlc_response_is_error(resp) && resp->req_id == rid) {
        rc = resp->error->rc;
        if (resp->error->message)
            LOG_GOTO(free_resp, rc,
                     "Failed to ping TLC: '%s'", resp->error->message);
        else
            LOG_GOTO(free_resp, rc, "Failed to ping TLC");
    } else if (!(pho_tlc_response_is_ping(resp) && resp->req_id == rid)) {
        LOG_GOTO(free_resp, rc = -EPROTO,
                 "TLC answered an unexpected response (id %d) to ping",
                 resp->req_id);
    }

    *library_is_up = resp->ping->library_is_up;
    if (*library_is_up)
        pho_debug("Successful ping of TLC");
    else
        pho_debug("TLC cannot contact (or communicate with) tape library");

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
    .lib_scan         = lib_tlc_scan,
    .lib_load         = lib_tlc_load,
    .lib_unload       = lib_tlc_unload,
    .lib_refresh      = lib_tlc_refresh,
    .lib_ping         = lib_tlc_ping,
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
