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
 * \brief  Copy resource file of Phobos's Distributed State Service.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gmodule.h>
#include <libpq-fe.h>

#include "pho_type_utils.h"

#include "dss_utils.h"
#include "filters.h"
#include "copy.h"

static int copy_insert_query(PGconn *conn, void *void_copy, int item_cnt,
                             int64_t fields, GString *request)
{
    (void) fields;

    g_string_append(request,
                    "INSERT INTO copy (object_uuid, version, copy_name,"
                    " copy_status) VALUES ");

    for (int i = 0; i < item_cnt; ++i) {
        struct copy_info *copy = ((struct copy_info *) void_copy) + i;

        if (copy->object_uuid == NULL)
            LOG_RETURN(-EINVAL, "Copy object_uuid cannot be NULL");

        if (copy->version < 1)
            LOG_RETURN(-EINVAL, "Copy version must be strictly positive");

        if (copy->copy_name == NULL)
            LOG_RETURN(-EINVAL, "Copy name cannot be NULL");

        g_string_append_printf(request, "('%s', '%d', '%s', '%s')%s",
                               copy->object_uuid, copy->version,
                               copy->copy_name,
                               copy_status2str(copy->copy_status),
                               i < item_cnt - 1 ? ", " : ";");
    }

    return 0;
}

static struct dss_field FIELDS[] = {
    { DSS_COPY_UPDATE_ACCESS_TIME, "access_time = '%s'", get_access_time },
    { DSS_COPY_UPDATE_COPY_STATUS, "copy_status = '%s'", get_copy_status },
};

static int copy_update_query(PGconn *conn, void *src_copy, void *dst_copy,
                             int item_cnt, int64_t fields, GString *request)
{
    (void) conn;

    for (int i = 0; i < item_cnt; ++i) {
        struct copy_info *src = ((struct copy_info *) src_copy) + i;
        struct copy_info *dst = ((struct copy_info *) dst_copy) + i;
        GString *sub_request = g_string_new(NULL);

        g_string_append(sub_request, " UPDATE copy SET ");

        update_fields(dst, fields, FIELDS, 2, sub_request);

        g_string_append_printf(sub_request,
                               " WHERE object_uuid = '%s' AND version = '%d'"
                               " AND copy_name = '%s';", src->object_uuid,
                               src->version, src->copy_name);
        g_string_append(request, sub_request->str);
        g_string_free(sub_request, true);
    }

    return 0;
}

static int copy_select_query(GString **conditions, int n_conditions,
                             GString *request, struct dss_sort *sort)
{
    g_string_append(request,
                    "SELECT object_uuid, version, copy_name, copy_status,"
                    " creation_time, access_time FROM copy");

    if (n_conditions == 1)
        g_string_append(request, conditions[0]->str);
    else if (n_conditions >= 2)
        return -ENOTSUP;

    dss_sort2sql(request, sort);
    g_string_append(request, ";");

    return 0;
}

static int copy_delete_query(void *void_copy, int item_cnt, GString *request)
{
    for (int i = 0; i < item_cnt; ++i) {
        struct copy_info *copy = ((struct copy_info *) void_copy) + i;

        g_string_append_printf(request,
                               "DELETE FROM copy WHERE object_uuid = '%s'"
                               " AND version = '%d' AND copy_name = '%s';",
                               copy->object_uuid, copy->version,
                               copy->copy_name);
    }

    return 0;
}

static int copy_from_pg_row(struct dss_handle *handle, void *void_copy,
                            PGresult *res, int row_num)
{
    struct copy_info *copy = void_copy;
    int rc;

    (void)handle;

    copy->object_uuid = get_str_value(res, row_num, 0);
    copy->version     = atoi(PQgetvalue(res, row_num, 1));
    copy->copy_name   = get_str_value(res, row_num, 2);
    copy->copy_status = str2copy_status(PQgetvalue(res, row_num, 3));
    rc = str2timeval(get_str_value(res, row_num, 4), &copy->creation_time);
    rc = rc ? : str2timeval(get_str_value(res, row_num, 5), &copy->access_time);

    return rc;
}

static void copy_result_free(void *void_copy)
{
    (void) void_copy;
}

const struct dss_resource_ops copy_ops = {
    .insert_query = copy_insert_query,
    .update_query = copy_update_query,
    .select_query = copy_select_query,
    .delete_query = copy_delete_query,
    .create       = copy_from_pg_row,
    .free         = copy_result_free,
    .size         = sizeof(struct copy_info),
};

