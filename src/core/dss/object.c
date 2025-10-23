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
 * \brief  Object resource file of Phobos's Distributed State Service.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gmodule.h>
#include <libpq-fe.h>

#include "pho_type_utils.h"

#include "deprecated.h"
#include "dss_utils.h"
#include "filters.h"
#include "object.h"

static int object_insert_query(PGconn *conn, void *void_object, int item_cnt,
                               int64_t fields, GString *request)
{
    if (fields & INSERT_OBJECT)
        g_string_append(request,
                        "INSERT INTO object (oid, user_md, _grouping, size) "
                        "VALUES ");
    else
        g_string_append(
            request,
            "INSERT INTO object (oid, object_uuid, version, user_md,"
            " _grouping, size) VALUES "
        );

    for (int i = 0; i < item_cnt; ++i) {
        struct object_info *object = ((struct object_info *) void_object) + i;

        if (fields & INSERT_OBJECT) {
            if (object->grouping)
                g_string_append_printf(request, "('%s', '%s', '%s', '%ld')",
                                       object->oid, object->user_md,
                                       object->grouping, object->size);
            else
                g_string_append_printf(request, "('%s', '%s', NULL, '%ld')",
                                       object->oid, object->user_md,
                                       object->size);
        } else {
            if (object->grouping)
                g_string_append_printf(request,
                                       "('%s', '%s', %d, '%s', '%s', '%ld')",
                                       object->oid, object->uuid,
                                       object->version, object->user_md,
                                       object->grouping, object->size);

            else
                g_string_append_printf(request,
                                       "('%s', '%s', %d, '%s', NULL, '%ld')",
                                       object->oid, object->uuid,
                                       object->version, object->user_md,
                                       object->size);
        }

        if (i < item_cnt - 1)
            g_string_append(request, ", ");
    }

    g_string_append(request, ";");

    return 0;
}

static inline const char *_get_user_md(void *object)
{
    return ((struct object_info *) object)->user_md;
}

static struct dss_field FIELDS[] = {
    { DSS_OBJECT_UPDATE_USER_MD, "user_md = '%s'", _get_user_md },
    { DSS_OBJECT_UPDATE_OID, "oid = '%s'", get_oid },
};

static int object_update_query(PGconn *conn, void *src_object, void *dst_object,
                               int item_cnt, int64_t fields, GString *request)
{
    (void) conn;

    for (int i = 0; i < item_cnt; ++i) {
        struct object_info *src = ((struct object_info *) src_object) + i;
        struct object_info *dst = ((struct object_info *) dst_object) + i;
        GString *sub_request = g_string_new(NULL);

        g_string_append(sub_request, " UPDATE object SET ");

        update_fields(dst, fields, FIELDS, 2, sub_request);

        if (fields == DSS_OBJECT_UPDATE_OID)
            g_string_append_printf(sub_request, " WHERE object_uuid = '%s';",
                                   src->uuid);
        else
            g_string_append_printf(sub_request, " WHERE oid = '%s';",
                                   src->oid);

        g_string_append(request, sub_request->str);
        g_string_free(sub_request, true);
    }

    return 0;
}

static int object_select_query(GString **conditions, int n_conditions,
                               GString *request, struct dss_sort *sort)
{
    g_string_append(request,
                    "SELECT oid, object_uuid, version, user_md, creation_time,"
                    "_grouping, size FROM object");

    if (n_conditions == 1)
        g_string_append(request, conditions[0]->str);
    else if (n_conditions >= 2)
        return -ENOTSUP;

    dss_sort2sql(request, sort);
    g_string_append(request, ";");

    return 0;
}

static int object_delete_query(void *void_object, int item_cnt,
                               GString *request)
{
    for (int i = 0; i < item_cnt; ++i) {
        struct object_info *object = ((struct object_info *) void_object) + i;

        g_string_append_printf(request, "DELETE FROM object WHERE oid = '%s';",
                               object->oid);
    }

    return 0;
}

static int object_from_pg_row(struct dss_handle *handle, void *void_object,
                              PGresult *res, int row_num)
{
    struct object_info *object = void_object;
    int rc;

    (void)handle;

    object->oid         = get_str_value(res, row_num, 0);
    object->uuid        = get_str_value(res, row_num, 1);
    object->version     = atoi(PQgetvalue(res, row_num, 2));
    object->user_md     = get_str_value(res, row_num, 3);
    object->deprec_time.tv_sec = 0;
    object->deprec_time.tv_usec = 0;
    rc = str2timeval(get_str_value(res, row_num, 4), &object->creation_time);
    object->grouping = get_str_value(res, row_num, 5);
    object->size = atoll(PQgetvalue(res, row_num, 6));

    return rc;
}

static void object_result_free(void *void_object)
{
    (void) void_object;
}

const struct dss_resource_ops object_ops = {
    .insert_query = object_insert_query,
    .update_query = object_update_query,
    .select_query = object_select_query,
    .delete_query = object_delete_query,
    .create       = object_from_pg_row,
    .free         = object_result_free,
    .size         = sizeof(struct object_info),
};
