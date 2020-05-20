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

#include <sys/syscall.h>
#include <unistd.h>

#include "pho_cfg.h"
#include "pho_comm.h"
#include "pho_common.h"
#include "pho_dss.h"
#include "pho_srl_lrs.h"
#include "pho_type_utils.h"

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
/* Static Communication-related Functions *************************************/
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
/* Static Database-related Functions ******************************************/
/* ****************************************************************************/

static int _get_device_by_id(struct admin_handle *adm, struct pho_id *dev_id,
                             struct dev_info **dev_res)
{
    struct dss_filter filter;
    int dcnt;
    int rc;

    rc = dss_filter_build(&filter,
                          "{\"$AND\": ["
                          "  {\"DSS::DEV::family\": \"%s\"},"
                          "  {\"$OR\": ["
                          "    {\"DSS::DEV::serial\": \"%s\"},"
                          "    {\"DSS::DEV::path\": \"%s\"}"
                          "  ]}"
                          "]}",
                          rsc_family2str(dev_id->family),
                          dev_id->name, dev_id->name);
    if (rc)
        return rc;

    rc = dss_device_get(&adm->dss, &filter, dev_res, &dcnt);
    dss_filter_free(&filter);
    if (rc)
        return rc;

    if (dcnt == 0)
        LOG_RETURN(-ENXIO, "Device '%s' not found", dev_id->name);

    assert(dcnt == 1);

    return 0;
}

static int _device_update_adm_status(struct admin_handle *adm,
                                     struct pho_id *dev_ids, int num_dev,
                                     enum rsc_adm_status status, bool is_forced)
{
    struct dev_info *devices;
    int avail_devices = 0;
    int rc;
    int i;

    devices = calloc(num_dev, sizeof(*devices));
    if (!devices)
        LOG_RETURN(-ENOMEM, "Device info allocation failed");

    for (i = 0; i < num_dev; ++i) {
        struct dev_info *dev_res;

        rc = _get_device_by_id(adm, dev_ids + i, &dev_res);
        if (rc)
            goto out_free;

        rc = dss_device_lock(&adm->dss, dev_res, 1, adm->lock_owner);
        if (rc) {
            pho_error(-EBUSY, "Device '%s' is in use by '%s'", dev_ids[i].name,
                      dev_res->lock.lock);
            dss_res_free(dev_res, 1);
            continue;
        }

        dss_res_free(dev_res, 1);
        rc = _get_device_by_id(adm, dev_ids + i, &dev_res);
        if (rc)
            goto out_free;

        /* TODO: to uncomment once the dss_device_lock() is removed ie. when
         * the update of a single database field will be implemented
         */

/*      if (strcmp(dev_res->lock.lock, "")) {
 *          if (!is_forced) {
 *              pho_error(-EBUSY, "Device '%s' is in use by '%s'",
 *                        dev_ids[i].name, dev_res->lock.lock);
 *              dss_res_free(dev_res, 1);
 *              continue;
 *          } else if (status == PHO_RSC_ADM_ST_LOCKED) {
 *              pho_warn("Device '%s' is in use. Administrative locking will "
 *                       "not be effective immediately", dev_res->rsc.id.name);
 *          }
 *      }
 */
        if (dev_res->rsc.adm_status == status)
            pho_warn("Device '%s' is already in the desired state",
                     dev_ids[i].name);

        dev_res->rsc.adm_status = status;
        dev_info_cpy(devices + i, dev_res);

        dss_res_free(dev_res, 1);
        ++avail_devices;
    }

    if (avail_devices != num_dev)
        LOG_GOTO(out_free, rc = -EBUSY,
                 "At least one device is in use, use --force");

    rc = dss_device_set(&adm->dss, devices, num_dev, DSS_SET_UPDATE);
    if (rc)
        goto out_free;

    // in case the name given by the user is not the device ID name
    for (i = 0; i < num_dev; ++i)
        if (strcmp(dev_ids[i].name, devices[i].rsc.id.name))
            strcpy(dev_ids[i].name, devices[i].rsc.id.name);

out_free:
    dss_device_unlock(&adm->dss, devices, num_dev, adm->lock_owner);

    for (i = 0; i < num_dev; ++i)
        dev_info_free(devices + i, false);
    free(devices);

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

    free(adm->lock_owner);

    dss_fini(&adm->dss);
}

static __thread uint64_t adm_lock_number;

int phobos_admin_init(struct admin_handle *adm, bool lrs_required)
{
    const char *sock_path;
    int rc;

    memset(adm, 0, sizeof(*adm));
    adm->comm = pho_comm_info_init();

    rc = asprintf(&adm->lock_owner, "%.213s:%.8lx:%.16lx:%.16lx",
                  get_hostname(), syscall(SYS_gettid), time(NULL),
                  adm_lock_number);

    if (rc == -1)
        LOG_GOTO(out, rc, "Cannot allocate lock_owner");

    ++adm_lock_number;

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

int phobos_admin_device_lock(struct admin_handle *adm, struct pho_id *dev_ids,
                             int num_dev, bool is_forced)
{
    int rc;
    int i;

    rc = _device_update_adm_status(adm, dev_ids, num_dev,
                                   PHO_RSC_ADM_ST_LOCKED, is_forced);
    if (rc)
        return rc;

    if (!adm->daemon_is_online)
        return 0;

    for (i = 0; i < num_dev; ++i) {
        int rc2;

        rc2 = _admin_notify(adm, dev_ids + i, PHO_NTFY_OP_DEVICE_LOCK);
        if (rc2)
            pho_error(rc2, "Failure during daemon notification for '%s'",
                      dev_ids[i].name);
        rc = rc ? : rc2;
    }

    return rc;
}

int phobos_admin_device_unlock(struct admin_handle *adm, struct pho_id *dev_ids,
                               int num_dev, bool is_forced)
{
    int rc;
    int i;

    rc = _device_update_adm_status(adm, dev_ids, num_dev,
                                   PHO_RSC_ADM_ST_UNLOCKED, is_forced);
    if (rc)
        return rc;

    if (!adm->daemon_is_online)
        return 0;

    for (i = 0; i < num_dev; ++i) {
        int rc2;

        rc2 = _admin_notify(adm, dev_ids + i, PHO_NTFY_OP_DEVICE_UNLOCK);
        if (rc2)
            pho_error(rc2, "Failure during daemon notification for '%s'",
                      dev_ids[i].name);
        rc = rc ? : rc2;
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

int phobos_admin_extent_list(struct admin_handle *adm, const char *pattern,
                             struct layout_info **objs, int *n_objs)
{
    struct dss_filter filter;
    int rc;

    rc = dss_filter_build(&filter, "{\"$REGEXP\": {\"DSS::EXT::oid\": \"%s\"}}",
                          pattern);
    if (rc)
        return rc;

    rc = dss_layout_get(&adm->dss, &filter, objs, n_objs);
    if (rc)
        pho_error(rc, "Cannot fetch extents");

    dss_filter_free(&filter);

    return rc;
}

void phobos_admin_list_free(void *objs, const int n_objs)
{
    dss_res_free(objs, n_objs);
}
