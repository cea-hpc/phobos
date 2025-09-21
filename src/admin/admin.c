/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2024 CEA/DAM.
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
#include <inttypes.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>

#include "pho_cfg.h"
#include "pho_comm.h"
#include "pho_comm_wrapper.h"
#include "pho_common.h"
#include "pho_dss.h"
#include "pho_dss_wrapper.h"
#include "pho_io.h"
#include "pho_ldm.h"
#include "pho_srl_lrs.h"
#include "pho_types.h"
#include "pho_type_utils.h"

#include "import.h"

enum pho_cfg_params_admin {
    /* Actual admin parameters */
    PHO_CFG_ADMIN_lrs_socket,

    /* Delimiters, update when modifying options */
    PHO_CFG_ADMIN_FIRST = PHO_CFG_ADMIN_lrs_socket,
    PHO_CFG_ADMIN_LAST  = PHO_CFG_ADMIN_lrs_socket
};

const struct pho_config_item cfg_admin[] = {
    [PHO_CFG_ADMIN_lrs_socket] = LRS_SOCKET_CFG_ITEM,
};

/* ****************************************************************************/
/* Static Communication-related Functions *************************************/
/* ****************************************************************************/

static int _admin_notify(struct admin_handle *adm, struct pho_id *id,
                         enum notify_op op, bool need_to_wait)
{
    pho_resp_t *resp = NULL;
    pho_req_t req;
    int rid = 1;
    int rc;

    if (op <= PHO_NTFY_OP_INVAL || op >= PHO_NTFY_OP_LAST)
        LOG_RETURN(-ENOTSUP, "Operation not supported");

    pho_srl_request_notify_alloc(&req);

    req.id = rid;
    req.notify->op = op;
    req.notify->rsrc_id->family = id->family;
    req.notify->rsrc_id->name = xstrdup(id->name);
    req.notify->rsrc_id->library = xstrdup(id->library);
    req.notify->wait = need_to_wait;

    rc = comm_send(&adm->phobosd_comm, &req);
    if (rc)
        LOG_RETURN(rc, "Error with phobosd communication");

    if (!need_to_wait)
        return rc;

    rc = comm_recv(&adm->phobosd_comm, &resp);
    if (rc)
        LOG_RETURN(rc, "Error with phobosd communication");

    if (pho_response_is_notify(resp)) {
        if (resp->req_id == rid &&
            (int) id->family == (int) resp->notify->rsrc_id->family &&
            !strcmp(resp->notify->rsrc_id->name, id->name) &&
            !strcmp(resp->notify->rsrc_id->library, id->library)) {
            pho_debug("Notify request succeeded");
            goto out;
        }

        LOG_GOTO(out, rc = -EINVAL, "Received response does not "
                                    "answer emitted request");
    }

    if (pho_response_is_error(resp)) {
        rc = resp->error->rc;
        if (rc == -ECONNREFUSED && id->family != PHO_RSC_DIR)
            LOG_GOTO(out, rc, "Received error response. Cannot contact '%s'",
                     id->family == PHO_RSC_TAPE ? "TLC" : "RADOS");
        else
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

static void _free_dev_info(struct dev_info *devices, unsigned int num_dev)
{
    for (int i = 0; i < num_dev; ++i) {
        free(devices[i].host);
        free(devices[i].rsc.model);
        free(devices[i].path);
    }

    free(devices);
}

static int _add_device_in_dss(struct admin_handle *adm, struct pho_id *dev_ids,
                              unsigned int num_dev, bool keep_locked,
                              const char *library, struct dev_info **_devices)
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

    devices = xcalloc(num_dev, sizeof(*devices));

    for (i = 0; i < num_dev; ++i) {
        struct dev_info *devi = devices + i;

        /* Pools do not have a real path */
        if (dev_ids[i].family == PHO_RSC_RADOS_POOL)
            path = xstrdup(dev_ids[i].name);
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

        if ((strlen(lds.lds_serial) + 1) > PHO_URI_MAX)
            LOG_GOTO(out_free, rc = -EINVAL, "Drive serial is too long");

        pho_id_name_set(&devi->rsc.id, lds.lds_serial, library);
        devi->rsc.adm_status = status;
        rc = get_allocated_hostname(&devi->host);
        if (rc)
            LOG_GOTO(out_free, rc, "Failed to retrieve hostname");

        devi->rsc.model = xstrdup_safe(lds.lds_model);
        devi->path = xstrdup(dev_ids[i].name);

        ldm_dev_state_fini(&lds);

        pho_info("Will add device (family '%s', name '%s', library '%s') to "
                 "the database: model=%s serial=%s (%s)",
                 rsc_family2str(devi->rsc.id.family), devi->rsc.id.name,
                 devi->rsc.id.library, devi->rsc.model, devi->rsc.id.name,
                 rsc_adm_status2str(devi->rsc.adm_status));

        /* in case the name given by the user is not the device ID name */
        if (strcmp(dev_ids[i].name, devi->rsc.id.name))
            strcpy(dev_ids[i].name, devi->rsc.id.name);
    }

    rc = dss_device_insert(&adm->dss, devices, num_dev);
    if (rc)
        LOG_GOTO(out_free, rc, "Cannot add devices");

    *_devices = devices;

    return 0;

out_free:
    _free_dev_info(devices, num_dev);

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
                          "  ]},"
                          "  {\"DSS::DEV::library\": \"%s\"}"
                          "]}",
                          rsc_family2str(dev_id->family),
                          dev_id->name, dev_id->name, dev_id->library);
    if (rc)
        return rc;

    rc = dss_device_get(&adm->dss, &filter, dev_res, &dcnt, NULL);
    dss_filter_free(&filter);
    if (rc)
        return rc;

    if (dcnt == 0)
        LOG_RETURN(-ENXIO,
                   "Device (family '%s', serial '%s', library '%s') not found",
                   rsc_family2str(dev_id->family), dev_id->name,
                   dev_id->library);

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

    devices = xcalloc(num_dev, sizeof(*devices));

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
        dev_info_cpy(&devices[avail_devices], dev_res);
        dss_res_free(dev_res, 1);

        avail_devices++;
    }

    if (one_device_not_avail)
        LOG_GOTO(out_free, rc = -EBUSY,
                 "At least one device is in use and cannot be notified");

    if (avail_devices) {
        rc = dss_device_update(&adm->dss, devices, devices, avail_devices,
                               DSS_DEVICE_UPDATE_ADM_STATUS);
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

    dss_fini(&adm->dss);
}

int phobos_admin_init(struct admin_handle *adm, bool lrs_required,
                      void *phobos_context_handle)
{
    union pho_comm_addr lrs_sock_addr = {0};
    int rc;

    if (phobos_context_handle)
        phobos_module_context_set(
            (struct phobos_global_context *)phobos_context_handle);

    memset(adm, 0, sizeof(*adm));
    adm->phobosd_comm = pho_comm_info_init();

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

out:
    if (rc) {
        pho_error(rc, "Error during Admin initialization");
        phobos_admin_fini(adm);
    }

    return rc;
}

int phobos_admin_device_add(struct admin_handle *adm, struct pho_id *dev_ids,
                            unsigned int num_dev, bool keep_locked,
                            const char *library)
{
    struct dev_info *devices = NULL;
    int rc;
    int i;

    if (!num_dev)
        LOG_RETURN(-EINVAL, "No device were given");

    if (dev_ids[0].family == PHO_RSC_DIR) {
        for (i = 0; i < num_dev; i++) {
            struct stat st;

            rc = _normalize_path(dev_ids[i].name);
            if (rc)
                return rc;

            if (stat(dev_ids[i].name, &st) != 0)
                LOG_RETURN(-errno, "stat() failed on '%s'", dev_ids[i].name);

            if (!S_ISDIR(st.st_mode))
                LOG_RETURN(-ENOTDIR, "'%s' is not a directory",
                           dev_ids[i].name);
        }
    }

    rc = _add_device_in_dss(adm, dev_ids, num_dev, keep_locked, library,
                            &devices);
    if (rc)
        return rc;

    if (keep_locked)
        // do not need to inform the daemon because it does not
        // consider locked devices
        goto free_devices;

    if (!adm->phobosd_is_online)
        goto free_devices;

    for (i = 0; i < num_dev; ++i) {
        int rc2;

        rc2 = _admin_notify(adm, dev_ids + i, PHO_NTFY_OP_DEVICE_ADD, true);
        if (rc2) {
            rc = rc ? : rc2;
            pho_error(rc2, "Failure during daemon notification for '%s'",
                      dev_ids[i].name);
            dss_device_delete(&adm->dss, &devices[i], 1);
        }
    }

free_devices:
    _free_dev_info(devices, num_dev);

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
    pho_resp_t *resp;
    pho_req_t req;
    int rc;

    pho_srl_request_configure_alloc(&req);

    req.id = 1;
    req.configure->op = op;
    req.configure->configuration = configuration;

    rc = comm_send_and_recv(&adm->phobosd_comm, &req, &resp);
    if (rc)
        LOG_GOTO(free_req, rc, "Failed to send/receive configure with phobosd");

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
            *res = xstrdup(resp->configure->configuration);
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

        values[index] = xstrdup(json_string_value(value));
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

    devices = xcalloc(num_dev, sizeof(*devices));

    for (i = 0; i < num_dev; ++i) {
        if (dev_ids[i].family == PHO_RSC_DIR) {
            rc = _normalize_path(dev_ids[i].name);
            if (rc)
                continue;
        }

        rc = _get_device_by_path_or_serial(adm, dev_ids + i, &dev_res);
        if (rc)
            continue;

        rc = dss_lock(&adm->dss, DSS_DEVICE, dev_res, 1);
        if (rc) {
            pho_warn("Device (family '%s', name '%s', library '%s') cannot be "
                     "locked, so cannot be removed",
                     rsc_family2str(dev_ids[i].family), dev_ids[i].name,
                     dev_ids[i].library);
            dss_res_free(dev_res, 1);
            continue;
        }

        dev_info_cpy(&devices[avail_devices], dev_res);

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

    dss_unlock(&adm->dss, DSS_DEVICE, devices, avail_devices, false);
    for (i = 0; i < avail_devices; ++i)
        dev_info_free(devices + i, false);

out_free:
    free(devices);

    return rc;
}

int phobos_admin_device_lock(struct admin_handle *adm, struct pho_id *dev_ids,
                             int num_dev, bool need_to_wait)
{
    int rc = 0;
    int i;

    for (i = 0; i < num_dev; i++) {
        if (dev_ids[i].family == PHO_RSC_DIR) {
            rc = _normalize_path(dev_ids[i].name);
            if (rc)
                return rc;
        }
    }

    rc = _device_update_adm_status(adm, dev_ids, num_dev,
                                   PHO_RSC_ADM_ST_LOCKED, true);
    if (!adm->phobosd_is_online || rc)
        return rc;

    for (i = 0; i < num_dev; ++i) {
        int rc2;

        rc2 = _admin_notify(adm, dev_ids + i, PHO_NTFY_OP_DEVICE_LOCK,
                            need_to_wait);
        if (rc2) {
            pho_error(rc2,
                      "Failure during daemon notification for (family '%s', "
                      "name '%s', library '%s')",
                      rsc_family2str(dev_ids[i].family), dev_ids[i].name,
                      dev_ids[i].library);
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

    for (i = 0; i < num_dev; i++) {
        if (dev_ids[i].family == PHO_RSC_DIR) {
            rc = _normalize_path(dev_ids[i].name);
            if (rc)
                return rc;
        }
    }

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
            pho_error(rc2,
                      "Failure during daemon notification for (family '%s', "
                      "name '%s', library '%s')",
                      rsc_family2str(dev_ids[i].family), dev_ids[i].name,
                      dev_ids[i].library);
        rc = rc ? : rc2;
    }

    return rc;
}

int phobos_admin_device_status(struct admin_handle *adm,
                               enum rsc_family family,
                               char **status)
{
    pho_resp_t *resp = NULL;
    pho_req_t req;
    int rc;

    if (family < 0 || family >= PHO_RSC_LAST)
        LOG_RETURN(-EINVAL, "Invalid family %d", family);

    pho_srl_request_monitor_alloc(&req);

    req.id = 0;
    req.monitor->family = family;

    rc = comm_send_and_recv(&adm->phobosd_comm, &req, &resp);
    if (rc)
        return rc;

    if (pho_response_is_monitor(resp)) {
        *status = xstrdup(resp->monitor->status);
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
                               const char *library,
                               unsigned int *num_migrated_dev)
{
    struct dev_info *dst_devices;
    struct dev_info *src_devices;
    struct dev_info *dev_res;
    char *host_cpy = NULL;
    char *lib_cpy = NULL;
    int avail_devices = 0;
    int64_t fields = 0;
    size_t len;
    int rc = 0;
    int rc2;
    int i;

    *num_migrated_dev = 0;

    if (host != NULL) {
        host_cpy = xstrdup(host);
        fields |= DSS_DEVICE_UPDATE_HOST;
    }

    if (library != NULL) {
        lib_cpy = xstrdup(library);
        fields |= DSS_DEVICE_UPDATE_LIBRARY;
    }

    src_devices = xcalloc(num_dev, sizeof(*src_devices));
    dst_devices = xcalloc(num_dev, sizeof(*dst_devices));

    for (i = 0; i < num_dev; ++i) {
        rc2 = _get_device_by_path_or_serial(adm, dev_ids + i, &dev_res);
        if (rc2) {
            rc = rc ? : rc2;
            continue;
        }

        if (host != NULL && strcmp(host_cpy, dev_res->host) == 0) {
            pho_info("Device (family '%s', name '%s', library '%s') is already "
                     "located on '%s'",
                     rsc_family2str(dev_ids[i].family), dev_ids[i].name,
                     dev_ids[i].library, host_cpy);
            if (library == NULL) {
                dss_res_free(dev_res, 1);
                continue;
            }
        }

        if (library != NULL && strcmp(lib_cpy, dev_res->rsc.id.library) == 0) {
            pho_info("Device (family '%s', name '%s', library '%s') is already "
                     "located on '%s'",
                     rsc_family2str(dev_ids[i].family), dev_ids[i].name,
                     dev_ids[i].library, host_cpy);
            dss_res_free(dev_res, 1);
            continue;
        }

        rc2 = dss_lock(&adm->dss, DSS_DEVICE, dev_res, 1);
        if (rc2) {
            pho_error(rc2,
                      "Device (family '%s', name '%s', library '%s') cannot be "
                      "locked, so cannot be migrated",
                      rsc_family2str(dev_ids[i].family), dev_ids[i].name,
                      dev_ids[i].library);
            dss_res_free(dev_res, 1);
            rc = rc ? : (rc2 == -EEXIST ? -EBUSY : rc2);
            continue;
        }

        dev_info_cpy(&src_devices[avail_devices], dev_res);
        dev_info_cpy(&dst_devices[avail_devices], dev_res);

        if (host != NULL) {
            free((&dst_devices[avail_devices])->host);
            (&dst_devices[avail_devices])->host = xstrdup(host_cpy);
        }

        if (library != NULL) {
            len = strnlen(lib_cpy, PHO_URI_MAX);
            memcpy((&dst_devices[avail_devices])->rsc.id.library, lib_cpy, len);
            (&dst_devices[avail_devices])->rsc.id.library[len] = '\0';
        }

        dss_res_free(dev_res, 1);
        avail_devices++;
    }

    if (avail_devices == 0)
        LOG_GOTO(out_free, rc, "There are no available devices to migrate");

    rc2 = dss_device_update(&adm->dss, src_devices, dst_devices, avail_devices,
                            fields);

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

    dss_unlock(&adm->dss, DSS_DEVICE, src_devices, avail_devices, false);
    for (i = 0; i < avail_devices; ++i) {
        dev_info_free(src_devices + i, false);
        dev_info_free(dst_devices + i, false);
    }

    free(src_devices);
    free(dst_devices);
    if (host != NULL)
        free(host_cpy);

    if (library != NULL)
        free(lib_cpy);

    return rc;
}

int phobos_admin_drive_scsi_release(struct admin_handle *adm,
                                    struct pho_id *dev_ids,
                                    int num_dev, int *num_released_dev)
{
    struct pho_id media = { .family = PHO_RSC_TAPE, .name = "", .library = ""};
    struct fs_adapter_module *fsa;
    struct dev_info *dev_res;
    int released_dev = 0;
    struct pho_log log;
    int rc = 0;
    int rc2;
    int i;

    rc = get_fs_adapter(PHO_FS_LTFS, &fsa);
    if (rc)
        LOG_RETURN(-EINVAL, "Invalid filesystem type");

    for (i = 0; i < num_dev; i++) {
        rc2 = _get_device_by_path_or_serial(adm, dev_ids + i, &dev_res);
        if (rc2) {
            pho_error(-rc2,
                      "Unable to find device (family '%s', name '%s', library "
                      "'%s') in DSS", rsc_family2str(dev_ids[i].family),
                      dev_ids[i].name, dev_ids[i].library);
            rc = rc ? : rc2;
            continue;
        }

        rc2 = dss_lock(&adm->dss, DSS_DEVICE, dev_res, 1);
        if (rc2) {
            pho_error(-rc2,
                      "Device (family '%s', name '%s', library '%s') cannot be "
                      "locked, so cannot be released",
                      rsc_family2str(dev_ids[i].family), dev_ids[i].name,
                      dev_ids[i].library);
            dss_res_free(dev_res, 1);
            rc = rc ? : (rc2 == -EEXIST ? -EBUSY : rc2);
            continue;
        }

        init_pho_log(&log, dev_ids + i, &media, PHO_LTFS_RELEASE);
        rc2 = ldm_fs_release(fsa, dev_res->path, &log.message);
        emit_log_after_action(&adm->dss, &log, PHO_LTFS_RELEASE, rc2);
        if (rc2) {
            pho_error(-rc2, "Cannot release the LTFS reservation of the drive "
                      "(family '%s', name '%s', library '%s')",
                      rsc_family2str(dev_ids[i].family), dev_ids[i].name,
                      dev_ids[i].library);
            dss_unlock(&adm->dss, DSS_DEVICE, dev_res, 1, false);
            dss_res_free(dev_res, 1);
            rc = rc ? : rc2;
            continue;
        }

        rc2 = dss_unlock(&adm->dss, DSS_DEVICE, dev_res, 1, false);
        if (rc2) {
            pho_error(-rc2,
                      "Device (family '%s', name '%s', library '%s') cannot be "
                      "unlocked", rsc_family2str(dev_ids[i].family),
                      dev_ids[i].name, dev_ids[i].library);
            dss_res_free(dev_res, 1);
            rc = rc ? : rc2;
            continue;
        }
        dss_res_free(dev_res, 1);
        released_dev++;
    }
    *num_released_dev = released_dev;

    return rc;
}

static int receive_format_response(struct admin_handle *adm,
                                   const struct pho_id *ids,
                                   bool *awaiting_resps,
                                   int n_ids)
{
    pho_resp_t *resp;
    int rc = 0;

    rc = comm_recv(&adm->phobosd_comm, &resp);
    if (rc)
        return rc;

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
            struct pho_id resp_id;

            resp_id.family = (int) resp->format->med_id->family;
            pho_id_name_set(&resp_id, resp->format->med_id->name,
                            resp->format->med_id->library);

            if (pho_id_equal(&resp_id, &ids[resp->req_id]))
                pho_debug("Format request for medium (family '%s', name '%s', "
                          "library '%s') succeeded",
                          rsc_family2str(resp_id.family), resp_id.name,
                          resp_id.library);
            else
                pho_error(rc = -EPROTO,
                          "Received response does not answer emitted request, "
                          "expected (family '%s', name '%s', library '%s'), "
                          "got (family '%s', name '%s', library '%s') "
                          "(invalid req_id %d)",
                          rsc_family2str(ids[resp->req_id].family),
                          ids[resp->req_id].name, ids[resp->req_id].library,
                          rsc_family2str(resp_id.family), resp_id.name,
                          resp_id.library, resp->req_id);
        }
    } else if (pho_response_is_error(resp) &&
               resp->error->req_kind == PHO_REQUEST_KIND__RQ_FORMAT) {
        pho_error(rc = resp->error->rc,
                  "Format failed for medium (family '%s', name '%s', "
                  "library '%s')",
                  rsc_family2str(ids[resp->req_id].family),
                  ids[resp->req_id].name, ids[resp->req_id].library);

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
    int n_rq_to_recv = 0;
    bool *awaiting_resps;
    pho_req_t req;
    int rc = 0;
    int rc2;
    int i;

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

    awaiting_resps = xmalloc(n_ids * sizeof(*awaiting_resps));

    for (i = 0; i < n_ids; i++) {
        if (ids[i].family == PHO_RSC_DIR) {
            rc2 = _normalize_path((char *) ids[i].name);
            if (rc2) {
                rc = rc ? : rc2;
                continue;
            }
        }

        awaiting_resps[i] = false;

        pho_srl_request_format_alloc(&req);

        req.format->med_id->name = xstrdup(ids[i].name);
        req.format->med_id->library = xstrdup(ids[i].library);
        req.format->fs = fs;
        req.format->unlock = unlock;
        req.format->force = force;
        req.format->med_id->family = ids[i].family;

        req.id = i;

        rc2 = comm_send(&adm->phobosd_comm, &req);
        if (rc2) {
            rc = rc ? : rc2;
            pho_error(rc2,
                      "Failed to send format request for medium (family '%s', "
                      "name '%s', library '%s'), will skip",
                      rsc_family2str(ids[i].family), ids[i].name,
                      ids[i].library);
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
    }

    for (i = 0; i < n_rq_to_recv; ++i) {
        rc2 = receive_format_response(adm, ids, awaiting_resps, n_ids);
        if (rc2)
            rc = rc ? : rc2;
    }

    for (i = 0; i < n_ids; i++) {
        if (awaiting_resps[i] == true) {
            rc = rc ? : -ENODATA;
            pho_error(-ENODATA,
                      "Did not receive a response for medium (family '%s', "
                      "name '%s', library '%s')",
                      rsc_family2str(ids[i].family), ids[i].name,
                      ids[i].library);
        }
    }

    free(awaiting_resps);

    return rc;
}

static int _get_extents(struct admin_handle *adm,
                        const struct pho_id *source,
                        struct extent **extents, int *count, bool repack)
{
    struct dss_filter filter;
    int rc;

    if (repack)
        rc = dss_filter_build(&filter,
                              "{\"$AND\": ["
                              "  {\"DSS::EXT::medium_family\": \"%s\"},"
                              "  {\"DSS::EXT::medium_id\": \"%s\"},"
                              "  {\"DSS::EXT::medium_library\": \"%s\"},"
                              "  {\"$NOR\": [{\"DSS::EXT::state\": \"%s\"}]}"
                              "]}", rsc_family2str(source->family),
                              source->name, source->library,
                              extent_state2str(PHO_EXT_ST_ORPHAN));
    else
        rc = dss_filter_build(&filter,
                              "{\"$AND\": ["
                              "  {\"DSS::EXT::medium_family\": \"%s\"},"
                              "  {\"DSS::EXT::medium_id\": \"%s\"},"
                              "  {\"DSS::EXT::medium_library\": \"%s\"}"
                              "]}", rsc_family2str(source->family),
                              source->name, source->library);
    if (rc)
        LOG_RETURN(rc, "Failed to build filter for extent retrieval");

    if (repack)
        rc = dss_get_extents_order_by_ctime(&adm->dss, &filter, extents,
                                            count);
    else
        rc = dss_extent_get(&adm->dss, &filter, extents, count);

    dss_filter_free(&filter);
    if (rc)
        LOG_RETURN(rc,
                   "Failed to retrieve (family '%s', name '%s', library '%s') "
                   "extents",
                   rsc_family2str(source->family), source->name,
                   source->library);

    return rc;
}

static ssize_t _sum_extent_size(struct extent *extents, int count)
{
    ssize_t total_size = 0;
    int i;

    for (i = 0; i < count; ++i)
        total_size += extents[i].size;

    return total_size;
}

static int _get_source_medium(struct admin_handle *adm,
                              const struct pho_id *source,
                              struct pho_ext_loc *loc,
                              struct pho_io_descr *iod,
                              struct io_adapter_module **ioa,
                              enum fs_type *fs_type)
{
    pho_resp_t *resp;
    pho_req_t req;
    int rc;

    pho_srl_request_read_alloc(&req, 1);
    req.id = 1;
    req.ralloc->n_required = 1;
    req.ralloc->operation = PHO_READ_TARGET_ALLOC_OP_READ;
    req.ralloc->med_ids[0]->family = source->family;
    req.ralloc->med_ids[0]->name = xstrdup(source->name);
    req.ralloc->med_ids[0]->library = xstrdup(source->library);

    rc = comm_send_and_recv(&adm->phobosd_comm, &req, &resp);
    if (rc)
        return rc;

    if (pho_response_is_error(resp) && resp->req_id == 1)
        LOG_RETURN(resp->error->rc, "Error for read allocation");

    if (!(pho_response_is_read(resp) && resp->req_id == 1))
        LOG_RETURN(-EBADMSG, "Bad response for read allocation: ID #%d - '%s'",
                   resp->req_id, pho_srl_response_kind_str(resp));

    *fs_type = (enum fs_type)resp->ralloc->media[0]->fs_type;
    rc = get_io_adapter(*fs_type, ioa);
    if (rc)
        LOG_GOTO(free_resp, rc, "Failed to init read IO adapter");

    loc->root_path = xstrdup(resp->ralloc->media[0]->root_path);
    loc->addr_type = (enum address_type)resp->ralloc->media[0]->addr_type;
    iod->iod_loc = loc;

free_resp:
    pho_srl_response_free(resp, true);

    return rc;
}

static int _get_target_medium(struct admin_handle *adm,
                              const struct pho_id *source,
                              ssize_t total_size,
                              struct string_array *tags,
                              struct pho_ext_loc *loc,
                              struct pho_io_descr *iod,
                              struct pho_id *target)
{
    pho_resp_t *resp;
    pho_req_t req;
    int rc;
    int i;

    pho_srl_request_write_alloc(&req, 1, &tags->count);
    req.id = 2;
    req.walloc->family = source->family;
    req.walloc->prevent_duplicate = true;
    req.walloc->media[0]->size = total_size;
    req.walloc->media[0]->empty_medium = true;
    for (i = 0; i < tags->count; ++i)
        req.walloc->media[0]->tags[i] = xstrdup(tags->strings[i]);

    rc = comm_send_and_recv(&adm->phobosd_comm, &req, &resp);
    if (rc)
        return rc;

    if (pho_response_is_error(resp) && resp->req_id == 2)
        LOG_RETURN(resp->error->rc, "Error for write allocation");

    if (!(pho_response_is_write(resp) && resp->req_id == 2))
        LOG_RETURN(-EBADMSG, "Bad response for write allocation: ID #%d - '%s'",
                   resp->req_id, pho_srl_response_kind_str(resp));

    loc->root_path = xstrdup(resp->walloc->media[0]->root_path);
    loc->addr_type =
        (enum address_type)resp->walloc->media[0]->addr_type;
    iod->iod_flags = PHO_IO_REPLACE | PHO_IO_NO_REUSE;
    iod->iod_loc = loc;

    target->family = resp->walloc->media[0]->med_id->family;
    pho_id_name_set(target, resp->walloc->media[0]->med_id->name,
                    resp->walloc->media[0]->med_id->library);

    pho_srl_response_free(resp, true);

    return rc;
}

static int _send_and_recv_release(struct admin_handle *adm,
                                  const struct pho_id *source,
                                  const struct pho_io_descr *iod_source,
                                  int req_id, const struct pho_id *target,
                                  const struct pho_io_descr *iod_target,
                                  ssize_t total_size_written,
                                  ssize_t nb_extents_written)
{
    pho_resp_t *resp;
    pho_req_t req;
    int rc;

    if (target != NULL)
        pho_srl_request_release_alloc(&req, 2, false);
    else
        pho_srl_request_release_alloc(&req, 1, true);
    req.id = req_id;
    req.release->media[0]->med_id->family = source->family;
    req.release->media[0]->med_id->name = xstrdup(source->name);
    req.release->media[0]->med_id->library = xstrdup(source->library);
    req.release->media[0]->rc = iod_source->iod_rc;
    req.release->media[0]->size_written = 0;
    req.release->media[0]->nb_extents_written = 0;
    req.release->media[0]->to_sync = false;

    if (target == NULL) {
        rc = comm_send(&adm->phobosd_comm, &req);

        return rc;
    }

    req.release->media[1]->med_id->family = target->family;
    req.release->media[1]->med_id->name = xstrdup(target->name);
    req.release->media[1]->med_id->library = xstrdup(target->library);
    req.release->media[1]->rc = iod_target->iod_rc;
    req.release->media[1]->size_written = total_size_written;
    req.release->media[1]->nb_extents_written = nb_extents_written;
    req.release->media[1]->to_sync = true;

    rc = comm_send_and_recv(&adm->phobosd_comm, &req, &resp);
    if (rc)
        return rc;

    if (pho_response_is_error(resp) && resp->req_id == req_id)
        LOG_RETURN(resp->error->rc, "Error for release request");

    if (!(pho_response_is_release(resp) && resp->req_id == req_id))
        LOG_RETURN(-EBADMSG, "Bad response for release request: ID #%d - '%s'",
                   resp->req_id, pho_srl_response_kind_str(resp));

    pho_srl_response_free(resp, true);

    return rc;
}

static void _build_new_extent(const struct pho_id *target,
                              struct extent *old_extent,
                              struct extent *new_extent,
                              struct pho_io_descr *iod_source,
                              struct pho_io_descr *iod_target)
{
    new_extent->uuid = generate_uuid();
    new_extent->state = PHO_EXT_ST_PENDING;
    new_extent->size = old_extent->size;
    new_extent->address.size = old_extent->address.size;
    new_extent->address.buff = xstrdup(old_extent->address.buff);
    pho_id_copy(&new_extent->media, target);
    new_extent->with_xxh128 = old_extent->with_xxh128;
    if (new_extent->with_xxh128)
        memcpy(new_extent->xxh128, old_extent->xxh128,
               sizeof(old_extent->xxh128));
    new_extent->with_md5 = old_extent->with_md5;
    if (new_extent->with_md5)
        memcpy(new_extent->md5, old_extent->md5, sizeof(old_extent->md5));
    iod_source->iod_loc->extent = old_extent;
    iod_source->iod_size = old_extent->size;
    iod_target->iod_loc->extent = new_extent;
}

static int _clean_database_following_format(struct admin_handle *adm,
                                            const struct pho_id *source)
{
    struct dss_filter filter;
    struct extent *extents;
    int count;
    int rc;

    rc = dss_filter_build(&filter,
                          "{\"$AND\": ["
                          "  {\"DSS::EXT::medium_family\": \"%s\"},"
                          "  {\"DSS::EXT::medium_id\": \"%s\"},"
                          "  {\"DSS::EXT::medium_library\": \"%s\"}"
                          "]}", rsc_family2str(source->family), source->name,
                          source->library);
    if (rc)
        LOG_RETURN(rc, "Failed to build orphan extents retrieval filter");

    rc = dss_extent_get(&adm->dss, &filter, &extents, &count);
    dss_filter_free(&filter);
    if (rc)
        LOG_GOTO(free_ext, rc,
                 "Failed to retrieve orphan extents of source medium");

    if (count == 0)
        goto free_ext;

    rc = dss_extent_delete(&adm->dss, extents, count);
    if (rc)
        pho_error(rc, "Failed to remove orphan extents of source medium");

free_ext:
    dss_res_free(extents, count);

    return rc;
}

static int _retrieve_source_info(struct admin_handle *adm,
                                 const struct pho_id *medium,
                                 struct string_array *tags,
                                 enum fs_type *fs_type)
{
    struct dss_filter filter;
    struct media_info *media;
    int count;
    int rc;

    rc = dss_filter_build(&filter,
                          "{\"$AND\": ["
                          "  {\"DSS::MDA::family\": \"%s\"},"
                          "  {\"DSS::MDA::id\": \"%s\"},"
                          "  {\"DSS::MDA::library\": \"%s\"}"
                          "]}", rsc_family2str(medium->family), medium->name,
                          medium->library);
    if (rc)
        LOG_RETURN(rc, "Failed to build medium filter");

    rc = dss_media_get(&adm->dss, &filter, &media, &count, NULL);
    dss_filter_free(&filter);
    if (rc)
        LOG_RETURN(rc,
                   "Failed to retrieve medium "FMT_PHO_ID" info from DSS",
                   PHO_ID(*medium));

    if (count != 1) {
        dss_res_free(media, count);
        LOG_RETURN(-EINVAL, "Did not retrieve one medium, %d instead", count);
    }

    string_array_dup(tags, &media->tags);
    if (fs_type)
        *fs_type = media->fs.type;

    dss_res_free(media, count);

    return 0;
}

int phobos_admin_repack(struct admin_handle *adm, const struct pho_id *source,
                        struct string_array *tags)
{
    enum fs_type source_fs_type = PHO_FS_INVAL;
    struct pho_io_descr iod_source = {0};
    struct pho_io_descr iod_target = {0};
    struct string_array empty_tags = {0};
    struct pho_ext_loc loc_source = {0};
    struct pho_ext_loc loc_target = {0};
    struct io_adapter_module *ioa = {0};
    struct string_array *ptr_tags;
    struct extent *ext_res = NULL;
    struct string_array src_tags;
    GArray *new_ext_uuids = NULL;
    int ext_cnt_done = 0;
    struct pho_id target;
    ssize_t total_size;
    int ext_cnt;
    int rc;
    int i;

    if (source->family != PHO_RSC_TAPE)
        LOG_RETURN(-ENOTSUP, "Repack operation is only available for tapes");

    /* Invoke garbage collector for source tape */
    rc = dss_update_gc_for_tape(&adm->dss, source);
    if (rc)
        return rc;

    /* Determine total size of live objects */
    rc = _get_extents(adm, source, &ext_res, &ext_cnt, true);
    if (rc)
        return rc;

    rc = _retrieve_source_info(adm, source, &src_tags,
                               source_fs_type == PHO_FS_INVAL ?
                                  &source_fs_type : NULL);
    if (rc)
        LOG_GOTO(free_ext, rc,
                 "Failed to retrieve info for "FMT_PHO_ID, PHO_ID(*source));

    total_size = _sum_extent_size(ext_res, ext_cnt);
    if (total_size == 0)
        goto format;

    /* Prepare read allocation */
    rc = _get_source_medium(adm, source, &loc_source, &iod_source, &ioa,
                            &source_fs_type);
    if (rc)
        goto free_tags;

    /* Prepare write allocation
     * Use the same tags as the source medium
     */
    if (tags->count == 0)
        ptr_tags = &src_tags;
    /* Use no tags */
    else if (tags->count == 1 && strcmp(tags->strings[0], "") == 0)
        ptr_tags = &empty_tags;
    /* Use the tags specified */
    else
        ptr_tags = tags;

    rc = _get_target_medium(adm, source, total_size, ptr_tags, &loc_target,
                            &iod_target, &target);
    if (rc) {
        free(loc_source.root_path);

        _send_and_recv_release(adm, source, &iod_source, 3, NULL, NULL, 0, 0);
        goto free_tags;
    }

    new_ext_uuids = g_array_new(FALSE, TRUE, sizeof(ext_res[0].uuid));

    /* Copy loop */
    for (i = 0; i < ext_cnt; ++i, ++ext_cnt_done) {
        struct extent ext_new = {0};

        _build_new_extent(&target, &ext_res[i], &ext_new, &iod_source,
                          &iod_target);

        rc = copy_extent(ioa, &iod_source, ioa, &iod_target, PHO_RSC_TAPE);
        if (rc) {
            pho_error(rc, "Failed to copy extent '%s'", ext_res[i].uuid);
            break;
        }

        rc = dss_extent_insert(&adm->dss, &ext_new, 1);
        if (rc) {
            pho_error(rc, "Failed to add extent '%s' information in DSS",
                      ext_res[i].uuid);
            break;
        }

        g_array_append_val(new_ext_uuids, ext_new.uuid);

        free(ext_new.address.buff);
    }
    free(loc_target.root_path);
    free(loc_source.root_path);

    if (rc) {
        pho_error(rc, "Error encountered, repack is interrupted");
        goto free_ext;
    }

    rc = _send_and_recv_release(adm, source, &iod_source, 3,
                                &target, &iod_target,
                                ext_cnt_done == ext_cnt ?
                                    total_size :
                                    _sum_extent_size(ext_res, ext_cnt_done),
                                ext_cnt_done);
    if (rc)
        LOG_GOTO(free_ext, rc, "Failed to send/receive release");

    /* Database update: swap new and old extents */
    for (i = 0; i < ext_cnt_done; ++i) {
        rc = dss_update_extent_migrate(&adm->dss, ext_res[i].uuid,
                                       g_array_index(new_ext_uuids, char *, i));
        if (rc)
            LOG_GOTO(free_ext, rc, "Failed to update layouts in DSS");
    }

    g_array_free(new_ext_uuids, TRUE);
    new_ext_uuids = NULL;

format:
    rc = phobos_admin_format(adm, source, 1, 1, source_fs_type, true, true);
    if (rc)
        LOG_GOTO(free_ext, rc,
                 "Failed to format "FMT_PHO_ID, PHO_ID(*source));

    rc = _clean_database_following_format(adm, source);

free_ext:
    if (new_ext_uuids) {
        rc = dss_update_extent_state(&adm->dss,
                                     (const char **)new_ext_uuids->data,
                                     (int)new_ext_uuids->len,
                                     PHO_EXT_ST_ORPHAN);
        g_array_free(new_ext_uuids, TRUE);
        if (rc)
            pho_error(rc, "Failed to update state of new extents to orphan");

    }

free_tags:
    string_array_free(&src_tags);
    dss_res_free(ext_res, ext_cnt);

    return rc;
}

int phobos_admin_ping_lrs(struct admin_handle *adm)
{
    pho_resp_t *resp;
    pho_req_t req;
    int rid = 1;
    int rc;

    pho_srl_request_ping_alloc(&req);

    req.id = rid;

    rc = comm_send_and_recv(&adm->phobosd_comm, &req, &resp);
    if (rc)
        LOG_RETURN(rc, "Error with phobosd communication");

    /* expect ping response, send error otherwise */
    if (!(pho_response_is_ping(resp) && resp->req_id == rid))
        pho_error(rc = -EBADMSG, "Bad response from phobosd");

    pho_srl_response_free(resp, false);

    return rc;
}

int phobos_admin_ping_tlc(const char *library, bool *library_is_up)
{
    struct lib_handle lib_hdl;
    int rc2;
    int rc;

    ENTRY;

    rc = get_lib_adapter_and_open(PHO_LIB_SCSI, &lib_hdl, library);
    if (rc)
        return rc;

    rc = ldm_lib_ping(&lib_hdl, library_is_up);
    rc2 = ldm_lib_close(&lib_hdl);
    if (rc2)
        pho_error(rc2, "Failed to close library");

    rc = rc ? : rc2;

    return rc;
}

/**
 * Construct the extent string for the extent list filter.
 *
 * The caller must ensure extent_str is initialized before calling.
 *
 * \param[in,out]   extent_str      Empty extent string.
 * \param[in]       medium          Medium filter.
 * \param[in]       library         Library filter.
 * \param[in]       orphan          Orphan state filter.
 */
static void phobos_construct_extent(GString *extent_str, const char *medium,
                                    const char *library, bool orphan)
{
    int count = 0;

    count += (medium != NULL ? 1 : 0);
    count += (library != NULL ? 1 : 0);
    count += (orphan ? 1 : 0);

    if (count == 0)
        return;

    g_string_append_printf(extent_str, "{\"$AND\":[");

    if (medium)
        g_string_append_printf(extent_str,
                               "{\"DSS::EXT::medium_id\": \"%s\"}%s",
                                medium, --count != 0 ? "," : "");

    if (library)
        g_string_append_printf(extent_str,
                               "{\"DSS::EXT::medium_library\": \"%s\"}%s",
                               library, --count != 0 ? "," : "");

    if (orphan)
        g_string_append_printf(extent_str,
                               "{\"DSS::EXT::state\": \"%s\"}",
                               extent_state2str(PHO_EXT_ST_ORPHAN));

    g_string_append_printf(extent_str, "]}");
}

/**
 * Construct the object string for the extent list filter.
 *
 * The caller must ensure obj_str is initialized before calling.
 *
 * \param[in,out]   obj_str         Empty object string.
 * \param[in]       res             Ressource filter.
 * \param[in]       n_res           Number of requested resources.
 * \param[in]       is_pattern      True if search done using POSIX pattern.
 * \param[in]       copy_name       Copy name filter.
 */
static void phobos_construct_object(GString *obj_str, const char **res,
                                    int n_res, bool is_pattern,
                                    const char *copy_name)
{
    char *res_prefix = (is_pattern ? "{\"$REGEXP\": " : "");
    char *res_suffix = (is_pattern ? "}" : "");
    int i;

    g_string_append_printf(obj_str, "{\"$AND\" : [");

    if (copy_name)
        g_string_append_printf(obj_str,
                               "{\"DSS::COPY::copy_name\":\"%s\"}%s",
                               copy_name, n_res ? "," : "");

    if (n_res) {
        g_string_append_printf(obj_str, "{\"$OR\" : [");

        for (i = 0; i < n_res; ++i)
            g_string_append_printf(obj_str,
                                   "%s {\"DSS::OBJ::oid\":\"%s\"}%s %s",
                                   res_prefix,
                                   res[i],
                                   res_suffix,
                                   (i + 1 != n_res) ? "," : "");

        g_string_append_printf(obj_str, "]}");
    }

    g_string_append_printf(obj_str, "]}");
}

int phobos_admin_layout_list(struct admin_handle *adm, const char **res,
                             int n_res, bool is_pattern, const char *medium,
                             const char *library, const char *copy_name,
                             bool orphan, struct layout_info **layouts,
                             int *n_layouts, struct dss_sort *sort)
{
    struct dss_filter *extent_filter_ptr = NULL;
    struct dss_filter *obj_filter_ptr = NULL;
    struct dss_filter extent_filter;
    struct dss_filter obj_filter;
    bool library_is_valid;
    bool medium_is_valid;
    bool copy_is_valid;
    GString *extent_str;
    GString *obj_str;
    int rc = 0;

    extent_str = g_string_new(NULL);
    obj_str = g_string_new(NULL);

    medium_is_valid = (medium && strcmp(medium, ""));
    library_is_valid = (library && strcmp(library, ""));

    if (medium_is_valid || library_is_valid || orphan) {
        phobos_construct_extent(extent_str, medium, library, orphan);

        rc = dss_filter_build(&extent_filter, "%s", extent_str->str);
        if (rc)
            goto release_extent;

        extent_filter_ptr = &extent_filter;
    }

    /**
     * If there are at least one resource, we construct a string containing
     * each request.
     */
    copy_is_valid = (copy_name && strcmp(copy_name, ""));

    if (n_res || copy_is_valid) {
        phobos_construct_object(obj_str, res, n_res, is_pattern, copy_name);
        rc = dss_filter_build(&obj_filter, "%s", obj_str->str);
        if (rc)
            goto release_extent;

        obj_filter_ptr = &obj_filter;
    }

    /**
     * If no resource or medium was asked for, then using filters isn't
     * necessary, thus passing them as NULL to dss_full_layout_get ensures
     * the expected behaviour.
     */
    rc = dss_full_layout_get(&adm->dss, obj_filter_ptr, extent_filter_ptr,
                             layouts, n_layouts, sort);
    if (rc)
        pho_error(rc, "Cannot fetch layouts");

release_extent:
    g_string_free(obj_str, TRUE);
    g_string_free(extent_str, TRUE);

    dss_filter_free(extent_filter_ptr);
    dss_filter_free(obj_filter_ptr);

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

    if (medium_id->family == PHO_RSC_DIR) {
        rc = _normalize_path((char *) medium_id->name);
        if (rc)
            return rc;
    }

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

int phobos_admin_media_add(struct admin_handle *adm, struct media_info *med_ls,
                           int med_cnt)
{
    int rc;
    int i;

    if (!med_cnt)
        LOG_RETURN(-EINVAL, "No media were given");

    if (med_ls[0].rsc.id.family == PHO_RSC_DIR) {
        for (i = 0; i < med_cnt; i++) {
            rc = _normalize_path(med_ls[i].rsc.id.name);
            if (rc)
                return rc;
        }
    }

    return dss_media_insert(&adm->dss, med_ls, med_cnt);
}

int phobos_admin_media_delete(struct admin_handle *adm, struct pho_id *med_ids,
                              int num_med, int *num_removed_med)
{
    struct media_info *media_res;
    struct media_info *media;
    struct extent *ext_res;
    int avail_media = 0;
    int ext_cnt;
    int rc;
    int i;

    *num_removed_med = 0;

    media = xcalloc(num_med, sizeof(*media));

    for (i = 0; i < num_med; ++i) {
        if (med_ids[i].family == PHO_RSC_DIR) {
            rc = _normalize_path(med_ids[i].name);
            if (rc)
                goto out_free;
        }

        rc = dss_one_medium_get_from_id(&adm->dss, med_ids + i, &media_res);
        if (rc)
            goto out_free;

        rc = dss_lock(&adm->dss, DSS_MEDIA, media_res, 1);
        if (rc) {
            pho_warn("Media (family '%s', name '%s', library '%s') cannot be "
                     "locked, so cannot be removed",
                     rsc_family2str(med_ids[i].family), med_ids[i].name,
                     med_ids[i].library);
            dss_res_free(media_res, 1);
            continue;
        }

        rc = _get_extents(adm, med_ids + i, &ext_res, &ext_cnt, false);
        if (rc)
            goto out_free;

        dss_res_free(ext_res, ext_cnt);
        if (ext_cnt > 0) {
            pho_warn("Media (family '%s', name '%s', library '%s') contains "
                     "extents, so cannot be removed",
                     rsc_family2str(med_ids[i].family), med_ids[i].name,
                     med_ids[i].library);
            dss_unlock(&adm->dss, DSS_MEDIA, media_res, 1, false);
            dss_res_free(media_res, 1);
            continue;
        }

        media_info_copy(&media[avail_media], media_res);
        avail_media++;

        dss_res_free(media_res, 1);
    }

    if (avail_media == 0)
        LOG_GOTO(out_free, rc = -ENODEV,
                 "There are no available media to remove");

    rc = dss_media_delete(&adm->dss, media, avail_media);
    if (rc)
        pho_error(rc, "Medium cannot be removed");

    *num_removed_med = avail_media;

out_free:
    dss_unlock(&adm->dss, DSS_MEDIA, media, avail_media, false);
    for (i = 0; i < avail_media; ++i)
        media_info_cleanup(media + i);

    free(media);

    return rc;
}

int phobos_admin_media_import(struct admin_handle *adm,
                              struct media_info *med_ls,
                              int med_cnt,
                              bool check_hash)
{
    struct dss_filter filter;
    struct copy_info *copy;
    int cpy_cnt;
    int rc = 0;
    int rc2;
    int i;

    ENTRY;

    for (i = 0; i < med_cnt; i++) {
        const struct pho_id *medium = &med_ls[i].rsc.id;

        if (medium->family == PHO_RSC_DIR) {
            rc = _normalize_path((char *) medium->name);
            if (rc)
                return rc;
        }

        med_ls[i].fs.status = PHO_FS_STATUS_IMPORTING;

        pho_verb("Starting import of (family '%s', name '%s', library '%s')",
                 rsc_family2str(medium->family), medium->name, medium->library);

        rc = dss_media_insert(&adm->dss, med_ls + i, 1);
        if (rc)
            LOG_RETURN(rc,
                       "Unable to add medium (family '%s', name '%s', library "
                       "'%s') to database", rsc_family2str(medium->family),
                       medium->name, medium->library);

        rc = import_medium(adm, &med_ls[i], check_hash);
        if (rc)
            LOG_RETURN(rc,
                       "Unable to import medium (family '%s', name '%s', "
                       "library '%s') to database",
                       rsc_family2str(medium->family), medium->name,
                       medium->library);
    }

    rc = dss_filter_build(&filter,
                          "{\"$OR\": [ "
                            "{\"DSS::COPY::copy_status\": \"incomplete\"},"
                            "{\"DSS::COPY::copy_status\": \"readable\"}"
                          "]}");
    if (rc)
        return rc;

    rc = dss_copy_get(&adm->dss, &filter, &copy, &cpy_cnt, NULL);
    dss_filter_free(&filter);
    if (rc)
        return rc;

    if (cpy_cnt == 0) {
        dss_res_free(copy, cpy_cnt);
        return rc;
    }

    for (int i = 0; i < cpy_cnt; i++) {
        rc2 = reconstruct_copy(adm, &copy[i]);
        if (rc2) {
            pho_error(rc2, "Failed to reconstruct copy '%s', skipping it",
                      copy[i].copy_name);
            rc = rc ? : rc2;
        }
    }

    dss_res_free(copy, cpy_cnt);
    return rc;
}

int phobos_admin_media_library_rename(struct admin_handle *adm,
                                      struct pho_id *med_ids,
                                      int num_med, const char *library,
                                      int *num_renamed_med)
{
    struct media_info *src_media;
    struct media_info *dst_media;
    struct media_info *med_res;
    GArray *extents = NULL;
    struct extent *ext_res;
    char *lib_cpy = NULL;
    int avail_medias = 0;
    int ext_cnt;
    size_t len;
    int rc = 0;
    int i, j;
    int rc2;

    *num_renamed_med = 0;

    src_media = xcalloc(num_med, sizeof(*src_media));
    dst_media = xcalloc(num_med, sizeof(*dst_media));
    extents = g_array_new(FALSE, TRUE, sizeof(*ext_res));
    lib_cpy = xstrdup_safe(library);

    for (i = 0; i < num_med; i++) {
        rc2 = dss_one_medium_get_from_id(&adm->dss, med_ids + i, &med_res);
        if (rc2) {
            rc = rc ? : rc2;
            continue;
        }

        if (strcmp(lib_cpy, med_res->rsc.id.library) == 0) {
            pho_info("Media (family '%s', name '%s', library '%s') is already "
                     "located on '%s'",
                     rsc_family2str(med_ids[i].family), med_ids[i].name,
                     med_ids[i].library, lib_cpy);
            dss_res_free(med_res, 1);
            continue;
        }

        rc2 = dss_lock(&adm->dss, DSS_MEDIA, med_res, 1);
        if (rc2) {
            pho_error(rc2,
                      "Media (family '%s', name '%s', library '%s') cannot be "
                      "locked, so cannot be renamed",
                      rsc_family2str(med_ids[i].family), med_ids[i].name,
                      med_ids[i].library);
            dss_res_free(med_res, 1);
            rc = rc ? : rc2;
            continue;
        }

        rc2 = _get_extents(adm, med_ids + i, &ext_res, &ext_cnt, false);
        if (rc2) {
            dss_unlock(&adm->dss, DSS_MEDIA, med_res, 1, false);
            dss_res_free(med_res, 1);
            rc = rc ? : rc2;
            continue;
        }

        if (ext_cnt > 0) {
            /* Change library in all extents presents on the media */
            len = strnlen(lib_cpy, PHO_URI_MAX);
            for (j = 0; j < ext_cnt; j++) {
                memcpy(ext_res[j].media.library, lib_cpy, len);
                ext_res[j].media.library[len] = '\0';
            }
            g_array_append_vals(extents, ext_res, ext_cnt);
            dss_res_free(ext_res, ext_cnt);
        }

        media_info_copy(&src_media[avail_medias], med_res);
        media_info_copy(&dst_media[avail_medias], med_res);
        len = strnlen(lib_cpy, PHO_URI_MAX);
        memcpy((&dst_media[avail_medias])->rsc.id.library, lib_cpy, len);
        (&dst_media[avail_medias])->rsc.id.library[len] = '\0';

        dss_res_free(med_res, 1);
        avail_medias++;
    }

    if (avail_medias == 0)
        LOG_GOTO(out_free, rc, "There are no available medias to rename");

    if (extents->len > 0) {
        rc = dss_extent_update(&adm->dss, (struct extent *) extents->data,
                               (struct extent *) extents->data, extents->len);
        if (rc)
            LOG_GOTO(out_free, rc, "Update of extents has failed");
    }

    rc = dss_media_update(&adm->dss, src_media, dst_media, avail_medias,
                          LIBRARY);

    if (rc)
        pho_error(rc, "Failed to rename medias");
    else
        *num_renamed_med = avail_medias;

out_free:
    dss_unlock(&adm->dss, DSS_MEDIA, src_media, avail_medias, false);
    for (i = 0; i < avail_medias; ++i) {
        media_info_cleanup(src_media + i);
        media_info_cleanup(dst_media + i);
    }

    g_array_free(extents, TRUE);
    free(src_media);
    free(dst_media);
    free(lib_cpy);

    return rc;
}

int phobos_admin_lib_scan(enum lib_type lib_type, const char *library,
                          bool refresh, json_t **lib_data)
{
    struct lib_handle lib_hdl;
    struct dss_handle dss;
    struct pho_id device;
    struct pho_id medium;
    struct pho_log log;
    int rc2;
    int rc;

    ENTRY;

    rc = get_lib_adapter_and_open(lib_type, &lib_hdl, library);
    if (rc)
        return rc;

    rc = dss_init(&dss);
    if (rc)
        LOG_RETURN(rc, "Failed to initialize DSS");

    medium.name[0] = 0;
    medium.library[0] = 0;
    medium.family = PHO_RSC_TAPE;
    device.name[0] = 0;
    device.library[0] = 0;
    device.family = PHO_RSC_TAPE;
    init_pho_log(&log, &device, &medium, PHO_LIBRARY_SCAN);
    log.message = json_object();
    rc = ldm_lib_scan(&lib_hdl, refresh, lib_data, log.message);
    emit_log_after_action(&dss, &log, PHO_LIBRARY_SCAN, rc);
    if (rc)
        LOG_GOTO(out, rc, "Failed to scan library '%s'", library);

out:
    rc2 = ldm_lib_close(&lib_hdl);
    if (rc2)
        pho_error(rc2, "Failed to close library");

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
                "<%s> Action '%s' with device (family '%s', name '%s', library "
                "'%s') and medium (family '%s', name '%s', library '%s') %s "
                "(rc = %d): %s\n",
                time_buf,
                operation_type2str(logs[i].cause),
                rsc_family2str(logs[i].device.family),
                logs[i].device.name,
                logs[i].device.library,
                rsc_family2str(logs[i].medium.family),
                logs[i].medium.name,
                logs[i].medium.library,
                logs[i].error_number != 0 ? "failed" : "succeeded",
                logs[i].error_number,
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

/* Mutualized between phobos_admin_drive_lookup and phobos_admin_unload */
static int drive_lookup(const char *drive_serial, const char *library,
                        struct lib_drv_info *drive_info)
{
    struct lib_handle lib_hdl;
    int rc2;
    int rc;

    rc = get_lib_adapter_and_open(PHO_LIB_SCSI, &lib_hdl, library);
    if (rc)
        return rc;

    rc = ldm_lib_drive_lookup(&lib_hdl, drive_serial, drive_info);
    rc2 = ldm_lib_close(&lib_hdl);
    if (rc2)
        pho_error(rc2, "Failed to close library");

    rc = rc ? : rc2;

    return rc;
}

int phobos_admin_drive_lookup(struct admin_handle *adm, struct pho_id *id,
                              struct lib_drv_info *drive_info)
{
    struct dev_info *dev_res;
    int rc;

    if (id->family != PHO_RSC_TAPE)
        LOG_RETURN(-EINVAL,
                   "Resource family for a drive lookup must be %s "
                   "(PHO_RSC_TAPE), instead we got %s",
                   rsc_family2str(PHO_RSC_TAPE), rsc_family2str(id->family));

    /* fetch the serial number and store it in id */
    rc = _get_device_by_path_or_serial(adm, id, &dev_res);
    if (rc)
        LOG_RETURN(rc,
                   "Unable to find device (family '%s', name '%s', library "
                   "'%s') in DSS", rsc_family2str(id->family), id->name,
                   id->library);

    dss_res_free(dev_res, 1);

    return drive_lookup(id->name, id->library, drive_info);
}

/**
 * To load, a lock is taken on the drive and on the tape to
 * prevent any concurrent move on these resources.
 *
 * If it is not failed or already admin locked, we set the status of the drive
 * to "admin locked" during the load.
 */
int phobos_admin_load(struct admin_handle *adm, struct pho_id *drive_id,
                      const struct pho_id *tape_id)
{
    bool drive_was_unlocked = false;
    struct media_info *med_res;
    struct lib_handle lib_hdl;
    struct dev_info *dev_res;
    const char *hostname;
    int rc, rc2;
    int pid;

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

    rc = _get_device_by_path_or_serial(adm, drive_id, &dev_res);
    if (rc)
        LOG_RETURN(rc,
                   "Unable to retrieve serial number from device (name '%s', "
                   "library '%s') in DSS", drive_id->name, drive_id->library);

    /* No need to admin lock a failed drive because, it is an unused one. */
    if (dev_res->rsc.adm_status == PHO_RSC_ADM_ST_UNLOCKED) {
        drive_was_unlocked = true;
        rc = phobos_admin_device_lock(adm, drive_id, 1, true);
        if (rc)
            LOG_GOTO(free_dev_res, rc,
                     "Unable to admin lock the drive (name '%s', library '%s') "
                     "before loading",
                     drive_id->name, drive_id->library);
    }

    /* Lock drive and medium */
    rc = dss_one_medium_get_from_id(&adm->dss, tape_id, &med_res);
    if (rc)
        LOG_GOTO(admin_unlock, rc,
                 "Unable to get tape (name '%s', library '%s') from DSS",
                 tape_id->name, tape_id->library);

    rc = fill_host_owner(&hostname, &pid);
    if (rc)
        LOG_GOTO(free_med_res, rc,
                 "Unable to retrieve hostname and pid to check drive and "
                 "medium locks");

    rc = dss_lock(&adm->dss, DSS_DEVICE, dev_res, 1);
    if (rc)
        LOG_GOTO(free_med_res, rc,
                 "admin host '%s' owner/pid '%d' failed to lock the "
                 "drive (name: '%s', library '%s', path: '%s') to load",
                 hostname, pid, dev_res->rsc.id.name, dev_res->rsc.id.library,
                 dev_res->path);

    rc = dss_check_and_take_lock(&adm->dss, &med_res->rsc.id, &med_res->lock,
                                 DSS_MEDIA, med_res, hostname, pid);
    if (rc)
        LOG_GOTO(unlock_drive, rc,
                 "Unable to lock the tape (name '%s', library '%s')",
                 tape_id->name, tape_id->library);

    /* load request with the tlc*/
    rc = get_lib_adapter_and_open(PHO_LIB_SCSI, &lib_hdl,
                                  dev_res->rsc.id.library);
    if (rc)
        goto unlock_tape;

    rc = ldm_lib_load(&lib_hdl, dev_res->rsc.id.name, med_res->rsc.id.name);
    if (rc)
        pho_error(rc,
                  "Admin failed to load the tape (name '%s', library '%s') "
                  "into the drive (name '%s', library '%s', path '%s')",
                  med_res->rsc.id.name, med_res->rsc.id.library,
                  dev_res->rsc.id.name, dev_res->rsc.id.library, dev_res->path);

    rc2 = ldm_lib_close(&lib_hdl);
    if (rc2) {
        pho_error(rc2, "Failed to close library of type '%s'",
                  lib_type2str(PHO_LIB_SCSI));
        rc = rc ? : rc2;
    }

unlock_tape:
    /* do not unlock if there was a locate lock before the load operation */
    if (!med_res->lock.is_early) {
        rc2 = dss_unlock(&adm->dss, DSS_MEDIA, med_res, 1, false);
        if (rc2) {
            pho_error(rc2, "Unable to unlock tape (name '%s', family '%s')",
                      med_res->rsc.id.name, med_res->rsc.id.library);
            rc = rc ? : rc2;
        }
    }

unlock_drive:
    rc2 = dss_unlock(&adm->dss, DSS_DEVICE, dev_res, 1, false);
    if (rc2) {
        pho_error(rc2,
                  "Unable to unlock drive (id: '%s', library '%s', path: '%s')",
                  dev_res->rsc.id.name, dev_res->rsc.id.library, dev_res->path);
        rc = rc ? : rc2;
    }

free_med_res:
    dss_res_free(med_res, 1);

admin_unlock:
    if (drive_was_unlocked) {
        rc2 = phobos_admin_device_unlock(adm, drive_id, 1, true);
        if (rc2) {
            pho_error(rc2,
                      "Unable to admin unlock the drive (name '%s', library "
                      "'%s) after loading",
                      drive_id->name, drive_id->library);
            rc = rc ? : rc2;
        }
    }

free_dev_res:
    dss_res_free(dev_res, 1);
    return rc;
}

/**
 * To unload, a lock is taken on the drive and on the tape to
 * prevent any concurrent move on these resources.
 *
 * If it is not failed or already admin locked, we set the status of the drive
 * to "admin locked" during the unload.
 */
int phobos_admin_unload(struct admin_handle *adm, struct pho_id *drive_id,
                        const struct pho_id *tape_id)
{
    bool drive_was_unlocked = false;
    struct lib_drv_info drive_info;
    struct media_info *med_res;
    struct lib_handle lib_hdl;
    struct dev_info *dev_res;
    const char *hostname;
    int rc, rc2;
    int pid;

    if (drive_id->family != PHO_RSC_TAPE)
        LOG_RETURN(-EINVAL,
                   "Drive resource family for an unload must be %s, instead we "
                   "got %s", rsc_family2str(PHO_RSC_TAPE),
                   rsc_family2str(drive_id->family));

    if (tape_id && tape_id->family != PHO_RSC_TAPE)
        LOG_RETURN(-EINVAL,
                   "Tape resource family for an unload must be %s, instead we "
                   "got %s", rsc_family2str(PHO_RSC_TAPE),
                   rsc_family2str(tape_id->family));

    rc = _get_device_by_path_or_serial(adm, drive_id, &dev_res);
    if (rc)
        LOG_RETURN(rc,
                   "Unable to retrieve serial number from drive (name '%s', "
                   "library '%s') in DSS", drive_id->name, drive_id->library);

    /* get loaded tape and check drive status */
    rc = drive_lookup(drive_id->name, drive_id->library, &drive_info);
    if (rc)
        LOG_GOTO(free_dev_res, rc,
                 "Unable to lookup the drive (name '%s', library '%s') to "
                 "unload", drive_id->name, drive_id->library);

    if (drive_info.ldi_full == false) {
        if (!tape_id) {
            pho_verb("Was asked to unload an empty drive (name '%s', library "
                     "'%s'), nothing to do", drive_id->name, drive_id->library);
            GOTO(free_dev_res, rc = 0);
        } else {
            LOG_GOTO(free_dev_res, rc = -EINVAL,
                     "Empty drive (name '%s', library '%s') does not contain "
                     "expected tape (name '%s', library '%s')", drive_id->name,
                     drive_id->library, tape_id->name, tape_id->library);
        }
    }

    if (tape_id) {
        if (!pho_id_equal(tape_id, &drive_info.ldi_medium_id)) {
            LOG_GOTO(free_dev_res, rc = -EINVAL,
                     "drive (name '%s', library '%s') contains tape (name "
                     "'%s', library '%s') instead of expected tape to unload "
                     "(name '%s', library '%s')",
                     drive_id->name, drive_id->library,
                     drive_info.ldi_medium_id.name,
                     drive_info.ldi_medium_id.library,
                     tape_id->name, tape_id->library);
        }
    }

    /* No need to admin lock a failed drive because, it is an unused one. */
    if (dev_res->rsc.adm_status == PHO_RSC_ADM_ST_UNLOCKED) {
        drive_was_unlocked = true;
        rc = phobos_admin_device_lock(adm, drive_id, 1, true);
        if (rc)
            LOG_GOTO(free_dev_res, rc,
                     "Unable to admin lock the drive (name '%s', library '%s') "
                     "before unloading", drive_id->name, drive_id->library);
    }

    /* Lock drive and medium */
    rc = dss_one_medium_get_from_id(&adm->dss, &drive_info.ldi_medium_id,
                                    &med_res);
    if (rc)
        LOG_GOTO(admin_unlock, rc,
                 "Unable to get tape (name '%s', library '%s') from DSS",
                 drive_info.ldi_medium_id.name,
                 drive_info.ldi_medium_id.library);

    rc = fill_host_owner(&hostname, &pid);
    if (rc)
        LOG_GOTO(free_med_res, rc,
                 "Unable to retrieve hostname and pid to check drive and "
                 "medium locks");

    rc = dss_lock(&adm->dss, DSS_DEVICE, dev_res, 1);
    if (rc)
        LOG_GOTO(free_med_res, rc,
                 "admin host '%s' owner/pid '%d' failed to lock the "
                 "drive (id: '%s', library '%s', path: '%s') to unload",
                 hostname, pid, dev_res->rsc.id.name, dev_res->rsc.id.library,
                 dev_res->path);

    rc = dss_check_and_take_lock(&adm->dss, &med_res->rsc.id, &med_res->lock,
                                 DSS_MEDIA, med_res, hostname, pid);
    if (rc)
        LOG_GOTO(unlock_drive, rc,
                 "Unable to lock the tape (name '%s', library '%s')",
                 tape_id->name, tape_id->library);

    /* unload request with the tlc*/
    rc = get_lib_adapter_and_open(PHO_LIB_SCSI, &lib_hdl,
                                  dev_res->rsc.id.library);
    if (rc)
        goto unlock_tape;

    rc = ldm_lib_unload(&lib_hdl, dev_res->rsc.id.name, med_res->rsc.id.name);
    if (rc)
        pho_error(rc,
                  "Admin failed to unload tape (name '%s', library '%s') from "
                  "drive (name '%s', library '%s', path '%s')",
                  med_res->rsc.id.name, med_res->rsc.id.library,
                  dev_res->rsc.id.name, dev_res->rsc.id.library, dev_res->path);

    rc2 = ldm_lib_close(&lib_hdl);
    if (rc2) {
        pho_error(rc2, "Failed to close library of type '%s'",
                  lib_type2str(PHO_LIB_SCSI));
        rc = rc ? : rc2;
    }

unlock_tape:
    rc2 = dss_unlock(&adm->dss, DSS_MEDIA, med_res, 1, false);
    if (rc2) {
        pho_error(rc2, "Unable to unlock tape (name '%s', library '%s')",
                  med_res->rsc.id.name, med_res->rsc.id.library);
        rc = rc ? : rc2;
    }

unlock_drive:
    rc2 = dss_unlock(&adm->dss, DSS_DEVICE, dev_res, 1, false);
    if (rc2) {
        pho_error(rc2,
                  "Unable to unlock drive (id: '%s', library '%s', path: '%s')",
                  dev_res->rsc.id.name, dev_res->rsc.id.library, dev_res->path);
        rc = rc ? : rc2;
    }

free_med_res:
    dss_res_free(med_res, 1);

admin_unlock:
    if (drive_was_unlocked) {
        rc2 = phobos_admin_device_unlock(adm, drive_id, 1, true);
        if (rc2) {
            pho_error(rc2,
                      "Unable to admin unlock the drive (name '%s', library "
                      "'%s') after unloading",
                      drive_id->name, drive_id->library);
            rc = rc ? : rc2;
        }
    }

free_dev_res:
    dss_res_free(dev_res, 1);
    return rc;
}

int phobos_admin_lib_refresh(enum lib_type lib_type, const char *library)
{
    struct lib_handle lib_hdl;
    int rc2;
    int rc;

    ENTRY;

    rc = get_lib_adapter_and_open(lib_type, &lib_hdl, library);
    if (rc)
        return rc;

    rc = ldm_lib_refresh(&lib_hdl);
    if (rc)
        LOG_GOTO(out, rc,
                 "Failed to refresh library '%s'", library);

out:
    rc2 = ldm_lib_close(&lib_hdl);
    if (rc2)
        pho_error(rc2, "Failed to close library");

    rc = rc ? : rc2;

    return rc;
}

static struct media_info *media_from_id_list(struct pho_id *ids, size_t count)
{
    struct media_info *media;
    size_t i;

    media = xcalloc(count, sizeof(*media));

    for (i = 0; i < count; i++)
        pho_id_copy(&media[i].rsc.id, &ids[i]);

    return media;
}

int phobos_admin_notify_media_update(struct admin_handle *adm,
                                     struct pho_id *ids,
                                     size_t count,
                                     lock_conflict_handler_t on_conflict)
{
    struct media_info *media;
    const char *myhostname;
    struct pho_lock *locks;
    int nrc = 0;
    int crc = 0;
    size_t i;
    int rc;

    myhostname = get_hostname();
    if (!myhostname)
        return -errno;

    if (!on_conflict)
        on_conflict = default_conflict_handler;

    for (i = 0; i < count; i++) {
        if (ids[i].family == PHO_RSC_DIR) {
            rc = _normalize_path(ids[i].name);
            if (rc)
                return rc;
        }
    }

    media = media_from_id_list(ids, count);
    locks = xmalloc(sizeof(*locks) * count);

    rc = dss_lock_status(&adm->dss, DSS_MEDIA, media, count, locks);
    if (rc && rc != -ENOLCK)
        goto out_free;

    if (rc == -ENOLCK)
        /* this means that some elements are not locked, ignore this */
        rc = 0;

    for (i = 0; i < count; i++) {
        if (!locks[i].hostname)
            /* not locked, can safely be updated without notification */
            continue;

        if (!strcmp(myhostname, locks[i].hostname)) {
            /* wait for the reply as the update should be quick */
            nrc = _admin_notify(adm, &ids[i], PHO_NTFY_OP_MEDIUM_UPDATE, true);
            rc = rc ? : nrc;
        } else {
            int rc2;

            rc2 = on_conflict(&ids[i], locks[i].hostname);
            crc = rc2 ? : crc;
        }
    }

    rc = rc ? : crc;

out_free:
    free(media);
    free(locks);

    return rc;
}
