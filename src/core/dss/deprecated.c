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
    (void) fields;

    g_string_append(
        request,
        "INSERT INTO deprecated_object (oid, object_uuid, version, user_md,"
        "  _grouping, size) VALUES "
    );

    for (int i = 0; i < item_cnt; ++i) {
        struct object_info *object =
            ((struct object_info *) void_deprecated) + i;

        if (object->uuid == NULL)
            LOG_RETURN(-EINVAL, "Object uuid cannot be NULL");

        if (object->version < 1)
            LOG_RETURN(-EINVAL, "Object version must be strictly positive");

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

        if (i < item_cnt - 1)
            g_string_append(request, ", ");
    }

    g_string_append(request, ";");

    return 0;
}

static struct dss_field FIELDS[] = {
    { DSS_OBJECT_UPDATE_OID, "oid = '%s'", get_oid },
};

static int deprecated_update_query(PGconn *conn, void *src_deprecated,
                                   void *dst_deprecated, int item_cnt,
                                   int64_t fields, GString *request)
{
    (void) conn;

    for (int i = 0; i < item_cnt; ++i) {
        struct object_info *src = ((struct object_info *) src_deprecated) + i;
        struct object_info *dst = ((struct object_info *) dst_deprecated) + i;

        GString *sub_request = g_string_new(NULL);

        g_string_append(sub_request, "UPDATE deprecated_object SET ");

        update_fields(dst, fields, FIELDS, 1, sub_request);

        g_string_append_printf(sub_request,
                               " WHERE object_uuid = '%s' AND version = %d;",
                               src->uuid, src->version);

        g_string_append(request, sub_request->str);
        g_string_free(sub_request, true);
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

static int deprecated_delete_query(void *void_deprecated, int item_cnt,
                                   GString *request)
{
    for (int i = 0; i < item_cnt; ++i) {
        struct object_info *object =
            ((struct object_info *) void_deprecated) + i;

        g_string_append_printf(request,
                               "DELETE FROM deprecated_object"
                               " WHERE object_uuid = '%s' AND version = '%d';",
                               object->uuid, object->version);
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
