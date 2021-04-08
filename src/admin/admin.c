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

#include <errno.h>
#include <unistd.h>

#include "pho_cfg.h"
#include "pho_comm.h"
#include "pho_common.h"
#include "pho_dss.h"
#include "pho_ldm.h"
#include "pho_srl_lrs.h"
#include "pho_type_utils.h"
#include "admin_utils.h"

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

int _send_and_receive(struct admin_handle *adm, pho_req_t *req,
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

static int _add_device_in_dss(struct admin_handle *adm, struct pho_id *dev_ids,
                              unsigned int num_dev, bool keep_locked)
{
    char host_name[HOST_NAME_MAX + 1];
    enum rsc_adm_status status;
    struct ldm_dev_state lds = {};
    struct dev_info *devices;
    struct dev_adapter deva;
    char *real_path;
    int rc;
    int i;

    rc = get_dev_adapter(dev_ids[0].family, &deva);
    if (rc)
        LOG_RETURN(rc, "Cannot get device adapter");

    rc = gethostname(host_name, HOST_NAME_MAX);
    if (rc)
        LOG_RETURN(rc, "Cannot get host name");
    if (strchr(host_name, '.'))
        *strchr(host_name, '.') = '\0';

    devices = calloc(num_dev, sizeof(*devices));
    if (!devices)
        LOG_RETURN(errno, "Device info allocation failed");

    for (i = 0; i < num_dev; ++i) {
        struct dev_info *devi = devices + i;

        real_path = realpath(dev_ids[i].name, NULL);
        if (!real_path)
            LOG_GOTO(out_free, rc = -errno,
                     "Cannot get the real path of device '%s'",
                     dev_ids[i].name);

        rc = ldm_dev_query(&deva, real_path, &lds);
        free(real_path);
        if (rc)
            LOG_GOTO(out_free, rc, "Failed to query device '%s'",
                     dev_ids[i].name);

        status = keep_locked ? PHO_RSC_ADM_ST_LOCKED : PHO_RSC_ADM_ST_UNLOCKED;

        devi->rsc.id.family = dev_ids[i].family;
        pho_id_name_set(&devi->rsc.id, lds.lds_serial);
        devi->rsc.adm_status = status;
        devi->host = host_name;

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

    rc = dss_device_set(&adm->dss, devices, num_dev, DSS_SET_INSERT);
    if (rc)
        LOG_GOTO(out_free, rc, "Cannot add devices");


out_free:
    for (i = 0; i < num_dev; ++i) {
        free(devices[i].rsc.model);
        free(devices[i].path);
    }
    free(devices);

    return rc;
}

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
                      dev_res->lock.owner);
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
        dev_info_cpy(&devices[avail_devices++], dev_res);

        dss_res_free(dev_res, 1);
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
    if (avail_devices)
        dss_device_unlock(&adm->dss, devices, avail_devices, adm->lock_owner);

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

int phobos_admin_init(struct admin_handle *adm, bool lrs_required)
{
    const char *sock_path;
    int rc;

    memset(adm, 0, sizeof(*adm));
    adm->comm = pho_comm_info_init();

    rc = dss_init_lock_owner(&adm->lock_owner);
    if (rc)
        LOG_GOTO(out, rc, "Cannot allocate lock_owner");

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

    if (!adm->daemon_is_online)
        return 0;

    for (i = 0; i < num_dev; ++i) {
        int rc2;

        rc2 = _admin_notify(adm, dev_ids + i, PHO_NTFY_OP_DEVICE_ADD);
        if (rc2)
            pho_error(rc2, "Failure during daemon notification for '%s'",
                      dev_ids[i].name);
        rc = rc ? : rc2;
    }

    return rc;
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

int phobos_admin_ping(struct admin_handle *adm)
{
    pho_resp_t *resp;
    pho_req_t req;
    int rid = 1;
    int rc;

    rc = pho_srl_request_ping_alloc(&req);
    if (rc)
        LOG_RETURN(rc, "Cannot create ping request");

    req.id = rid;

    rc = _send_and_receive(adm, &req, &resp);
    if (rc)
        LOG_RETURN(rc, "Error with phobosd communication");

    /* expect ping response, send error otherwise */
    if (!(pho_response_is_ping(resp) && resp->req_id == rid))
        pho_error(rc = -EBADMSG, "Bad response from phobosd");

    pho_srl_response_free(resp, false);

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
    g_string_append_printf(medium_str,
                           "{\"$INJSON\": "
                             "{\"DSS::EXT::media_idx\": \"%s\"}}",
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
    struct dss_filter *filter_ptr = NULL;
    struct dss_filter filter;
    bool medium_is_valid;
    GString *extent_str;
    GString *medium_str;
    int rc = 0;

    extent_str = g_string_new(NULL);
    medium_str = g_string_new(NULL);
    medium_is_valid = (medium && strcmp(medium, ""));

    /**
     * If a medium is specified, we construct a string to filter it.
     */
    if (medium_is_valid)
        phobos_construct_medium(medium_str, medium);

    /**
     * If there are at least one resource, we construct a string containing
     * each request.
     */
    if (n_res)
        phobos_construct_extent(extent_str, res, n_res, is_pattern);

    if (n_res || medium_is_valid) {
        /**
         * Finally, if the request has at least a medium or one resource,
         * we build the filter in the following way: if there is one
         * medium and resource, then using an AND is necessary
         * (which correspond to the first and last "%s").
         * After that, we add to the filter the resource and medium
         * if any is present, which are the second and fourth "%s".
         * Finally, is both are present, a comma is necessary.
         */
        rc = dss_filter_build(&filter,
                              "%s %s %s %s %s",
                              n_res && medium_is_valid ? "{\"$AND\": [" : "",
                              extent_str->str != NULL ? extent_str->str : "",
                              n_res && medium_is_valid ? ", " : "",
                              medium_str->str != NULL ? medium_str->str : "",
                              n_res && medium_is_valid ? "]}" : "");

        g_string_free(extent_str, TRUE);
        g_string_free(medium_str, TRUE);

        filter_ptr = &filter;
    }

    if (rc)
        return rc;

    /**
     * If no resource or medium was asked for, then using a filter isn't
     * necessary, thus passing it as NULL to dss_layout_get ensures
     * the expected behaviour.
     */
    rc = dss_layout_get(&adm->dss, filter_ptr, layouts, n_layouts);
    if (rc)
        pho_error(rc, "Cannot fetch layouts");

    dss_filter_free(filter_ptr);

    return rc;
}

void phobos_admin_layout_list_free(struct layout_info *layouts, int n_layouts)
{
    dss_res_free(layouts, n_layouts);
}
