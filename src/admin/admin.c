/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2020 CEA/DAM.
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
 * \brief  Phobos Administration interface
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "phobos_admin.h"

#include <unistd.h>

#include "pho_cfg.h"
#include "pho_comm.h"
#include "pho_dss.h"
#include "pho_srl_lrs.h"

enum pho_cfg_params_admin {
    /* Actual admin parameters */
    PHO_CFG_ADMIN_lrs_socket,

    /* Delimiters, update when modifying options */
    PHO_CFG_ADMIN_FIRST = PHO_CFG_ADMIN_lrs_socket,
    PHO_CFG_ADMIN_LAST  = PHO_CFG_ADMIN_lrs_socket
};

const struct pho_config_item cfg_admin[] = {
    [PHO_CFG_ADMIN_lrs_socket] = {
        .section = "lrs",
        .name    = "server_socket",
        .value   = "/tmp/socklrs"
    },
};

/* ****************************************************************************/
/* Static Functions ***********************************************************/
/* ****************************************************************************/

static int _send_and_receive(struct admin_handle *adm, pho_req_t *req,
                             pho_resp_t **resp)
{
    struct pho_comm_data *data_in = NULL;
    struct pho_comm_data data_out;
    int n_data_in = 0;
    int rc;

    data_out = pho_comm_data_init(&adm->comm);
    rc = pho_srl_request_pack(req, &data_out.buf);
    pho_srl_request_free(req, false);
    if (rc)
        LOG_RETURN(rc, "Cannot serialize request");

    rc = pho_comm_send(&data_out);
    free(data_out.buf.buff);
    if (rc)
        LOG_RETURN(rc, "Cannot send request to LRS");

    rc = pho_comm_recv(&adm->comm, &data_in, &n_data_in);
    if (rc || n_data_in != 1) {
        if (data_in)
            free(data_in->buf.buff);
        free(data_in);
        if (rc)
            LOG_RETURN(rc, "Cannot receive responses from LRS");
        else
            LOG_RETURN(-EINVAL, "Received %d responses (expected 1)",
                       n_data_in);
    }

    *resp = pho_srl_response_unpack(&data_in->buf);
    free(data_in);
    if (!*resp)
        LOG_RETURN(-EINVAL, "The received response cannot be deserialized");

    return 0;
}

static int _admin_notify(struct admin_handle *adm, struct pho_id *id,
                         enum notify_op op)
{
    pho_resp_t *resp;
    pho_req_t req;
    int rid = 1;
    int rc;

    if (op <= PHO_NTFY_OP_INVAL || op >= PHO_NTFY_OP_LAST)
        LOG_RETURN(-ENOTSUP, "Operation not supported");

    rc = pho_srl_request_notify_alloc(&req);
    if (rc)
        LOG_RETURN(rc, "Cannot create notify request");

    req.id = rid;
    req.notify->op = op;
    req.notify->rsrc_id->family = id->family;
    req.notify->rsrc_id->name = strdup(id->name);

    rc = _send_and_receive(adm, &req, &resp);
    if (rc)
        LOG_RETURN(rc, "Error with LRS communication");

    if (pho_response_is_notify(resp)) {
        if (resp->req_id == rid &&
            (int) id->family == (int) resp->notify->rsrc_id->family &&
            !strcmp(resp->notify->rsrc_id->name, id->name)) {
            pho_debug("Notify request succeeded");
            goto out;
        }

        LOG_GOTO(out, rc = -EINVAL, "Received response does not "
                                    "answer emitted request");
    }

    if (pho_response_is_error(resp)) {
        rc = resp->error->rc;
        LOG_GOTO(out, rc, "Received error response");
    }

    pho_error(rc = -EINVAL, "Received invalid response");

out:
    pho_srl_response_free(resp, true);
    return rc;
}

/* ****************************************************************************/
/* API Functions **************************************************************/
/* ****************************************************************************/

void phobos_admin_fini(struct admin_handle *adm)
{
    int rc;

    rc = pho_comm_close(&adm->comm);
    if (rc)
        pho_error(rc, "Cannot close the communication socket");

    dss_fini(&adm->dss);
}

int phobos_admin_init(struct admin_handle *adm, bool lrs_required)
{
    const char *sock_path;
    int rc;

    memset(adm, 0, sizeof(*adm));
    adm->comm = pho_comm_info_init();

    rc = pho_cfg_init_local(NULL);
    if (rc && rc != -EALREADY)
        return rc;

    sock_path = PHO_CFG_GET(cfg_admin, PHO_CFG_ADMIN, lrs_socket);

    rc = dss_init(&adm->dss);
    if (rc)
        LOG_GOTO(out, rc, "Cannot initialize DSS");

    rc = pho_comm_open(&adm->comm, sock_path, false);
    if (!lrs_required && rc == -ENOTCONN) {
        pho_warn("Cannot contact 'phobosd', but not required: will continue");
        rc = 0;
    } else if (rc) {
        LOG_GOTO(out, rc, "Cannot contact 'phobosd': will abort");
    } else {
        adm->daemon_is_online = true;
    }

out:
    if (rc) {
        pho_error(rc, "Error during Admin initialization");
        phobos_admin_fini(adm);
    }

    return rc;
}

/**
 * TODO: admin_device_add will have the responsability to add the device
 * to the DSS, to then remove this part of code from the CLI.
 */
int phobos_admin_device_add(struct admin_handle *adm, enum rsc_family family,
                            const char *name)
{
    struct pho_id dev_id;
    int rc;

    if (!adm->daemon_is_online)
        return 0;

    pho_id_name_set(&dev_id, name);
    dev_id.family = family;

    rc = _admin_notify(adm, &dev_id, PHO_NTFY_OP_ADD_DEVICE);
    if (rc)
        LOG_RETURN(rc, "Communication with LRS failed");

    return 0;
}

/**
 * TODO: admin_device_unlock will have the responsability to update the device
 * state, to then remove this part of code from the CLI.
 */
int phobos_admin_device_unlock(struct admin_handle *adm, struct pho_id *dev_ids,
                               int num_dev)
{
    int rc = 0;
    int i;

    if (!adm->daemon_is_online)
        return 0;

    for (i = 0; i < num_dev; ++i) {
        int rc2;

        rc2 = _admin_notify(adm, dev_ids + i, PHO_NTFY_OP_DEVICE_UNLOCK);
        if (rc2)
            pho_error(rc2, "Failure during daemon notification for '%s'",
                      dev_ids[i].name);
        rc = rc ? rc : rc2;
    }

    return rc;
}

int phobos_admin_format(struct admin_handle *adm, const struct pho_id *id,
                        enum fs_type fs, bool unlock)
{
    pho_resp_t *resp;
    pho_req_t req;
    int rid = 1;
    int rc;

    rc = pho_srl_request_format_alloc(&req);
    if (rc)
        LOG_RETURN(rc, "Cannot create format request");

    req.id = rid;
    req.format->fs = fs;
    req.format->unlock = unlock;
    req.format->med_id->family = id->family;
    req.format->med_id->name = strdup(id->name);

    rc = _send_and_receive(adm, &req, &resp);
    if (rc)
        LOG_RETURN(rc, "Error with LRS communication");

    if (pho_response_is_format(resp)) {
        if (resp->req_id == rid &&
            (int)resp->format->med_id->family == (int)id->family &&
            !strcmp(resp->format->med_id->name, id->name)) {
            pho_debug("Format request succeeded");
            goto out;
        }

        LOG_GOTO(out, rc = -EINVAL, "Received response does not "
                                    "answer emitted request");
    }

    if (pho_response_is_error(resp)) {
        rc = resp->error->rc;
        LOG_GOTO(out, rc, "Received error response");
    }

    pho_error(rc = -EINVAL, "Received invalid response");

out:
    pho_srl_response_free(resp, true);
    return rc;
}
