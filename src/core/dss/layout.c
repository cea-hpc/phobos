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
 * \brief  Layout resource file of Phobos's Distributed State Service.
 */

#include <jansson.h>
#include <libpq-fe.h>

#include "dss_utils.h"
#include "layout.h"

/**
 * Encode a layout description to JSON.
 *
 * The description encompass the layout name, major and minor version numbers,
 * and optionnally, any additional attributes.
 *
 * \param[in] desc        The layout module description to encode
 *
 * \return The encoded layout description as JSON on success (the result should
 *         be freed by the caller)
 *         NULL on error
 */
static char *dss_layout_desc_encode(struct module_desc *desc)
{
    json_t *attrs  = NULL;
    char *result = NULL;
    json_t *root;
    int rc = 0;

    ENTRY;

    root = json_object();
    if (!root) {
        pho_error(-ENOMEM, "Failed to create json object");
        return NULL;
    }

    rc = json_object_set_new(root, PHO_MOD_DESC_KEY_NAME,
                             json_string(desc->mod_name));
    if (rc)
        LOG_GOTO(out_free, rc = -EINVAL, "Cannot set layout name");

    rc = json_object_set_new(root, PHO_MOD_DESC_KEY_MAJOR,
                             json_integer(desc->mod_major));
    if (rc)
        LOG_GOTO(out_free, rc = -EINVAL, "Cannot set layout major number");

    rc = json_object_set_new(root, PHO_MOD_DESC_KEY_MINOR,
                             json_integer(desc->mod_minor));
    if (rc)
        LOG_GOTO(out_free, rc = -EINVAL, "Cannot set layout minor number");

    if (!pho_attrs_is_empty(&desc->mod_attrs)) {
        attrs = json_object();
        if (!attrs)
            LOG_GOTO(out_free, rc = -ENOMEM, "Cannot set layout attributes");

        rc = pho_attrs_to_json_raw(&desc->mod_attrs, attrs);
        if (rc)
            LOG_GOTO(out_free, rc, "Cannot convert layout attributes");

        rc = json_object_set_new(root, PHO_MOD_DESC_KEY_ATTRS, attrs);
        if (rc)
            LOG_GOTO(out_free, rc = -EINVAL, "Cannot set layout attributes");
    }

    result = json_dumps(root, 0);

    pho_debug("Created json representation for layout type: '%s'",
              result ? result : "(null)");

out_free:
    json_decref(root); /* if attrs, it is included in root */
    return result;
}

static int layout_insert_query(PGconn *conn, void *void_layout, int item_cnt,
                               int64_t fields, GString *request)
{
    int rc = 0;

    (void) fields;

    g_string_append(
        request,
        "INSERT INTO layout (object_uuid, version, extent_uuid, layout_index,"
        " copy_name) VALUES "
    );

    for (int i = 0; i < item_cnt; ++i) {
        struct layout_info *layout = ((struct layout_info *) void_layout) + i;
        char *copy_name = NULL;
        char *uuid = NULL;

        uuid = dss_char4sql(conn, layout->uuid);
        if (uuid == NULL) {
            rc = -EINVAL;
            goto free_info;
        }

        copy_name = dss_char4sql(conn, layout->copy_name);
        if (copy_name == NULL) {
            rc = -EINVAL;
            goto free_info;
        }

        for (int j = 0; j < layout->ext_count; ++j) {
            struct extent *extent = &layout->extents[j];
            char *extent_uuid = dss_char4sql(conn, extent->uuid);

            if (extent_uuid == NULL) {
                rc = -EINVAL;
                goto free_info;
            }

            g_string_append_printf(
                request, "(%s, %d, %s, %d, %s)",
                uuid, layout->version, extent_uuid,
                extent->layout_idx, copy_name
            );

            free_dss_char4sql(extent_uuid);

            if (j < layout->ext_count - 1)
                g_string_append(request, ", ");
        }

        if (i < item_cnt - 1)
            g_string_append(request, ", ");

free_info:
        free_dss_char4sql(copy_name);
        free_dss_char4sql(uuid);

        if (rc)
            return rc;
    }

    g_string_append(request, ";");

    for (int i = 0; i < item_cnt; ++i) {
        struct layout_info *layout = ((struct layout_info *) void_layout) + i;
        char *layout_description = NULL;
        char *copy_name = NULL;
        char *uuid = NULL;
        char *desc = NULL;

        layout_description = dss_layout_desc_encode(&layout->layout_desc);
        if (!layout_description)
            LOG_RETURN(-EINVAL, "JSON layout desc encoding error");

        desc = dss_char4sql(conn, layout_description);
        if (desc == NULL) {
            rc = -EINVAL;
            goto free_lyt_info;
        }

        uuid = dss_char4sql(conn, layout->uuid);
        if (uuid == NULL) {
            rc = -EINVAL;
            goto free_lyt_info;
        }

        copy_name = dss_char4sql(conn, layout->copy_name);
        if (copy_name == NULL) {
            rc = -EINVAL;
            goto free_lyt_info;
        }

        g_string_append_printf(
            request,
            "UPDATE copy SET lyt_info = %s WHERE "
            "object_uuid = %s AND version = %d AND copy_name = %s;",
            desc, uuid, layout->version, copy_name
        );

free_lyt_info:
        free(layout_description);
        free_dss_char4sql(copy_name);
        free_dss_char4sql(uuid);
        free_dss_char4sql(desc);

        if (rc)
            return rc;
    }

    return 0;
}

static int layout_select_query(GString **conditions, int n_conditions,
                               GString *request, struct dss_sort *sort)
{
    g_string_append(request,
                    "SELECT oid, object_uuid, version, lyt_info, copy_name,"
                    " json_agg(extent_uuid ORDER BY layout_index)"
                    " FROM layout"
                    " LEFT JOIN ("
                    "  SELECT oid, object_uuid, version, lyt_info, copy_name"
                    "  FROM copy LEFT JOIN ("
                    "    SELECT oid, object_uuid, version FROM object"
                    "    UNION SELECT oid, object_uuid, version FROM deprecated_object)"
                    "    AS tmpO USING (object_uuid, version)"
                    ") AS inner_table USING (object_uuid, version, copy_name)");

    if (n_conditions == 1)
        g_string_append(request, conditions[0]->str);
    else if (n_conditions >= 2)
        return -ENOTSUP;

    g_string_append(request,
                    " GROUP BY oid, object_uuid, version, lyt_info,"
                    " copy_name;");

    return 0;
}

static int layout_delete_query(PGconn *conn, void *void_layout, int item_cnt,
                               GString *request)
{
    (void) conn;

    for (int i = 0; i < item_cnt; ++i) {
        struct layout_info *layout = ((struct layout_info *) void_layout) + i;

        g_string_append_printf(request,
                               "DELETE FROM layout"
                               " WHERE object_uuid = '%s'"
                               "  AND version = '%d' AND copy_name = '%s'%s",
                               layout->uuid, layout->version,
                               layout->copy_name,
                               layout->ext_count ? " AND (" : "");

        for (int j = 0; j < layout->ext_count; j++)
            g_string_append_printf(request, "extent_uuid = '%s'%s",
                                   layout->extents[j].uuid,
                                   j + 1 < layout->ext_count ? " OR " : "");

        g_string_append_printf(request, "%s;", layout->ext_count ? ")" : "");
    }

    return 0;
}

int layout_desc_decode(struct module_desc *desc, const char *json)
{
    json_error_t json_error;
    json_t *attrs;
    json_t *root;
    int rc = 0;

    ENTRY;

    pho_debug("Decoding JSON representation for module desc: '%s'", json);

    memset(desc, 0, sizeof(*desc));

    root = json_loads(json, JSON_REJECT_DUPLICATES, &json_error);
    if (!root)
        LOG_RETURN(-EINVAL, "Failed to parse json data: %s", json_error.text);

    if (!json_is_object(root))
        LOG_GOTO(out_free, rc = -EINVAL, "Invalid module description");

    /* Mandatory fields */
    desc->mod_name  = json_dict2str(root, PHO_MOD_DESC_KEY_NAME);
    if (!desc->mod_name)
        LOG_GOTO(out_free, rc = -EINVAL, "Missing attribute %s",
                 PHO_MOD_DESC_KEY_NAME);

    desc->mod_major = json_dict2int(root, PHO_MOD_DESC_KEY_MAJOR);
    if (desc->mod_major < 0)
        LOG_GOTO(out_free, rc = -EINVAL, "Missing attribute %s",
                 PHO_MOD_DESC_KEY_MAJOR);

    desc->mod_minor = json_dict2int(root, PHO_MOD_DESC_KEY_MINOR);
    if (desc->mod_minor < 0)
        LOG_GOTO(out_free, rc = -EINVAL, "Missing attribute %s",
                 PHO_MOD_DESC_KEY_MINOR);

    /* Optional attributes */
    attrs = json_object_get(root, PHO_MOD_DESC_KEY_ATTRS);
    if (!attrs) {
        rc = 0;
        goto out_free;  /* no attributes, nothing else to do */
    }

    if (!json_is_object(attrs))
        LOG_GOTO(out_free, rc = -EINVAL, "Invalid attributes format");

    pho_json_raw_to_attrs(&desc->mod_attrs, attrs);

out_free:
    if (rc) {
        free(desc->mod_name);
        memset(desc, 0, sizeof(*desc));
    }

    json_decref(root);
    return rc;
}

static int layout_from_pg_row(struct dss_handle *handle, void *void_layout,
                              PGresult *res, int row_num)
{
    struct layout_info *layout = void_layout;
    struct extent *extents = NULL;
    json_error_t json_error;
    size_t count = 0;
    json_t *root;
    int rc;
    int i;

    (void) handle;

    layout->oid = PQgetvalue(res, row_num, 0);
    layout->uuid = PQgetvalue(res, row_num, 1);
    layout->version = atoi(PQgetvalue(res, row_num, 2));
    rc = layout_desc_decode(&layout->layout_desc, PQgetvalue(res, row_num, 3));
    layout->copy_name = PQgetvalue(res, row_num, 4);
    pho_debug("Decoding JSON representation for extents: '%s'",
              PQgetvalue(res, row_num, 5));

    root = json_loads(PQgetvalue(res, row_num, 5), JSON_REJECT_DUPLICATES,
                      &json_error);
    if (!root)
        LOG_RETURN(-EINVAL, "Failed to parse json data: %s", json_error.text);

    if (!json_is_array(root))
        LOG_GOTO(out, rc = -EINVAL, "Invalid extents description");

    count = json_array_size(root);
    if (count == 0)
        LOG_GOTO(out, rc = -EINVAL,
                 "json parser: extents array is empty");

    extents = xcalloc(count, sizeof(*extents));
    for (i = 0; i < count; i++) {
        extents[i].layout_idx = i;
        extents[i].uuid = xstrdup(json_string_value(json_array_get(root, i)));
    }

out:
    json_decref(root);
    layout->ext_count = count;
    layout->extents = extents;

    return rc;
}

static void layout_result_free(void *void_layout)
{
    struct layout_info *layout = void_layout;

    if (!layout)
        return;

    layout_info_free_extents(void_layout);

    /* Undo dss_layout_desc_decode */
    free(layout->layout_desc.mod_name);
    pho_attrs_free(&layout->layout_desc.mod_attrs);
}

const struct dss_resource_ops layout_ops = {
    .insert_query = layout_insert_query,
    .update_query = NULL,
    .select_query = layout_select_query,
    .delete_query = layout_delete_query,
    .create       = layout_from_pg_row,
    .free         = layout_result_free,
    .size         = sizeof(struct layout_info),
};
