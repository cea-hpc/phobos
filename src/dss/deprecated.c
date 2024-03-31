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
        " obj_status) VALUES "
    );

    for (int i = 0; i < item_cnt; ++i) {
        struct object_info *object =
            ((struct object_info *) void_deprecated) + i;

        if (object->uuid == NULL)
            LOG_RETURN(-EINVAL, "Object uuid cannot be NULL");

        if (object->version < 1)
            LOG_RETURN(-EINVAL, "Object version must be strictly positive");

        g_string_append_printf(request,
                               "('%s', '%s', %d, '%s', '%s')",
                               object->oid, object->uuid,
                               object->version, object->user_md,
                               obj_status2str(object->obj_status));

        if (i < item_cnt - 1)
            g_string_append(request, ", ");
    }

    g_string_append(request, ";");

    return 0;
}

static int deprecated_update_query(PGconn *conn, void *void_deprecated,
                                   int item_cnt, int64_t update_fields,
                                   GString *request)
{
    (void) update_fields;

    for (int i = 0; i < item_cnt; ++i) {
        struct object_info *deprecated =
            ((struct object_info *) void_deprecated) + i;
        GString *sub_request = g_string_new(NULL);

        g_string_append_printf(sub_request,
                               "UPDATE deprecated_object SET obj_status = '%s'"
                               " WHERE oid = '%s';",
                               obj_status2str(deprecated->obj_status),
                               deprecated->oid);

        g_string_append(request, sub_request->str);
        g_string_free(sub_request, true);
    }

    return 0;
}

static int deprecated_select_query(GString *conditions, GString *request)
{
    g_string_append(request,
                    "SELECT oid, object_uuid, version, user_md, obj_status,"
                    " deprec_time FROM deprecated_object");

    if (conditions)
        g_string_append(request, conditions->str);

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

static int deprecated_from_pg_row(struct dss_handle *handle, void *void_object,
                                  PGresult *res, int row_num)
{
    struct object_info *object = void_object;
    int rc;

    rc = create_resource(DSS_OBJECT, handle, void_object, res, row_num);
    rc = rc ? : str2timeval(get_str_value(res, row_num, 5),
                            &object->deprec_time);

    return rc;
}

static void deprecated_result_free(void *void_object)
{
    (void) void_object;
}

const struct dss_resource_ops deprecated_ops = {
    .insert_query = deprecated_insert_query,
    .update_query = deprecated_update_query,
    .select_query = deprecated_select_query,
    .delete_query = deprecated_delete_query,
    .create       = deprecated_from_pg_row,
    .free         = deprecated_result_free,
    .size         = sizeof(struct object_info),
};
