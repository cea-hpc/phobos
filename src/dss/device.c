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
 * \brief  Device resource file of Phobos's Distributed State Service.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gmodule.h>
#include <libpq-fe.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include "pho_dss.h"
#include "pho_types.h"
#include "pho_type_utils.h"

#include "device.h"
#include "dss_utils.h"
#include "logs.h"

static int device_insert_query(PGconn *conn, void *void_dev, int item_cnt,
                               int64_t fields, GString *request)
{
    (void) fields;

    g_string_append(
        request,
        "INSERT INTO device (family, model, id, host, adm_status, path) "
        " VALUES "
    );

    for (int i = 0; i < item_cnt; ++i) {
        struct dev_info *device = ((struct dev_info *) void_dev) + i;
        char *model;

        model = dss_char4sql(conn, device->rsc.model);
        if (!model)
            return -EINVAL;

        g_string_append_printf(request,
                               "('%s', %s, '%s', '%s', '%s', '%s')",
                               rsc_family2str(device->rsc.id.family),
                               model,
                               device->rsc.id.name,
                               device->host,
                               rsc_adm_status2str(device->rsc.adm_status),
                               device->path);

        if (i < item_cnt - 1)
            g_string_append(request, ", ");

        free_dss_char4sql(model);
    }

    g_string_append(request, ";");

    return 0;
}

static inline const char *_get_adm_status(void *dev)
{
    return rsc_adm_status2str(((struct dev_info *) dev)->rsc.adm_status);
}

static inline const char *_get_host(void *dev)
{
    return ((struct dev_info *) dev)->host;
}

static struct dss_field FIELDS[] = {
    { DSS_DEVICE_UPDATE_ADM_STATUS, "adm_status = '%s'", _get_adm_status },
    { DSS_DEVICE_UPDATE_HOST, "host = '%s'", _get_host }
};

static int device_update_query(PGconn *conn, void *void_dev, int item_cnt,
                               int64_t fields, GString *request)
{
    (void) conn;

    for (int i = 0; i < item_cnt; ++i) {
        struct dev_info *device = ((struct dev_info *) void_dev) + i;
        enum dss_device_operations _fields = fields;
        GString *sub_request = g_string_new(NULL);

        g_string_append(sub_request, "UPDATE device SET ");

        update_fields(device, _fields, FIELDS, 2, sub_request);

        g_string_append_printf(sub_request, "WHERE id = '%s'; ",
                               device->rsc.id.name);

        g_string_append(request, sub_request->str);
        g_string_free(sub_request, true);
    }

    return 0;
}

static int device_select_query(GString *conditions, GString *request)
{
    g_string_append(request,
                    "SELECT family, model, id, adm_status, host, path"
                    " FROM device");

    if (conditions)
        g_string_append(request, conditions->str);

    g_string_append(request, ";");

    return 0;
}

static int device_delete_query(void *void_dev, int item_cnt, GString *request)
{
    for (int i = 0; i < item_cnt; ++i) {
        struct dev_info *device = ((struct dev_info *) void_dev) + i;

        g_string_append_printf(request, "DELETE FROM device WHERE id = '%s'; ",
                               device->rsc.id.name);
    }

    return 0;
}

static int device_from_pg_row(struct dss_handle *handle, void *void_dev,
                              PGresult *res, int row_num)
{
    struct dev_info *dev = void_dev;
    int rc;

    dev->rsc.id.family  = str2rsc_family(PQgetvalue(res, row_num, 0));
    dev->rsc.model      = get_str_value(res, row_num, 1);
    pho_id_name_set(&dev->rsc.id, get_str_value(res, row_num, 2));
    dev->rsc.adm_status = str2rsc_adm_status(PQgetvalue(res, row_num, 3));
    dev->host           = get_str_value(res, row_num, 4);
    dev->path           = get_str_value(res, row_num, 5);
    dev->health         = 0;

    rc = dss_lock_status(handle, DSS_DEVICE, dev, 1, &dev->lock);
    if (rc == -ENOLCK)
        rc = 0;

    return rc;
}

static void device_result_free(void *void_dev)
{
    struct dev_info *device = void_dev;

    pho_lock_clean(&device->lock);
}

const struct dss_resource_ops device_ops = {
    .insert_query = device_insert_query,
    .update_query = device_update_query,
    .select_query = device_select_query,
    .delete_query = device_delete_query,
    .create       = device_from_pg_row,
    .free         = device_result_free,
    .size         = sizeof(struct dev_info),
};

int dss_get_usable_devices(struct dss_handle *hdl, const enum rsc_family family,
                           const char *host, struct dev_info **dev_ls,
                           int *dev_cnt)
{
    struct dss_filter filter;
    char *host_filter = NULL;
    int rc;

    if (host) {
        rc = asprintf(&host_filter, "{\"DSS::DEV::host\": \"%s\"},", host);
        if (rc < 0)
            return -ENOMEM;
    }

    rc = dss_filter_build(&filter,
                          "{\"$AND\": ["
                          "  %s"
                          "  {\"DSS::DEV::adm_status\": \"%s\"},"
                          "  {\"DSS::DEV::family\": \"%s\"}"
                          "]}",
                          host ? host_filter : "",
                          rsc_adm_status2str(PHO_RSC_ADM_ST_UNLOCKED),
                          rsc_family2str(family));
    free(host_filter);
    if (rc)
        return rc;

    rc = dss_device_get(hdl, &filter, dev_ls, dev_cnt);
    dss_filter_free(&filter);
    return rc;
}

int dss_device_health(struct dss_handle *dss, const struct pho_id *device_id,
                      size_t max_health, size_t *health)
{
    return dss_resource_health(dss, device_id, DSS_DEVICE, max_health, health);
}
