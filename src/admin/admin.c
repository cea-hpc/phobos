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
 * \brief  Phobos Administration interface
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "phobos_admin.h"

#include <errno.h>
#include <glib.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>

#include "pho_cfg.h"
#include "pho_comm.h"
#include "pho_common.h"
#include "pho_dss.h"
#include "pho_ldm.h"
#include "pho_srl_lrs.h"
#include "pho_srl_tlc.h"
#include "pho_types.h"
#include "pho_type_utils.h"
#include "admin_utils.h"

enum pho_cfg_params_admin {
    /* Actual admin parameters */
    PHO_CFG_ADMIN_lrs_socket,
    PHO_CFG_ADMIN_tlc_hostname,
    PHO_CFG_ADMIN_tlc_port,

    /* Delimiters, update when modifying options */
    PHO_CFG_ADMIN_FIRST = PHO_CFG_ADMIN_lrs_socket,
    PHO_CFG_ADMIN_LAST  = PHO_CFG_ADMIN_tlc_port
};

const struct pho_config_item cfg_admin[] = {
    [PHO_CFG_ADMIN_lrs_socket] = LRS_SOCKET_CFG_ITEM,
    [PHO_CFG_ADMIN_tlc_hostname] = TLC_HOSTNAME_CFG_ITEM,
    [PHO_CFG_ADMIN_tlc_port] = TLC_PORT_CFG_ITEM,
};

/* ****************************************************************************/
/* Static Communication-related Functions *************************************/
/* ****************************************************************************/
static int _send(struct pho_comm_info *comm, struct proto_req proto_req)
{
    struct pho_comm_data data_out;
    int rc;

    data_out = pho_comm_data_init(comm);
    if (proto_req.type == LRS_REQUEST) {
        rc = pho_srl_request_pack(proto_req.msg.lrs_req, &data_out.buf);
        pho_srl_request_free(proto_req.msg.lrs_req, false);
        if (rc)
            LOG_RETURN(rc, "Cannot serialize LRS request");
    } else if (proto_req.type == TLC_REQUEST) {
        rc = pho_srl_tlc_request_pack(proto_req.msg.tlc_req, &data_out.buf);
        pho_srl_tlc_request_free(proto_req.msg.tlc_req, false);
        if (rc)
            LOG_RETURN(rc, "Cannot serialize TLC request");
    } else {
        LOG_RETURN(-EINVAL,
                   "Admin module must only send LRS or TLC request");
    }

    rc = pho_comm_send(&data_out);
    free(data_out.buf.buff);
    if (rc)
        LOG_RETURN(rc, "Cannot send request to %s",
                   request_type2str(proto_req.type));

    return 0;
}

static int _receive(struct pho_comm_info *comm, struct proto_resp *proto_resp)
{
    struct pho_comm_data *data_in = NULL;
    int n_data_in = 0;
    int rc;

    rc = pho_comm_recv(comm, &data_in, &n_data_in);
    if (rc || n_data_in != 1) {
        if (data_in)
            free(data_in->buf.buff);

        free(data_in);
        if (rc)
            LOG_RETURN(rc, "Cannot receive responses from %s",
                       request_type2str(proto_resp->type));
        else
            LOG_RETURN(-EINVAL, "Received %d responses (expected 1) from %s",
                       n_data_in, request_type2str(proto_resp->type));
    }

    switch (proto_resp->type) {
    case LRS_REQUEST:
        proto_resp->msg.lrs_resp = pho_srl_response_unpack(&data_in->buf);
        if (!proto_resp->msg.lrs_resp)
            LOG_GOTO(out, rc = -EINVAL,
                     "The received LRS response cannot be deserialized");
        break;
    case TLC_REQUEST:
        proto_resp->msg.tlc_resp = pho_srl_tlc_response_unpack(&data_in->buf);
        if (!proto_resp->msg.tlc_resp)
            LOG_GOTO(out, rc = -EINVAL,
                     "The received TLC response cannot be deserialized");
        break;
    default:
        LOG_GOTO(out, rc = -EINVAL,
                 "Admin module must only receive LRS or TLC response");
    }

out:
    free(data_in);
    return rc;
}

int _send_and_receive(struct pho_comm_info *comm, struct proto_req proto_req,
                      struct proto_resp *proto_resp)
{
    int rc;

    rc = _send(comm, proto_req);
    if (rc)
        return rc;

    rc = _receive(comm, proto_resp);

    return rc;
}

static int _admin_notify(struct admin_handle *adm, struct pho_id *id,
                         enum notify_op op, bool need_to_wait)
{
    struct proto_resp proto_resp = {LRS_REQUEST};
    struct proto_req proto_req = {LRS_REQUEST};
    pho_resp_t *resp = NULL;
    pho_req_t req;
    int rid = 1;
    int rc;

    proto_req.msg.lrs_req = &req;
    if (op <= PHO_NTFY_OP_INVAL || op >= PHO_NTFY_OP_LAST)
        LOG_RETURN(-ENOTSUP, "Operation not supported");

    rc = pho_srl_request_notify_alloc(&req);
    if (rc)
        LOG_RETURN(rc, "Cannot create notify request");

    req.id = rid;
    req.notify->op = op;
    req.notify->rsrc_id->family = id->family;
    req.notify->rsrc_id->name = strdup(id->name);
    req.notify->wait = need_to_wait;

    rc = _send(&adm->phobosd_comm, proto_req);
    if (rc)
        LOG_RETURN(rc, "Error with phobosd communication");

    if (!need_to_wait)
        return rc;

    rc = _receive(&adm->phobosd_comm, &proto_resp);
    if (rc)
        LOG_RETURN(rc, "Error with phobosd communication");

    resp = proto_resp.msg.lrs_resp;
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

static int _add_device_in_dss(struct admin_handle *adm, struct pho_id *dev_ids,
                              unsigned int num_dev, bool keep_locked)
{
    struct dev_adapter_module *deva;
    struct ldm_dev_state lds = {};
    enum rsc_adm_status status;
    struct dev_info *devices;
    char *path;
    int rc;
    int i;

    rc = get_dev_adapter(dev_ids[0].family, &deva);
    if (rc)
        LOG_RETURN(rc, "Cannot get device adapter");

    devices = calloc(num_dev, sizeof(*devices));
    if (!devices)
        LOG_RETURN(-errno, "Device info allocation failed");

    for (i = 0; i < num_dev; ++i) {
        struct dev_info *devi = devices + i;

        /* Pools do not have a real path */
        if (dev_ids[i].family == PHO_RSC_RADOS_POOL)
            path = strdup(dev_ids[i].name);
        else
            path = realpath(dev_ids[i].name, NULL);

        if (!path)
            LOG_GOTO(out_free, rc = -errno,
                     "Cannot get the real path of device '%s'",
                     dev_ids[i].name);

        rc = ldm_dev_query(deva, path, &lds);
        free(path);
        if (rc)
            LOG_GOTO(out_free, rc, "Failed to query device '%s'",
                     dev_ids[i].name);

        status = keep_locked ? PHO_RSC_ADM_ST_LOCKED : PHO_RSC_ADM_ST_UNLOCKED;

        devi->rsc.id.family = dev_ids[i].family;
        pho_id_name_set(&devi->rsc.id, lds.lds_serial);
        devi->rsc.adm_status = status;
        rc = get_allocated_hostname(&devi->host);
        if (rc)
            LOG_GOTO(out_free, rc, "Failed to retrieve hostname");

        if (lds.lds_model) {
            devi->rsc.model = strdup(lds.lds_model);
            if (!devi->rsc.model)
                LOG_GOTO(out_free, rc = -errno, "Allocation failed");
        }

        devi->path = strdup(dev_ids[i].name);
        if (!devi->path)
            LOG_GOTO(out_free, rc = -errno, "Allocation failed");

        ldm_dev_state_fini(&lds);

        pho_info("Will add device '%s:%s' to the database: "
                 "model=%s serial=%s (%s)", rsc_family2str(devi->rsc.id.family),
                 dev_ids[i].name, devi->rsc.model, devi->rsc.id.name,
                 rsc_adm_status2str(devi->rsc.adm_status));

        /* in case the name given by the user is not the device ID name */
        if (strcmp(dev_ids[i].name, devi->rsc.id.name))
            strcpy(dev_ids[i].name, devi->rsc.id.name);
    }

    rc = dss_device_insert(&adm->dss, devices, num_dev);
    if (rc)
        LOG_GOTO(out_free, rc, "Cannot add devices");


out_free:
    for (i = 0; i < num_dev; ++i) {
        free(devices[i].host);
        free(devices[i].rsc.model);
        free(devices[i].path);
    }
    free(devices);

    return rc;
}

/**
 * At input dev_id.name could refer to dev_res->path or dev_res->rsc.id.name.
 * At output, dev_id->name is set to dev_res->rsc.id.name
 *
 * dev_res must be freed using dss_res_free().
 */
static int _get_device_by_path_or_serial(struct admin_handle *adm,
                                         struct pho_id *dev_id,
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

    /* If needed, set the input dev_id->name to the serial rsc.id.name */
    if (strcmp(dev_id->name, (*dev_res)->rsc.id.name))
        strcpy(dev_id->name, (*dev_res)->rsc.id.name);

    return 0;
}

static int _device_update_adm_status(struct admin_handle *adm,
                                     struct pho_id *dev_ids, int num_dev,
                                     enum rsc_adm_status status, bool is_forced)
{
    const char *hostname = get_hostname();
    bool one_device_not_avail = false;
    struct dev_info *devices;
    int avail_devices = 0;
    int rc;
    int i;

    devices = calloc(num_dev, sizeof(*devices));
    if (!devices)
        LOG_RETURN(-ENOMEM, "Device info allocation failed");

    for (i = 0; i < num_dev; ++i) {
        struct dev_info *dev_res;

        rc = _get_device_by_path_or_serial(adm, dev_ids + i, &dev_res);
        if (rc)
            goto out_free;

        if (dev_res->rsc.adm_status == status) {
            pho_warn("Device (path: '%s', name: '%s') is already in the "
                     "desired state", dev_res->path, dev_res->rsc.id.name);
            dss_res_free(dev_res, 1);
            continue;
        }

        if (strcmp(dev_res->host, hostname) != 0) {
            if (dev_res->lock.hostname != NULL) {
                pho_error(-EBUSY, "Device (path: '%s', name: '%s') is used by "
                                  "a distant daemon (host: '%s')",
                          dev_res->path, dev_res->rsc.id.name, dev_res->host);
                one_device_not_avail = true;
                dss_res_free(dev_res, 1);
                continue;
            }
        }

        if (dev_res->lock.hostname != NULL) {
            if (!is_forced) {
                pho_error(-EBUSY,
                          "Device (path: '%s', name: '%s') is in use by "
                          "'%s':%d",
                          dev_res->path, dev_res->rsc.id.name,
                          dev_res->lock.hostname, dev_res->lock.owner);
                one_device_not_avail = true;
                dss_res_free(dev_res, 1);
                continue;
            } else if (status == PHO_RSC_ADM_ST_LOCKED) {
                pho_warn("Device (path: '%s', name: '%s') is in use. "
                         "Administrative locking will not be effective "
                         "immediately", dev_res->path, dev_res->rsc.id.name);
            }
        }

        dev_res->rsc.adm_status = status;
        rc = dev_info_cpy(&devices[avail_devices], dev_res);
        dss_res_free(dev_res, 1);
        if (rc)
            LOG_GOTO(out_free, rc, "Couldn't copy device data");

        avail_devices++;
    }

    if (one_device_not_avail)
        LOG_GOTO(out_free, rc = -EBUSY,
                 "At least one device is in use and cannot be notified");

    if (avail_devices) {
        rc = dss_device_update_adm_status(&adm->dss, devices, avail_devices);
        if (rc)
            goto out_free;
    }

out_free:
    for (i = 0; i < avail_devices; ++i)
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

    rc = pho_comm_close(&adm->phobosd_comm);
    if (rc)
        pho_error(rc, "Cannot close the LRS communication socket");

    rc = pho_comm_close(&adm->tlc_comm);
    if (rc)
        pho_error(rc, "Cannot close the TLC communication socket");

    dss_fini(&adm->dss);
}

int phobos_admin_init(struct admin_handle *adm, bool lrs_required,
                      bool tlc_required, void *phobos_context_handle)
{
    union pho_comm_addr lrs_sock_addr;
    union pho_comm_addr tlc_sock_addr;
    int rc;

    if (phobos_context_handle)
        phobos_module_context_set(
            (struct phobos_global_context *)phobos_context_handle);

    memset(adm, 0, sizeof(*adm));
    adm->phobosd_comm = pho_comm_info_init();
    adm->tlc_comm = pho_comm_info_init();

    rc = pho_cfg_init_local(NULL);
    if (rc && rc != -EALREADY)
        return rc;

    rc = dss_init(&adm->dss);
    if (rc)
        LOG_GOTO(out, rc, "Cannot initialize DSS");

    /* LRS client connection */
    lrs_sock_addr.af_unix.path = PHO_CFG_GET(cfg_admin, PHO_CFG_ADMIN,
                                             lrs_socket);
    rc = pho_comm_open(&adm->phobosd_comm, &lrs_sock_addr,
                       PHO_COMM_UNIX_CLIENT);
    if (rc && lrs_required)
        LOG_GOTO(out, rc, "Cannot contact 'phobosd': will abort");
    else if (!rc)
        adm->phobosd_is_online = true;

    if (!lrs_required)
        rc = 0;

    /* TLC client connection */
    if (tlc_required) {
        tlc_sock_addr.tcp.hostname = PHO_CFG_GET(cfg_admin, PHO_CFG_ADMIN,
                                                 tlc_hostname);
        tlc_sock_addr.tcp.port = PHO_CFG_GET_INT(cfg_admin, PHO_CFG_ADMIN,
                                                 tlc_port, 0);
        if (tlc_sock_addr.tcp.port == 0)
            LOG_GOTO(out, rc = -EINVAL,
                     "Unable to get a valid integer TLC port value");

        if (tlc_sock_addr.tcp.port > 65536)
            LOG_GOTO(out, rc = -EINVAL,
                     "TLC port value %d can not be greater than 65536",
                     tlc_sock_addr.tcp.port);

        rc = pho_comm_open(&adm->tlc_comm, &tlc_sock_addr, PHO_COMM_TCP_CLIENT);
        if (rc)
            LOG_GOTO(out, rc, "Cannot contact 'TLC': will abort");
    }

out:
    if (rc) {
        pho_error(rc, "Error during Admin initialization");
        phobos_admin_fini(adm);
    }

    return rc;
}

int phobos_admin_device_add(struct admin_handle *adm, struct pho_id *dev_ids,
                            unsigned int num_dev, bool keep_locked)
{
    int rc;
    int i;

    if (!num_dev)
        LOG_RETURN(-EINVAL, "No device were given");

    rc = _add_device_in_dss(adm, dev_ids, num_dev, keep_locked);
    if (rc)
        return rc;

    if (keep_locked)
        // do not need to inform the daemon because it does not
        // consider locked devices
        return 0;

    if (!adm->phobosd_is_online)
        return 0;

    for (i = 0; i < num_dev; ++i) {
        int rc2;

        rc2 = _admin_notify(adm, dev_ids + i, PHO_NTFY_OP_DEVICE_ADD, true);
        if (rc2)
            pho_error(rc2, "Failure during daemon notification for '%s'",
                      dev_ids[i].name);
        rc = rc ? : rc2;
    }

    return rc;
}

static json_t *build_config_item(const char *section,
                                 const char *key,
                                 const char *value)
{
    json_t *config_item = NULL;
    int rc;

    config_item = json_object();
    if (!config_item)
        return NULL;

    rc = json_object_set(config_item, "section", json_string(section));
    if (rc)
        goto free_conf;

    rc = json_object_set(config_item, "key", json_string(key));
    if (rc)
        goto free_conf;

    if (value) {
        rc = json_object_set(config_item, "value", json_string(value));
        if (rc)
            goto free_conf;
    }

    return config_item;

free_conf:
    json_decref(config_item);

    return NULL;
}

static int send_configure(struct admin_handle *adm,
                          enum configure_op op,
                          char *configuration,
                          const char **res)
{
    struct proto_resp proto_resp = {LRS_REQUEST};
    struct proto_req proto_req = {LRS_REQUEST};
    pho_resp_t *resp;
    pho_req_t req;
    int rc;

    proto_req.msg.lrs_req = &req;
    rc = pho_srl_request_configure_alloc(&req);
    if (rc)
        LOG_RETURN(rc, "Cannot create configure request");

    req.id = 1;
    req.configure->op = op;
    req.configure->configuration = configuration;

    rc = _send(&adm->phobosd_comm, proto_req);
    if (rc)
        LOG_GOTO(free_req, rc, "Failed to send configure request to phobosd");

    rc = _receive(&adm->phobosd_comm, &proto_resp);
    if (rc)
        LOG_GOTO(free_req, rc,
                 "Failed to receive configure response from phobosd");

    resp = proto_resp.msg.lrs_resp;
    if (pho_response_is_error(resp)) {
        rc = resp->error->rc;
        pho_error(rc, "Received error response to configure request");
    } else if (pho_response_is_configure(resp)) {
        if (op == PHO_CONF_OP_GET && !resp->configure->configuration) {
            rc = -EPROTO;
            pho_error(rc, "Received empty configure response");
        }

        rc = 0;
        if (res)
            *res = strdup(resp->configure->configuration);

        if (res && !*res)
            rc = -errno;
    } else if (resp->req_id != req.id) {
        rc = -EINVAL;
        pho_error(rc, "Received response does not answer emitted request");
    } else {
        rc = -EINVAL;
        pho_error(rc, "Invalid response to configure request");
    }

    pho_srl_response_free(resp, true);
free_req:
    pho_srl_request_free(&req, false);

    return rc;
}

static int build_configuration_query(const char **sections,
                                     const char **keys,
                                     const char **values,
                                     size_t n,
                                     json_t **configuration)
{
    int rc = 0;
    size_t i;

    *configuration = json_array();
    if (!*configuration)
        return -errno;

    for (i = 0; i < n; i++) {
        json_t *config_item;

        config_item = build_config_item(sections[i], keys[i],
                                        values ? values[i] : NULL);
        if (!config_item)
            GOTO(free_config, rc = -errno);

        rc = json_array_append(*configuration, config_item);
        if (rc == -1)
            GOTO(free_config, rc = -errno);
    }

    return 0;

free_config:
    json_decref(*configuration);

    return rc;
}

int phobos_admin_sched_conf_get(struct admin_handle *adm,
                                const char **sections,
                                const char **keys,
                                const char **values,
                                size_t n)
{
    const char *res = NULL;
    json_t *configuration;
    json_error_t error;
    json_t *result;
    json_t *value;
    size_t index;
    int rc = 0;

    rc = build_configuration_query(sections, keys, NULL, n, &configuration);
    if (rc)
        return rc;

    rc = send_configure(adm, PHO_CONF_OP_GET,
                        /* the result of json_dumps will be stored in
                         * the notify request. Therefore, it will be
                         * freed when the request is, no need to check
                         * or free the result here.
                         */
                        json_dumps(configuration, JSON_COMPACT),
                        &res);
    if (rc)
        GOTO(free_configuration, rc = -EINVAL);

    result = json_loads(res, JSON_REJECT_DUPLICATES, &error);
    if (!result)
        LOG_GOTO(free_configuration, rc = -EINVAL,
                 "Failed to parse JSON result '%s': %s",
                 res, error.text);

    if (!json_is_array(result))
        LOG_GOTO(free_result, rc = -EINVAL,
                 "Result '%s' is not an array",
                 res);

    if (json_array_size(result) != n)
        LOG_GOTO(free_result, rc = -ERANGE,
                 "Expected an array of size %lu, got %lu",
                 n, json_array_size(result));

    json_array_foreach(result, index, value) {
        values[index] = NULL;

        if (!json_is_string(value)) {
            pho_warn("Invalid type in '%s' at index %lu", res, index);
            continue;
        }

        values[index] = strdup(json_string_value(value));
        if (!values[index]) {
            for (; index >= 0; index--)
                free((void *)values[index]);

            break;
        }
    }

free_result:
    json_decref(result);
free_configuration:
    json_decref(configuration);

    return rc;
}

int phobos_admin_sched_conf_set(struct admin_handle *adm,
                                const char **sections,
                                const char **keys,
                                const char **values,
                                size_t n)
{
    json_t *configuration;
    int rc = 0;

    rc = build_configuration_query(sections, keys, values, n, &configuration);
    if (rc)
        return rc;

    rc = send_configure(adm, PHO_CONF_OP_SET,
                        /* the result of json_dumps will be stored in
                         * the notify request. Therefore, it will be
                         * freed when the request is, no need to check
                         * or free the result here.
                         */
                        json_dumps(configuration, JSON_COMPACT),
                        NULL);

    json_decref(configuration);

    return rc;
}

int phobos_admin_device_delete(struct admin_handle *adm, struct pho_id *dev_ids,
                               int num_dev, int *num_removed_dev)
{
    struct dev_info *devices;
    struct dev_info *dev_res;
    int avail_devices = 0;
    int rc;
    int i;

    *num_removed_dev = 0;

    devices = calloc(num_dev, sizeof(*devices));
    if (!devices)
        LOG_RETURN(-ENOMEM, "Device info allocation failed");

    for (i = 0; i < num_dev; ++i) {
        rc = _get_device_by_path_or_serial(adm, dev_ids + i, &dev_res);
        if (rc)
            goto out_free;

        rc = dss_lock(&adm->dss, DSS_DEVICE, dev_res, 1);
        if (rc) {
            pho_warn("Device '%s' cannot be locked, so cannot be removed",
                     dev_ids[i].name);
            dss_res_free(dev_res, 1);
            continue;
        }

        rc = dev_info_cpy(&devices[avail_devices], dev_res);
        if (rc)
            LOG_GOTO(out_free, rc, "Couldn't copy device data");

        dss_res_free(dev_res, 1);
        avail_devices++;
    }

    if (avail_devices == 0)
        LOG_GOTO(out_free, rc = -ENODEV,
                 "There are no available devices to remove");

    rc = dss_device_delete(&adm->dss, devices, avail_devices);
    if (rc)
        pho_error(rc, "Devices cannot be removed");

    *num_removed_dev = avail_devices;

out_free:
    // In case an error occured when copying device information
    if (i != num_dev) {
        dss_unlock(&adm->dss, DSS_DEVICE, dev_res, 1, false);
        dss_res_free(dev_res, 1);
    }

    dss_unlock(&adm->dss, DSS_DEVICE, devices, avail_devices, false);
    for (i = 0; i < avail_devices; ++i)
        dev_info_free(devices + i, false);
    free(devices);

    return rc;
}

int phobos_admin_device_lock(struct admin_handle *adm, struct pho_id *dev_ids,
                             int num_dev, bool need_to_wait)
{
    int rc = 0;
    int i;

    rc = _device_update_adm_status(adm, dev_ids, num_dev,
                                   PHO_RSC_ADM_ST_LOCKED, true);
    if (!adm->phobosd_is_online || rc)
        return rc;

    for (i = 0; i < num_dev; ++i) {
        int rc2;

        rc2 = _admin_notify(adm, dev_ids + i, PHO_NTFY_OP_DEVICE_LOCK,
                            need_to_wait);
        if (rc2) {
            pho_error(rc2, "Failure during daemon notification for '%s'",
                      dev_ids[i].name);
            rc = rc ? : rc2;
        }
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

    if (!adm->phobosd_is_online)
        return 0;

    for (i = 0; i < num_dev; ++i) {
        int rc2;

        rc2 = _admin_notify(adm, dev_ids + i, PHO_NTFY_OP_DEVICE_UNLOCK, true);
        if (rc2)
            pho_error(rc2, "Failure during daemon notification for '%s'",
                      dev_ids[i].name);
        rc = rc ? : rc2;
    }

    return rc;
}

int phobos_admin_device_status(struct admin_handle *adm,
                               enum rsc_family family,
                               char **status)
{
    struct proto_resp proto_resp = {LRS_REQUEST};
    struct proto_req proto_req = {LRS_REQUEST};
    pho_resp_t *resp = NULL;
    pho_req_t req;
    int rc;

    proto_req.msg.lrs_req = &req;
    if (family < 0 || family >= PHO_RSC_LAST)
        LOG_RETURN(-EINVAL, "Invalid family %d", family);

    rc = pho_srl_request_monitor_alloc(&req);
    if (rc)
        LOG_RETURN(rc, "Failed to allocate monitor request");

    req.id = 0;
    req.monitor->family = family;

    rc = _send_and_receive(&adm->phobosd_comm, proto_req, &proto_resp);
    if (rc)
        return rc;

    resp = proto_resp.msg.lrs_resp;
    if (pho_response_is_monitor(resp)) {
        *status = strdup(resp->monitor->status);
        if (!*status)
            rc = -ENOMEM;
    } else if (pho_response_is_error(resp)) {
        rc = resp->error->rc;
    } else {
        rc = -EPROTO;
    }

    pho_srl_response_free(resp, true);

    return rc;
}

int phobos_admin_drive_migrate(struct admin_handle *adm, struct pho_id *dev_ids,
                               unsigned int num_dev, const char *host,
                               unsigned int *num_migrated_dev)
{
    struct dev_info *devices;
    struct dev_info *dev_res;
    char *host_cpy = NULL;
    int avail_devices = 0;
    int rc = 0;
    int rc2;
    int i;

    *num_migrated_dev = 0;

    rc = strdup_safe(&host_cpy, host);
    if (rc)
        LOG_RETURN(rc, "Couldn't copy host name");

    devices = calloc(num_dev, sizeof(*devices));
    if (!devices) {
        free(host_cpy);
        LOG_RETURN(-ENOMEM, "Device info allocation failed");
    }

    for (i = 0; i < num_dev; ++i) {
        char *old_host;

        rc2 = _get_device_by_path_or_serial(adm, dev_ids + i, &dev_res);
        if (rc2) {
            rc = rc ? : rc2;
            continue;
        }

        if (strcmp(host_cpy, dev_res->host) == 0) {
            pho_info("Device '%s' is already located on '%s'", dev_ids[i].name,
                                                               host_cpy);
            dss_res_free(dev_res, 1);
            continue;
        }

        rc2 = dss_lock(&adm->dss, DSS_DEVICE, dev_res, 1);
        if (rc2) {
            pho_error(rc2,
                      "Device '%s' cannot be locked, so cannot be migrated",
                      dev_ids[i].name);
            dss_res_free(dev_res, 1);
            rc = rc ? : (rc2 == -EEXIST ? -EBUSY : rc2);
            continue;
        }

        old_host = dev_res->host;
        dev_res->host = host_cpy;
        rc2 = dev_info_cpy(&devices[avail_devices], dev_res);
        dev_res->host = old_host;
        if (rc2)
            LOG_GOTO(out_free, rc2, "Couldn't copy device data");

        dss_res_free(dev_res, 1);
        avail_devices++;
    }

    if (avail_devices == 0)
        LOG_GOTO(out_free, rc, "There are no available devices to migrate");

    rc2 = dss_device_update_host(&adm->dss, devices, avail_devices);
    if (rc2)
        pho_error(rc2, "Failed to migrate devices");
    else
        *num_migrated_dev = avail_devices;

out_free:
    // In case an error occured when copying device information
    if (i != num_dev) {
        dss_unlock(&adm->dss, DSS_DEVICE, dev_res, 1, false);
        dss_res_free(dev_res, 1);
    }

    dss_unlock(&adm->dss, DSS_DEVICE, devices, avail_devices, false);
    for (i = 0; i < avail_devices; ++i)
        dev_info_free(devices + i, false);
    free(devices);
    free(host_cpy);

    return rc;
}

static int receive_format_response(struct admin_handle *adm,
                                   const struct pho_id *ids,
                                   bool *awaiting_resps,
                                   int n_ids)
{
    struct proto_resp proto_resp = {LRS_REQUEST};
    pho_resp_t *resp;
    int rc = 0;

    rc = _receive(&adm->phobosd_comm, &proto_resp);
    if (rc)
        return rc;

    resp = proto_resp.msg.lrs_resp;
    if (pho_response_is_format(resp)) {
        if (awaiting_resps[resp->req_id] == false)
            LOG_GOTO(out, -EPROTO,
                     "Received unexpected answer for id %d (corresponds to "
                     "medium '%s')", resp->req_id, ids[resp->req_id].name);
        else if (resp->req_id < 0 || resp->req_id >= n_ids)
            LOG_GOTO(out, rc = -EPROTO,
                     "Received response does not match any emitted request "
                     "(invalid req_id %d, only sent %d requests)",
                     resp->req_id, n_ids);

        awaiting_resps[resp->req_id] = false;

        if (resp->req_id < n_ids) {
            int resp_family = (int) resp->format->med_id->family;
            char *resp_name = resp->format->med_id->name;

            if (resp_family == ids[resp->req_id].family &&
                !strcmp(resp_name, ids[resp->req_id].name))
                pho_debug("Format request for medium '%s' succeeded",
                          resp_name);
            else
                pho_error(rc = -EPROTO,
                          "Received response does not answer emitted request, "
                          "expected '%s' (%s), "
                          "got '%s' (%s) (invalid req_id %d)",
                          ids[resp->req_id].name,
                          fs_type2str(ids[resp->req_id].family),
                          resp_name,
                          fs_type2str(resp_family),
                          resp->req_id);
        }
    } else if (pho_response_is_error(resp) &&
               resp->error->req_kind == PHO_REQUEST_KIND__RQ_FORMAT) {
        pho_error(rc = resp->error->rc, "Format failed for medium '%s'",
                  ids[resp->req_id].name);

        awaiting_resps[resp->req_id] = false;
    } else {
        pho_error(rc = -EPROTO, "Received invalid response");
    }

out:
    pho_srl_response_free(resp, true);

    return rc;
}

int phobos_admin_format(struct admin_handle *adm, const struct pho_id *ids,
                        int n_ids, int nb_streams, enum fs_type fs, bool unlock,
                        bool force)
{
    struct proto_req proto_req = {LRS_REQUEST};
    int n_rq_to_recv = 0;
    bool *awaiting_resps;
    pho_req_t req;
    int rc = 0;
    int rc2;
    int i;

    proto_req.msg.lrs_req = &req;
    /* TODO? This will be removed once the API use unsigned instead of signed
     * integers.
     */
    if (n_ids < 0)
        LOG_RETURN(-EINVAL, "Number of media cannot be negative");

    /* Check, in case of force, every medium is a tape, as the feature is not
     * available yet for other families.
     */
    if (force) {
        for (i = 0; i < n_ids; ++i) {
            if (ids[i].family != PHO_RSC_TAPE) {
                LOG_RETURN(-EINVAL,
                           "Force option is not available for family '%s'",
                           rsc_family2str(ids[i].family));
            }
        }
    }

    awaiting_resps = malloc(n_ids * sizeof(*awaiting_resps));
    if (awaiting_resps == NULL)
        LOG_RETURN(-ENOMEM, "Failed to allocate awaiting_resps array");

    for (i = 0; i < n_ids; i++) {
        awaiting_resps[i] = false;

        rc2 = pho_srl_request_format_alloc(&req);
        if (rc2)
            LOG_GOTO(req_fail, rc2,
                     "Cannot create format request for medium '%s', will skip",
                     ids[i].name);

        req.format->med_id->name = strdup(ids[i].name);
        if (req.format->med_id->name == NULL)
            LOG_GOTO(format_req_free, rc2 = -ENOMEM,
                     "Failed to duplicate medium name '%s', will skip",
                     ids[i].name);

        req.format->fs = fs;
        req.format->unlock = unlock;
        req.format->force = force;
        req.format->med_id->family = ids[i].family;

        req.id = i;

        rc2 = _send(&adm->phobosd_comm, proto_req);
        if (rc2) {
            rc = rc ? : rc2;
            pho_error(rc2, "Failed to send format request for medium '%s', "
                           "will skip", ids[i].name);
        } else {
            awaiting_resps[i] = true;
            n_rq_to_recv++;
        }

        if (nb_streams != 0 && n_rq_to_recv >= nb_streams) {
            rc2 = receive_format_response(adm, ids, awaiting_resps, i + 1);
            if (rc2)
                rc = rc ? : rc2;

            n_rq_to_recv--;
        }

        continue;

format_req_free:
        pho_srl_request_free(&req, false);
req_fail:
        rc = rc ? : rc2;
    }

    for (i = 0; i < n_rq_to_recv; ++i) {
        rc2 = receive_format_response(adm, ids, awaiting_resps, n_ids);
        if (rc2)
            rc = rc ? : rc2;
    }

    for (i = 0; i < n_ids; i++) {
        if (awaiting_resps[i] == true) {
            rc = rc ? : -ENODATA;
            pho_error(-ENODATA, "Did not receive a response for medium '%s'",
                      ids[i].name);
        }
    }

    free(awaiting_resps);

    return rc;
}

int phobos_admin_ping_lrs(struct admin_handle *adm)
{
    struct proto_resp proto_resp = {LRS_REQUEST};
    struct proto_req proto_req = {LRS_REQUEST};
    pho_resp_t *resp;
    pho_req_t req;
    int rid = 1;
    int rc;

    proto_req.msg.lrs_req = &req;
    rc = pho_srl_request_ping_alloc(&req);
    if (rc)
        LOG_RETURN(rc, "Cannot create LRS ping request");

    req.id = rid;

    rc = _send_and_receive(&adm->phobosd_comm, proto_req, &proto_resp);
    if (rc)
        LOG_RETURN(rc, "Error with phobosd communication");

    resp = proto_resp.msg.lrs_resp;
    /* expect ping response, send error otherwise */
    if (!(pho_response_is_ping(resp) && resp->req_id == rid))
        pho_error(rc = -EBADMSG, "Bad response from phobosd");

    pho_srl_response_free(resp, false);

    return rc;
}

int phobos_admin_ping_tlc(struct admin_handle *adm, bool *library_is_up)
{
    struct proto_resp proto_resp = {TLC_REQUEST};
    struct proto_req proto_req = {TLC_REQUEST};
    pho_tlc_resp_t *resp;
    pho_tlc_req_t req;
    int rid = 1;
    int rc;

    proto_req.msg.tlc_req = &req;
    pho_srl_tlc_request_ping_alloc(&req);
    req.id = rid;

    rc = _send_and_receive(&adm->tlc_comm, proto_req, &proto_resp);
    if (rc) {
        pho_verb("Error with TLC communication : %d , %s", rc, strerror(-rc));
        return rc;
    }

    resp = proto_resp.msg.tlc_resp;
    /* expect ping response and set library_is_up, no error response expected */
    if (!(pho_tlc_response_is_ping(resp) && resp->req_id == rid))
        pho_error(rc = -EBADMSG, "Bad response from TLC");
    else
        *library_is_up = resp->ping->library_is_up;

    pho_srl_tlc_response_free(resp, false);

    return rc;
}

/**
 * Construct the medium string for the extent list filter.
 *
 * The caller must ensure medium_str is initialized before calling.
 *
 * \param[in,out]   medium_str      Empty medium string.
 * \param[in]       medium          Medium filter.
 */
static void phobos_construct_medium(GString *medium_str, const char *medium)
{
    /**
     * We prefered putting this line in a separate function to ease a
     * possible future change that would allow for multiple medium selection.
     */
    g_string_append_printf(medium_str, "{\"DSS::EXT::medium_id\": \"%s\"}",
                           medium);
}

/**
 * Construct the extent string for the extent list filter.
 *
 * The caller must ensure extent_str is initialized before calling.
 *
 * \param[in,out]   extent_str      Empty extent string.
 * \param[in]       res             Ressource filter.
 * \param[in]       n_res           Number of requested resources.
 * \param[in]       is_pattern      True if search done using POSIX pattern.
 */
static void phobos_construct_extent(GString *extent_str, const char **res,
                                    int n_res, bool is_pattern)
{
    char *res_prefix = (is_pattern ? "{\"$REGEXP\": " : "");
    char *res_suffix = (is_pattern ? "}" : "");
    int i;

    if (n_res > 1)
        g_string_append_printf(extent_str, "{\"$OR\" : [");

    for (i = 0; i < n_res; ++i)
        g_string_append_printf(extent_str,
                               "%s {\"DSS::OBJ::oid\":\"%s\"}%s %s",
                               res_prefix,
                               res[i],
                               res_suffix,
                               (i + 1 != n_res) ? "," : "");

    if (n_res > 1)
        g_string_append_printf(extent_str, "]}");
}

int phobos_admin_layout_list(struct admin_handle *adm, const char **res,
                             int n_res, bool is_pattern, const char *medium,
                             struct layout_info **layouts, int *n_layouts)
{
    struct dss_filter *ext_filter_ptr = NULL;
    struct dss_filter *med_filter_ptr = NULL;
    struct dss_filter ext_filter;
    struct dss_filter med_filter;
    bool medium_is_valid;
    GString *extent_str;
    GString *medium_str;
    int rc = 0;

    extent_str = g_string_new(NULL);
    medium_str = g_string_new(NULL);
    medium_is_valid = (medium && strcmp(medium, ""));

    /**
     * If a medium is specified, we construct its filter.
     */
    if (medium_is_valid) {
        phobos_construct_medium(medium_str, medium);
        rc = dss_filter_build(&med_filter, "%s", medium_str->str);
        if (rc)
            goto release_extent;

        med_filter_ptr = &med_filter;
    }

    /**
     * If there are at least one resource, we construct a string containing
     * each request.
     */
    if (n_res) {
        phobos_construct_extent(extent_str, res, n_res, is_pattern);
        rc = dss_filter_build(&ext_filter, "%s", extent_str->str);
        if (rc)
            goto release_extent;

        ext_filter_ptr = &ext_filter;
    }

    /**
     * If no resource or medium was asked for, then using filters isn't
     * necessary, thus passing them as NULL to dss_full_layout_get ensures
     * the expected behaviour.
     */
    rc = dss_full_layout_get(&adm->dss, ext_filter_ptr, med_filter_ptr, layouts,
                             n_layouts);
    if (rc)
        pho_error(rc, "Cannot fetch layouts");

release_extent:
    g_string_free(extent_str, TRUE);
    g_string_free(medium_str, TRUE);

    dss_filter_free(ext_filter_ptr);
    dss_filter_free(med_filter_ptr);

    return rc;
}

void phobos_admin_layout_list_free(struct layout_info *layouts, int n_layouts)
{
    dss_res_free(layouts, n_layouts);
}

int phobos_admin_medium_locate(struct admin_handle *adm,
                               const struct pho_id *medium_id,
                               char **node_name)
{
    int rc;

    *node_name = NULL;

    /* get hostname if locked */
    rc = dss_medium_locate(&adm->dss, medium_id, node_name, NULL);
    if (rc)
        LOG_RETURN(rc, "Error when locating medium");

    /* Return NULL if medium is unlocked, not an error */
    return 0;
}

int phobos_admin_clean_locks(struct admin_handle *adm, bool global,
                             bool force, enum dss_type lock_type,
                             enum rsc_family dev_family,
                             char **lock_ids, int n_ids)
{
    const char *lock_hostname = NULL;
    const char *family_str = NULL;
    const char *type_str = NULL;
    int rc = 0;

    if (global && !force)
        LOG_RETURN(-EPERM, "Force mode is necessary for global mode.");

    if (!force && adm->phobosd_is_online)
        LOG_RETURN(-EPERM, "Deamon is online. Cannot release locks.");

    if (dev_family != PHO_RSC_NONE && lock_type == DSS_NONE)
        LOG_RETURN(-EINVAL, "Family parameter must be specified "
                            "with a type argument.");

    if (!global) {
        lock_hostname = get_hostname();

        if (!lock_hostname)
            return -EFAULT;
    }

    if (lock_type != DSS_NONE) {
        type_str = dss_type2str(lock_type);

        if (!type_str)
            LOG_RETURN(-EINVAL, "Specified type parameter is not valid: %d.",
                       lock_type);

        else if (lock_type != DSS_MEDIA && lock_type != DSS_OBJECT &&
            lock_type != DSS_DEVICE && lock_type != DSS_MEDIA_UPDATE_LOCK)
            LOG_RETURN(-EINVAL, "Specified type parameter is "
                                "not supported: %s.", type_str);
    }

    if (dev_family != PHO_RSC_NONE) {
        if (lock_type != DSS_DEVICE &&
            lock_type != DSS_MEDIA &&
            lock_type != DSS_MEDIA_UPDATE_LOCK)
            LOG_RETURN(-EINVAL, "Lock type '%s' not supported.", type_str);

        family_str = rsc_family2str(dev_family);

        if (!family_str)
            LOG_RETURN(-EINVAL, "Specified family parameter is not valid: %d.",
                       dev_family);
    }

    if (global && lock_type == DSS_NONE &&
        dev_family == PHO_RSC_NONE && n_ids == 0)
        rc = dss_lock_clean_all(&adm->dss);
    else
        rc = dss_lock_clean_select(&adm->dss, lock_hostname,
                                   type_str, family_str,
                                   lock_ids, n_ids);

    return rc;
}

int phobos_admin_lib_scan(enum lib_type lib_type, const char *lib_dev,
                          json_t **lib_data)
{
    const char *lib_type_name = lib_type2str(lib_type);
    struct lib_handle lib_hdl;
    json_t *lib_open_json;
    json_t *lib_scan_json;
    struct dss_handle dss;
    struct pho_id device;
    struct pho_id medium;
    struct pho_log log;
    int rc2;
    int rc;

    ENTRY;

    if (!lib_type_name)
        LOG_RETURN(-EINVAL, "Invalid lib type '%d'", lib_type);

    rc = dss_init(&dss);
    if (rc)
        LOG_RETURN(rc, "Failed to initialize DSS");

    rc = get_lib_adapter(lib_type, &lib_hdl.ld_module);
    if (rc)
        LOG_RETURN(rc, "Failed to get library adapter for type '%s'",
                   lib_type_name);

    medium.name[0] = 0;
    medium.family = PHO_RSC_TAPE;
    device.name[0] = 0;
    device.family = PHO_RSC_TAPE;
    init_pho_log(&log, device, medium, PHO_LIBRARY_SCAN);

    lib_open_json = json_object();

    rc = ldm_lib_open(&lib_hdl, lib_dev, lib_open_json);
    if (rc) {
        if (json_object_size(lib_open_json) != 0) {
            json_object_set_new(log.message,
                                OPERATION_TYPE_NAMES[PHO_LIBRARY_OPEN],
                                lib_open_json);
            log.error_number = rc;
            dss_emit_log(&dss, &log);
        } else {
            destroy_json(lib_open_json);
        }

        destroy_json(log.message);
        LOG_RETURN(rc, "Failed to open library of type '%s' for path '%s'",
                   lib_type_name, lib_dev);
    }

    destroy_json(lib_open_json);
    lib_scan_json = json_object();

    rc = ldm_lib_scan(&lib_hdl, lib_data, lib_scan_json);
    if (rc) {
        if (json_object_size(lib_scan_json) != 0) {
            json_object_set_new(log.message,
                                OPERATION_TYPE_NAMES[PHO_LIBRARY_SCAN],
                                lib_scan_json);
            log.error_number = rc;
            dss_emit_log(&dss, &log);
        } else {
            destroy_json(lib_scan_json);
        }

        destroy_json(log.message);
        LOG_GOTO(out, rc, "Failed to scan library of type '%s' for path '%s'",
                 lib_type_name, lib_dev);
    }

    destroy_json(lib_scan_json);
    destroy_json(log.message);

out:
    rc2 = ldm_lib_close(&lib_hdl);
    if (rc2)
        pho_error(rc2, "Failed to close library of type '%s'", lib_type_name);

    rc = rc ? : rc2;

    dss_fini(&dss);

    return rc;
}

int phobos_admin_dump_logs(struct admin_handle *adm, int fd,
                           struct pho_log_filter *log_filter)
{
    char time_buf[PHO_TIMEVAL_MAX_LEN];
    struct dss_filter dss_log_filter;
    struct dss_filter *filter_ptr;
    struct pho_log *logs;
    int n_logs;
    int dup_fd;
    FILE *fp;
    int rc;
    int i;

    filter_ptr = &dss_log_filter;

    rc = create_logs_filter(log_filter, &filter_ptr);
    if (rc)
        LOG_RETURN(rc, "Failed to create logs filter");

    dup_fd = dup(fd);
    if (dup_fd == -1)
        LOG_GOTO(out_filter, rc = -errno,
                 "Failed to duplicate file descriptor");

    /* fdopen will keep the offset of the file descriptor used */
    fp = fdopen(dup_fd, "w");
    if (fp == NULL) {
        rc = -errno;
        close(dup_fd);
        LOG_RETURN(rc, "Cannot open file descriptor as FILE*");
    }

    rc = dss_logs_get(&adm->dss, filter_ptr, &logs, &n_logs);
    if (rc)
        LOG_GOTO(out_close, rc, "Cannot fetch logs from the DSS");

    for (i = 0; i < n_logs; ++i) {
        char *json_buffer;

        timeval2str(&logs[i].time, time_buf);
        json_buffer = json_dumps(logs[i].message, 0);
        fprintf(fp,
                "<%s> Device '%s' with medium '%s' %s at '%s' (rc = %d): %s\n",
                time_buf, logs[i].device.name, logs[i].medium.name,
                logs[i].error_number != 0 ? "failed" : "succeeded",
                operation_type2str(logs[i].cause), logs[i].error_number,
                json_buffer);

        free(json_buffer);
    }

    dss_res_free(logs, n_logs);
    rc = 0;

out_close:
    /* fclose will close the file stream created here but also the underlying
     * file descriptor.
     */
    fclose(fp);

out_filter:
    dss_filter_free(filter_ptr);

    return rc;
}

int phobos_admin_clear_logs(struct admin_handle *adm,
                            struct pho_log_filter *log_filter, bool clear_all)
{
    struct dss_filter *filter_ptr = NULL;
    struct dss_filter dss_log_filter;
    int rc;

    if (!log_filter && !clear_all)
        LOG_RETURN(rc = -EINVAL,
                   "Cannot clear all logs without 'clear_all' option set");

    if (log_filter) {
        filter_ptr = &dss_log_filter;

        rc = create_logs_filter(log_filter, &filter_ptr);
        if (rc)
            LOG_RETURN(rc, "Failed to create logs filter");
    }

    rc = dss_logs_delete(&adm->dss, filter_ptr);
    dss_filter_free(filter_ptr);
    if (rc)
        LOG_RETURN(rc, "Failed to clear logs");

    return 0;
}

int phobos_admin_drive_lookup(struct admin_handle *adm, struct pho_id *id,
                              struct lib_drv_info *drive_info)
{
    struct proto_resp proto_resp = {TLC_REQUEST};
    struct proto_req proto_req = {TLC_REQUEST};
    struct dev_info *dev_res;
    pho_tlc_resp_t *resp;
    pho_tlc_req_t req;
    int rid = 1;
    int rc;

    if (id->family != PHO_RSC_TAPE)
        LOG_RETURN(-EINVAL,
                   "Resource family for a drive lookup must be %s "
                   "(PHO_RSC_TAPE), instead we got %s",
                   rsc_family2str(PHO_RSC_TAPE), rsc_family2str(id->family));

    /* fetch the serial number and store it in id */
    rc = _get_device_by_path_or_serial(adm, id, &dev_res);
    if (rc)
        LOG_RETURN(rc, "Unable to find device %s into DSS", id->name);

    dss_res_free(dev_res, 1);

    proto_req.msg.tlc_req = &req;
    rc = pho_srl_tlc_request_drive_lookup_alloc(&req);
    if (rc)
        LOG_RETURN(rc, "Unable to alloc drive lookup request");

    req.id = rid;
    req.drive_lookup->serial = strdup(id->name);
    if (!req.drive_lookup->serial) {
        pho_srl_tlc_request_free(&req, false);
        LOG_RETURN(-ENOMEM,
                   "Unable to copy serial number %s into drive lookup request",
                   id->name);
    }

    rc = _send_and_receive(&adm->tlc_comm, proto_req, &proto_resp);
    pho_srl_tlc_request_free(&req, false);
    if (rc)
        LOG_RETURN(rc,
                   "Error with TLC communication : %d , %s", rc, strerror(-rc));

    resp = proto_resp.msg.tlc_resp;

    if (pho_tlc_response_is_drive_lookup(resp) && resp->req_id == rid) {
        drive_info->ldi_addr.lia_type = MED_LOC_DRIVE;
        drive_info->ldi_addr.lia_addr = resp->drive_lookup->address;
        drive_info->ldi_first_addr = resp->drive_lookup->first_address;
        if (resp->drive_lookup->medium_name) {
            drive_info->ldi_full = true;
            drive_info->ldi_medium_id.family = PHO_RSC_TAPE;
            rc = pho_id_name_set(&drive_info->ldi_medium_id,
                                 resp->drive_lookup->medium_name);
            if (rc)
                LOG_GOTO(clean_response, rc,
                         "Unable to copy medium name %s from drive lookup "
                         "response", resp->drive_lookup->medium_name);
        }
    } else if (pho_tlc_response_is_error(resp) && resp->req_id == rid) {
        rc = resp->error->rc;
        if (resp->error->message)
            LOG_GOTO(clean_response, rc, "TLC failed to lookup the drive: '%s'",
                     resp->error->message);
        else
            LOG_GOTO(clean_response, rc, "TLC failed to lookup the drive");
    } else {
        LOG_GOTO(clean_response, rc = -EPROTO,
                 "TLC answers unexpected response to drive lookup");
    }

clean_response:
    pho_srl_tlc_response_free(resp, true);
    return rc;
}

int phobos_admin_load(struct admin_handle *adm, struct pho_id *drive_id,
                      const struct pho_id *tape_id)
{
    struct proto_resp proto_resp = {TLC_REQUEST};
    struct proto_req proto_req = {TLC_REQUEST};
    struct dev_info *dev_res;
    pho_tlc_resp_t *resp;
    pho_tlc_req_t req;
    int rid = 1;
    int rc;

    if (drive_id->family != PHO_RSC_TAPE)
        LOG_RETURN(-EINVAL,
                   "Drive resource family for a load must be %s, instead we "
                   "got %s", rsc_family2str(PHO_RSC_TAPE),
                   rsc_family2str(drive_id->family));

    if (tape_id->family != PHO_RSC_TAPE)
        LOG_RETURN(-EINVAL,
                   "Tape resource family for a load must be %s, instead we "
                   "got %s", rsc_family2str(PHO_RSC_TAPE),
                   rsc_family2str(tape_id->family));

    /* fetch the serial number and store it in id */
    rc = _get_device_by_path_or_serial(adm, drive_id, &dev_res);
    if (rc)
        LOG_RETURN(rc, "Unable to retrieve serial number from device name '%s' "
                   "in DSS", drive_id->name);

    dss_res_free(dev_res, 1);

    proto_req.msg.tlc_req = &req;
    rc = pho_srl_tlc_request_load_alloc(&req);
    if (rc)
        LOG_RETURN(rc, "Unable to alloc drive load request");

    req.id = rid;
    req.load->drive_serial = strdup(drive_id->name);
    if (!req.load->drive_serial) {
        pho_srl_tlc_request_free(&req, false);
        LOG_RETURN(-ENOMEM,
                   "Unable to copy drive serial number %s into load request",
                   drive_id->name);
    }

    req.load->tape_label = strdup(tape_id->name);
    if (!req.load->tape_label) {
        pho_srl_tlc_request_free(&req, false);
        LOG_RETURN(-ENOMEM,
                   "Unable to copy tape label %s into load request",
                   tape_id->name);
    }

    rc = _send_and_receive(&adm->tlc_comm, proto_req, &proto_resp);
    pho_srl_tlc_request_free(&req, false);
    if (rc)
        LOG_RETURN(rc,
                   "Error with TLC communication : %d , %s", rc, strerror(-rc));

    resp = proto_resp.msg.tlc_resp;
    if (pho_tlc_response_is_load(resp) && resp->req_id == rid) {
        if (resp->load->message)
            pho_verb("Successful admin load: %s", resp->load->message);

    } else if (pho_tlc_response_is_error(resp) && resp->req_id == rid) {
        rc = resp->error->rc;
        if (resp->error->message)
            pho_error(rc, "TLC failed to load: '%s'", resp->error->message);
        else
            pho_error(rc, "TLC failed to load: '%s'", tape_id->name);

    } else {
        rc = -EPROTO;
        pho_error(rc, "TLC answers unexpected response to load");
    }

    pho_srl_tlc_response_free(resp, true);
    return rc;
}
