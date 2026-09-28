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
    int rc = 0;

    (void) fields;

    g_string_append(
        request,
        "INSERT INTO device (family, model, id, library, host, adm_status, "
        "path) VALUES "
    );

    for (int i = 0; i < item_cnt; ++i) {
        struct dev_info *device = ((struct dev_info *) void_dev) + i;
        char *src_library = NULL;
        char *device_path = NULL;
        char *device_host = NULL;
        char *device_id = NULL;
        char *model = NULL;

        model = dss_char4sql(conn, device->rsc.model);
        if (model == NULL) {
            rc = -EINVAL;
            goto free_info;
        }

        device_id = dss_char4sql(conn, device->rsc.id.name);
        if (device_id == NULL) {
            rc = -EINVAL;
            goto free_info;
        }

        src_library = dss_char4sql(conn, device->rsc.id.library);
        if (src_library == NULL) {
            rc = -EINVAL;
            goto free_info;
        }

        device_host = dss_char4sql(conn, device->host);
        if (device_host == NULL) {
            rc = -EINVAL;
            goto free_info;
        }

        device_path = dss_char4sql(conn, device->path);
        if (device_path == NULL) {
            rc = -EINVAL;
            goto free_info;
        }

        g_string_append_printf(request,
                               "('%s', %s, %s, %s, %s, '%s', %s)",
                               rsc_family2str(device->rsc.id.family),
                               model,
                               device_id,
                               src_library,
                               device_host,
                               rsc_adm_status2str(device->rsc.adm_status),
                               device_path);

        if (i < item_cnt - 1)
            g_string_append(request, ", ");

free_info:
        free_dss_char4sql(model);
        free_dss_char4sql(device_id);
        free_dss_char4sql(src_library);
        free_dss_char4sql(device_host);
        free_dss_char4sql(device_path);

        if (rc)
            return rc;
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

static inline const char *_get_library(void *dev)
{
    return ((struct dev_info *) dev)->rsc.id.library;
}

static struct dss_field FIELDS[] = {
    { DSS_DEVICE_UPDATE_ADM_STATUS, "adm_status = %s", _get_adm_status },
    { DSS_DEVICE_UPDATE_HOST, "host = %s", _get_host },
    { DSS_DEVICE_UPDATE_LIBRARY, "library = %s", _get_library },
};

static int device_update_query(PGconn *conn, void *src_dev, void *dst_dev,
                               int item_cnt, int64_t fields, GString *request)
{
    int rc = 0;

    for (int i = 0; i < item_cnt; ++i) {
        struct dev_info *src = ((struct dev_info *) src_dev) + i;
        struct dev_info *dst = ((struct dev_info *) dst_dev) + i;
        GString *sub_request = g_string_new(NULL);
        char *src_library = NULL;
        char *device_id = NULL;

        g_string_append(sub_request, "UPDATE device SET ");

        update_fields(conn, dst, fields, FIELDS, 3, sub_request);

        device_id = dss_char4sql(conn, src->rsc.id.name);
        if (device_id == NULL) {
            rc = -EINVAL;
            goto free_sub_request;
        }

        src_library = dss_char4sql(conn, src->rsc.id.library);
        if (src_library == NULL) {
            rc = -EINVAL;
            goto free_sub_request;
        }

        g_string_append_printf(sub_request,
                               " WHERE family = '%s' AND id = %s AND "
                               "library = %s; ",
                               rsc_family2str(src->rsc.id.family),
                               device_id, src_library);

        g_string_append(request, sub_request->str);

free_sub_request:
        g_string_free(sub_request, true);
        free_dss_char4sql(device_id);
        free_dss_char4sql(src_library);

        if (rc)
            return rc;
    }

    return 0;
}

static int device_select_query(GString **conditions, int n_conditions,
                               GString *request, struct dss_sort *sort)
{
    g_string_append(request,
                    "SELECT family, model, device.id, device.library,"
                    "       adm_status, host, path"
                    " FROM device");

    /* If we want to sort with a column from the "lock" table, we need to
     * retrieve the "lock" table because the information is not in the "device"
     * table
     */
    if (sort && sort->is_lock)
        g_string_append(request,
                        " LEFT JOIN lock ON lock.id = device.id || '_' || "
                        "                             device.library");

    if (n_conditions == 1)
        g_string_append(request, conditions[0]->str);
    else if (n_conditions >= 2)
        return -ENOTSUP;

    dss_sort2sql(request, sort);
    g_string_append(request, ";");

    return 0;
}

static int device_delete_query(PGconn *conn, void *void_dev, int item_cnt,
                               GString *request)
{
    int rc = 0;

    for (int i = 0; i < item_cnt; ++i) {
        struct dev_info *device = ((struct dev_info *) void_dev) + i;
        char *src_library = NULL;
        char *device_id = NULL;

        device_id = dss_char4sql(conn, device->rsc.id.name);
        if (device_id == NULL) {
            rc = -EINVAL;
            goto free_info;
        }

        src_library = dss_char4sql(conn, device->rsc.id.library);
        if (src_library == NULL) {
            rc = -EINVAL;
            goto free_info;
        }

        g_string_append_printf(request,
                               "DELETE FROM device WHERE family = '%s' AND "
                               "id = %s AND library = %s; ",
                               rsc_family2str(device->rsc.id.family),
                               device_id, src_library);

free_info:
        free_dss_char4sql(device_id);
        free_dss_char4sql(src_library);

        if (rc)
            return rc;
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
    pho_id_name_set(&dev->rsc.id, get_str_value(res, row_num, 2),
                    get_str_value(res, row_num, 3));
    dev->rsc.adm_status = str2rsc_adm_status(PQgetvalue(res, row_num, 4));
    dev->host           = get_str_value(res, row_num, 5);
    dev->path           = get_str_value(res, row_num, 6);
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
