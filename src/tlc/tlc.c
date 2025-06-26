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
 * \brief  TLC main interface -- Tape Library Controller
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "pho_cfg.h"
#include "pho_comm.h"
#include "pho_common.h"
#include "pho_daemon.h"
#include "pho_dss.h"
#include "pho_ldm.h"
#include "pho_srl_tlc.h"
#include "pho_types.h"
#include "pho_type_utils.h"
#include "scsi_api.h"

#include "tlc_cfg.h"
#include "tlc_library.h"

static bool should_tlc_stop(void)
{
    return !running;
}

struct tlc {
    struct pho_comm_info comm;  /*!< Communication handle */
    struct lib_descriptor lib;  /*!< Library descriptor */
    struct dss_handle dss;      /*!< DSS handle, configured from conf */
};

static int tlc_init(struct tlc *tlc, const char *library)
{
    union pho_comm_addr sock_addr = {0};
    json_t *json_message;
    const char *lib_dev;
    size_t len_library;
    int rc;

    if (!library)
        library = PHO_CFG_GET(cfg_tlc, PHO_CFG_TLC, default_library);

    if (!library)
        LOG_RETURN(-EINVAL, "No default tape library defined in config");

    len_library = strlen(library);
    if (len_library > PHO_URI_MAX)
        LOG_RETURN(-EINVAL, "library name '%s' is too long (> %d)",
                   library, PHO_URI_MAX);

    memcpy(tlc->lib.name, library, len_library);
    tlc->lib.name[len_library] = '\0';

    /* open TLC lib file descriptor and load library cache */
    rc = tlc_lib_device_from_cfg(tlc->lib.name, &lib_dev);
    if (rc)
        LOG_RETURN(-EINVAL,
                   "Failed to get TLC lib device from config for library %s",
                   tlc->lib.name);

    rc = tlc_library_open(&tlc->lib, lib_dev, &json_message);
    if (rc) {
        if (json_message) {
            char *dump = json_dumps(json_message, 0);

            pho_error(rc, "Failed to open library device '%s': %s",
                      lib_dev, dump);
            free(dump);
            json_decref(json_message);
            return rc;
        } else {
            LOG_RETURN(-errno,
                       "Failed to open library device '%s'", lib_dev);
        }
    }

    if (json_message) {
        pho_verb("Successfully open the library: %s",
                 json_dumps(json_message, 0));
        json_decref(json_message);
    }

    /* open TLC communicator */
    rc = tlc_listen_hostname_from_cfg(tlc->lib.name,
                                      &sock_addr.tcp.hostname);
    if (rc)
        LOG_GOTO(close_lib, rc,
                 "Unable to get TLC listen hostname from config for "
                 "library %s", tlc->lib.name);

    rc = tlc_listen_port_from_cfg(tlc->lib.name, &sock_addr.tcp.port);
    if (rc)
        LOG_GOTO(close_lib, rc,
                 "Unable to get TLC listen port from config for library %s",
                 tlc->lib.name);

    rc = tlc_listen_interface_from_cfg(tlc->lib.name,
                                       &sock_addr.tcp.interface);
    if (rc)
        LOG_GOTO(close_lib, rc,
                 "Unable to get TLC listen interface from config for library %s",
                 tlc->lib.name);

    rc = pho_comm_open(&tlc->comm, &sock_addr, PHO_COMM_TCP_SERVER);
    if (rc)
        LOG_GOTO(close_lib, rc, "Error while opening the TLC socket");

    /* DSS handle init */
    rc = dss_init(&tlc->dss);
    if (rc)
        LOG_GOTO(close_lib, rc, "Cannot initialize DSS");

    return rc;

close_lib:
    tlc_library_close(&tlc->lib);
    return rc;
}

static void tlc_fini(struct tlc *tlc)
{
    int rc;

    ENTRY;

    if (tlc == NULL)
        return;

    rc = pho_comm_close(&tlc->comm);
    if (rc)
        pho_error(rc, "Error on closing the TLC socket");

    tlc_library_close(&tlc->lib);

    dss_fini(&tlc->dss);
}

/**
 * Send a response message
 *
 * @param[in]   resp            response message to send
 * @param[in]   client_socket   socket fd on which the response message must be
 *                              sent
 *
 * @return 0 on success, else a negative error code
 */
static int tlc_response_send(pho_tlc_resp_t *resp, int client_socket)
{
    struct pho_comm_data msg;
    int rc;

    pho_srl_tlc_response_pack(resp, &msg.buf);

    msg.fd = client_socket;
    rc = pho_comm_send(&msg);
    if (rc)
        pho_error(rc, "TLC error on sending response");

    free(msg.buf.buff);
    return rc;
}

static int process_ping_request(struct tlc *tlc, pho_tlc_req_t *req,
                                 int client_socket)
{
    pho_tlc_resp_t resp;
    int rc;

    pho_srl_tlc_response_ping_alloc(&resp);

    resp.req_id = req->id;

    rc = scsi_inquiry(tlc->lib.fd);
    if (rc)
        resp.ping->library_is_up = false;
    else
        resp.ping->library_is_up = true;

    rc = tlc_response_send(&resp, client_socket);
    pho_srl_tlc_response_free(&resp, false);
    return rc;
}

/**
 * Allocate and fill an error response
 *
 * @param[in, out]  error_response  error response to alloc and fill
 * @param[in]       id              request id
 * @param[in]       rc              return code
 * @param[in]       json_message    Dumped to the error response if not NULL,
 *                                  ignored if NULL.
 */
static void tlc_build_response_error(pho_tlc_resp_t *error_resp, uint32_t id,
                                     int rc, json_t *json_message)
{
    pho_srl_tlc_response_error_alloc(error_resp);
    error_resp->req_id = id;
    error_resp->error->rc = rc;
    if (json_message)
        error_resp->error->message = json_dumps(json_message, 0);
}

static int process_drive_lookup_request(struct tlc *tlc, pho_tlc_req_t *req,
                                        int client_socket)
{
    pho_tlc_resp_t drive_lookup_resp;
    struct lib_drv_info drv_info;
    pho_tlc_resp_t *resp = NULL;
    json_t *json_error_message;
    pho_tlc_resp_t error_resp;
    int rc, rc2;

    rc = tlc_library_drive_lookup(&tlc->lib, req->drive_lookup->serial,
                                  &drv_info, &json_error_message);
    if (rc)
        goto err;

    pho_srl_tlc_response_drive_lookup_alloc(&drive_lookup_resp);

    drive_lookup_resp.req_id = req->id;
    drive_lookup_resp.drive_lookup->address = drv_info.ldi_addr.lia_addr;
    drive_lookup_resp.drive_lookup->first_address = drv_info.ldi_first_addr;
    drive_lookup_resp.drive_lookup->medium_name = NULL;
    if (drv_info.ldi_full) {
        drive_lookup_resp.drive_lookup->medium_name =
            xstrdup(drv_info.ldi_medium_id.name);
    }

    resp = &drive_lookup_resp;

err:
    if (rc) {
        tlc_build_response_error(&error_resp, req->id, rc, json_error_message);
        if (json_error_message)
            json_decref(json_error_message);

        resp = &error_resp;
    }

    rc2 = tlc_response_send(resp, client_socket);
    if (rc2)
        rc = rc ? : rc2;

    pho_srl_tlc_response_free(resp, false);
    return rc;
}

static int process_load_request(struct tlc *tlc, pho_tlc_req_t *req,
                                int client_socket)
{
    json_t *json_message = NULL;
    pho_tlc_resp_t *resp = NULL;
    pho_tlc_resp_t error_resp;
    pho_tlc_resp_t load_resp;
    int rc, rc2;

    rc = tlc_library_load(&tlc->dss, &tlc->lib, req->load->drive_serial,
                          req->load->tape_label, &json_message);
    if (rc) {
        tlc_build_response_error(&error_resp, req->id, rc, json_message);
        if (json_message)
            json_decref(json_message);

        resp = &error_resp;
    } else {
        /* Build load response */
        pho_srl_tlc_response_load_alloc(&load_resp);
        load_resp.req_id = req->id;
        if (json_message) {
            load_resp.load->message = json_dumps(json_message, 0);
            json_decref(json_message);
        }

        resp = &load_resp;
    }

    rc2 = tlc_response_send(resp, client_socket);
    if (rc2)
        rc = rc ? : rc2;

    pho_srl_tlc_response_free(resp, false);
    return rc;
}

static int process_unload_request(struct tlc *tlc, pho_tlc_req_t *req,
                                  int client_socket)
{
    struct lib_item_addr unload_addr;
    json_t *json_message = NULL;
    pho_tlc_resp_t *resp = NULL;
    pho_tlc_resp_t unload_resp;
    pho_tlc_resp_t error_resp;
    char *unloaded_tape_label;
    int rc, rc2;

    rc = tlc_library_unload(&tlc->dss, &tlc->lib, req->unload->drive_serial,
                            req->unload->tape_label, &unloaded_tape_label,
                            &unload_addr, &json_message);
    if (rc) {
        tlc_build_response_error(&error_resp, req->id, rc, json_message);
        if (json_message)
            json_decref(json_message);

        resp = &error_resp;
    } else {
        /* Build unload response */
        pho_srl_tlc_response_unload_alloc(&unload_resp);
        if (unloaded_tape_label)
            unload_resp.unload->tape_label = xstrdup(unloaded_tape_label);

        free(unloaded_tape_label);
        unload_resp.unload->addr = unload_addr.lia_addr;
        unload_resp.req_id = req->id;
        if (json_message) {
            unload_resp.unload->message = json_dumps(json_message, 0);
            json_decref(json_message);
        }

        resp = &unload_resp;
    }

    rc2 = tlc_response_send(resp, client_socket);
    if (rc2)
        rc = rc ? : rc2;

    pho_srl_tlc_response_free(resp, false);
    return rc;
}

static int process_status_request(struct tlc *tlc, pho_tlc_req_t *req,
                                  int client_socket)
{
    char *string_lib_data = NULL;
    json_t *json_message = NULL;
    pho_tlc_resp_t *resp = NULL;
    bool refresh_failed = false;
    pho_tlc_resp_t status_resp;
    pho_tlc_resp_t error_resp;
    json_t *json_lib_data;
    int rc, rc2;

    if (req->status->refresh) {
        const char *lib_dev;

        rc = tlc_lib_device_from_cfg(tlc->lib.name, &lib_dev);
        if (rc) {
            pho_error(rc,
                      "Failed to get default library device from config to "
                      "refresh for library %s", tlc->lib.name);
            json_message = json_pack("{s:s}", "LIB_DEV_CONF_ERROR",
                                     "Failed to get default library device "
                                     "from config to refresh");
            refresh_failed = true;
            goto error_response;
        }

        rc = tlc_library_refresh(&tlc->lib, lib_dev, &json_message);
        if (rc) {
            refresh_failed = true;
            goto error_response;
        }

        if (json_message)
            json_decref(json_message);
    }

    rc = tlc_library_status(&tlc->lib, &json_lib_data, &json_message);
    if (!rc) {
        string_lib_data = json_dumps(json_lib_data, JSON_COMPACT);
        json_decref(json_lib_data);
        if (!string_lib_data) {
            if (json_message)
                json_decref(json_message);

            json_message = json_pack("{s:s}",
                                     "TLC_LIB_DATA_DUMP_ERROR",
                                     "TLC was unable to dump lib data to "
                                     "response");
            rc = -ENOMEM;
        }
    }

    if (rc) {
error_response:
        tlc_build_response_error(&error_resp, req->id, rc, json_message);
        if (json_message)
            json_decref(json_message);

        resp = &error_resp;
    } else {
        /* Build status response */
        pho_srl_tlc_response_status_alloc(&status_resp);
        status_resp.status->lib_data = string_lib_data;
        status_resp.req_id = req->id;
        if (json_message) {
            status_resp.status->message = json_dumps(json_message, 0);
            json_decref(json_message);
        }

        resp = &status_resp;
    }

    rc2 = tlc_response_send(resp, client_socket);
    if (rc2)
        rc = rc ? : rc2;

    pho_srl_tlc_response_free(resp, false);

    if (refresh_failed) {
        pho_error(rc, "On refresh failure, without any valid library cache, "
                      "TLC commits suicide");
        tlc_fini(tlc);
        exit(EXIT_FAILURE);
    }

    return rc;
}

static int process_refresh_request(struct tlc *tlc, pho_tlc_req_t *req,
                                   int client_socket)
{
    json_t *json_message = NULL;
    pho_tlc_resp_t *resp = NULL;
    pho_tlc_resp_t refresh_resp;
    pho_tlc_resp_t error_resp;
    const char *lib_dev;
    int rc, rc2;

    rc = tlc_lib_device_from_cfg(tlc->lib.name, &lib_dev);
    if (rc) {
        pho_error(rc,
                  "Failed to get default library device from config to refresh "
                  "for library %s", tlc->lib.name);
        json_message = json_pack("{s:s}", "LIB_DEV_CONF_ERROR",
                                 "Failed to get default library device from "
                                 "config to refresh");
        goto error_response;
    }

    rc = tlc_library_refresh(&tlc->lib, lib_dev, &json_message);
    if (rc) {
error_response:
        tlc_build_response_error(&error_resp, req->id, rc, json_message);
        if (json_message)
            json_decref(json_message);

        resp = &error_resp;
    } else {
        /* Build load response */
        pho_srl_tlc_response_refresh_alloc(&refresh_resp);
        refresh_resp.req_id = req->id;
        if (json_message)
            json_decref(json_message);

        resp = &refresh_resp;
    }

    rc2 = tlc_response_send(resp, client_socket);
    if (rc2)
        rc = rc ? : rc2;

    pho_srl_tlc_response_free(resp, false);

    if (rc) {
        pho_error(rc, "On refresh failure, without any valid library cache, "
                      "TLC commits suicide");
        tlc_fini(tlc);
        exit(EXIT_FAILURE);
    }

    return rc;
}

static int recv_work(struct tlc *tlc)
{
    struct pho_comm_data *data = NULL;
    int n_data;
    int rc, i;

    rc = pho_comm_recv(&tlc->comm, &data, &n_data);
    if (rc) {
        for (i = 0; i < n_data; ++i)
            free(data[i].buf.buff);

        free(data);
        LOG_RETURN(rc, "TLC error on reading input data");
    }

    for (i = 0; i < n_data; i++) {
        pho_tlc_req_t *req;

        if (data[i].buf.size == -1) /* close notification, ignore */
            continue;

        req = pho_srl_tlc_request_unpack(&data[i].buf);
        if (!req)
            continue;

        if (pho_tlc_request_is_ping(req)) {
            process_ping_request(tlc, req, data[i].fd);
            goto out_request;
        }

        if (pho_tlc_request_is_drive_lookup(req)) {
            process_drive_lookup_request(tlc, req, data[i].fd);
            goto out_request;
        }

        if (pho_tlc_request_is_load(req)) {
            process_load_request(tlc, req, data[i].fd);
            goto out_request;
        }

        if (pho_tlc_request_is_unload(req)) {
            process_unload_request(tlc, req, data[i].fd);
            goto out_request;
        }

        if (pho_tlc_request_is_status(req)) {
            process_status_request(tlc, req, data[i].fd);
            goto out_request;
        }

        if (pho_tlc_request_is_refresh(req)) {
            process_refresh_request(tlc, req, data[i].fd);
            goto out_request;
        }

out_request:
        pho_srl_tlc_request_free(req, true);
    }

    free(data);

    return rc;
}

int main(int argc, char **argv)
{
    int write_pipe_from_child_to_father;
    struct daemon_params param;
    struct tlc tlc = {};
    int rc;

    rc = daemon_creation(argc, argv, &param, &write_pipe_from_child_to_father,
                         "tlc");
    if (rc)
        return -rc;

    rc = daemon_init(param);

    if (!rc)
        rc = tlc_init(&tlc, param.library);

    if (param.is_daemon)
        daemon_notify_init_done(write_pipe_from_child_to_father, &rc);

    if (rc)
        return -rc;

    while (true) {
        if (should_tlc_stop())
            break;

        /* recv_work waits on input sockets */
        rc = recv_work(&tlc);
        if (rc) {
            pho_error(rc, "TLC error when receiving requests");
            break;
        }
    }

    tlc_fini(&tlc);
    return EXIT_SUCCESS;
}
