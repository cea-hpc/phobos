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
 * \brief  Deprecated object resource file of Phobos's Distributed State
 *         Service.
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
#include "resources.h"

static int deprecated_insert_query(PGconn *conn, void *void_deprecated,
                                   int item_cnt, int64_t fields,
                                   GString *request)
{
    int rc = 0;

    (void) fields;

    g_string_append(
        request,
        "INSERT INTO deprecated_object (oid, object_uuid, version, user_md,"
        "  _grouping, size) VALUES "
    );

    for (int i = 0; i < item_cnt; ++i) {
        struct object_info *object =
            ((struct object_info *) void_deprecated) + i;
        char *grouping = NULL;
        char *user_md = NULL;
        char *uuid = NULL;
        char *oid = NULL;

        if (object->uuid == NULL)
            LOG_RETURN(-EINVAL, "Object uuid cannot be NULL");

        if (object->version < 1)
            LOG_RETURN(-EINVAL, "Object version must be strictly positive");

        oid = dss_char4sql(conn, object->oid);
        if (oid == NULL) {
            rc = -EINVAL;
            goto free_info;
        }

        uuid = dss_char4sql(conn, object->uuid);
        if (uuid == NULL) {
            rc = -EINVAL;
            goto free_info;
        }

        user_md = dss_char4sql(conn, object->user_md);
        if (user_md == NULL) {
            rc = -EINVAL;
            goto free_info;
        }

        grouping = dss_char4sql(conn, object->grouping);
        if (grouping == NULL) {
            rc = -EINVAL;
            goto free_info;
        }

        g_string_append_printf(request, "(%s, %s, %d, %s, %s, '%ld')",
                               oid, uuid, object->version, user_md,
                               grouping, object->size);

        if (i < item_cnt - 1)
            g_string_append(request, ", ");

free_info:
        free_dss_char4sql(grouping);
        free_dss_char4sql(user_md);
        free_dss_char4sql(uuid);
        free_dss_char4sql(oid);

        if (rc)
            return rc;
    }

    g_string_append(request, ";");

    return 0;
}

static inline const char *_get_user_md(void *object)
{
    return ((struct object_info *) object)->user_md;
}

static struct dss_field FIELDS[] = {
    { DSS_OBJECT_UPDATE_USER_MD, "user_md = %s", _get_user_md },
    { DSS_OBJECT_UPDATE_OID, "oid = %s", get_oid },
};

static int deprecated_update_query(PGconn *conn, void *src_deprecated,
                                   void *dst_deprecated, int item_cnt,
                                   int64_t fields, GString *request)
{
    int rc = 0;

    for (int i = 0; i < item_cnt; ++i) {
        struct object_info *src = ((struct object_info *) src_deprecated) + i;
        struct object_info *dst = ((struct object_info *) dst_deprecated) + i;

        GString *sub_request = g_string_new(NULL);
        char *src_uuid = NULL;

        g_string_append(sub_request, "UPDATE deprecated_object SET ");

        update_fields(conn, dst, fields, FIELDS, 2, sub_request);

        src_uuid = dss_char4sql(conn, src->uuid);
        if (src_uuid == NULL) {
            rc = -EINVAL;
            goto free_info;
        }

        g_string_append_printf(sub_request,
                               " WHERE object_uuid = %s AND version = %d;",
                               src_uuid, src->version);

        g_string_append(request, sub_request->str);

free_info:
        g_string_free(sub_request, true);
        free_dss_char4sql(src_uuid);

        if (rc)
            return rc;
    }

    return 0;
}

static int deprecated_select_query(GString **conditions, int n_conditions,
                                   GString *request, struct dss_sort *sort)
{
    g_string_append(request,
                    "SELECT oid, object_uuid, version, user_md, creation_time,"
                    "  _grouping, size, deprec_time FROM deprecated_object");

    if (n_conditions == 1)
        g_string_append(request, conditions[0]->str);
    else if (n_conditions >= 2)
        return -ENOTSUP;

    dss_sort2sql(request, sort);
    g_string_append(request, ";");

    return 0;
}

static int deprecated_delete_query(PGconn *conn, void *void_deprecated,
                                   int item_cnt, GString *request)
{
    int rc = 0;

    for (int i = 0; i < item_cnt; ++i) {
        struct object_info *object =
            ((struct object_info *) void_deprecated) + i;
        char *uuid = dss_char4sql(conn, object->uuid);

        if (uuid == NULL) {
            rc = -EINVAL;
            goto free_info;
        }

        g_string_append_printf(request,
                               "DELETE FROM deprecated_object"
                               " WHERE object_uuid = %s AND version = '%d';",
                               uuid, object->version);

free_info:
        free_dss_char4sql(uuid);

        if (rc)
            return rc;
    }

    return 0;
}

/**
 * The creation of a deprecated object is the exact same process as for a
 * regular object, but with the "deprecated time" added.
 */
static int deprecated_from_pg_row(struct dss_handle *handle, void *void_object,
                                  PGresult *res, int row_num)
{
    struct object_info *object = void_object;
    char *deprec_time;
    int rc;

    rc = create_resource(DSS_OBJECT, handle, void_object, res, row_num);
    deprec_time = get_str_value(res, row_num, 7);

    if (deprec_time)
        rc = rc ? : str2timeval(deprec_time, &object->deprec_time);

    return rc;
}

static void deprecated_result_free(void *void_object)
{
    (void) void_object;
}

/**
 * The update query function is NULL because a deprecated object cannot be
 * updated
 */
const struct dss_resource_ops deprecated_ops = {
    .insert_query = deprecated_insert_query,
    .update_query = deprecated_update_query,
    .select_query = deprecated_select_query,
    .delete_query = deprecated_delete_query,
    .create       = deprecated_from_pg_row,
    .free         = deprecated_result_free,
    .size         = sizeof(struct object_info),
};
