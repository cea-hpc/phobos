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
 * \brief  Full layout resource file of Phobos's Distributed State Service.
 */

#include <jansson.h>
#include <libpq-fe.h>

#include "pho_type_utils.h"

#include "dss_utils.h"
#include "extent.h"
#include "full_layout.h"
#include "layout.h"

static int full_layout_select_query(GString **conditions, int n_conditions,
                                    GString *request, struct dss_sort *sort)
{
    g_string_append(request,
                    "SELECT oid, object_uuid, version, lyt_info, copy_name,"
                    " json_agg(json_build_object("
                    "  'extent_uuid', extent_uuid, 'sz', size,"
                    "  'offsetof', offsetof, 'fam', medium_family,"
                    "  'state', state, 'media', medium_id,"
                    "  'library', medium_library, 'addr', address,"
                    "  'hash', hash, 'info', info, 'lyt_index', layout_index,"
                    "  'creation_time', creation_time"
                    " ) ORDER BY layout_index)"
                    " FROM extent"
                    " RIGHT JOIN ("
                    "   SELECT oid, object_uuid, version, layout.copy_name,"
                    "          lyt_info, extent_uuid, layout_index"
                    "    FROM layout"
                    "    LEFT JOIN ("
                    "     SELECT oid, object_uuid, version, lyt_info, copy_name"
                    "     FROM copy LEFT JOIN ("
                    "       SELECT oid, object_uuid, version FROM object"
                    "       UNION SELECT oid, object_uuid, version FROM deprecated_object"
                    "     ) AS tmpO USING (object_uuid, version)"
                    "    ) AS inner_table"
                    "    USING (object_uuid, version, copy_name)");

    if (n_conditions >= 1)
        g_string_append(request, conditions[0]->str);

    g_string_append(request,
                    " ) AS outer_table USING (extent_uuid)");

    if (n_conditions >= 2)
        g_string_append(request, conditions[1]->str);

    g_string_append(request,
                    " GROUP BY oid, object_uuid, version, lyt_info, copy_name");
    if (sort)
        dss_sort2sql(request, sort);
    else
        g_string_append(request, " ORDER BY oid, version, object_uuid");

    return 0;
}

/**
 * Extract extents from json
 *
 * \param[out] extents extent list
 * \param[out] count  number of extents decoded
 * \param[in]  json   String with json media stats
 *
 * \return 0 on success, negative error code on failure.
 */
static int layout_extents_decode(struct extent **extents, int *count,
                                 const char *json)
{
    struct extent *result = NULL;
    size_t extents_res_size;
    json_error_t json_error;
    json_t *child;
    json_t *root;
    int rc;
    int i;

    ENTRY;

    pho_debug("Decoding JSON representation for extents: '%s'", json);

    root = json_loads(json, JSON_REJECT_DUPLICATES, &json_error);
    if (!root)
        LOG_RETURN(-EINVAL, "Failed to parse json data: %s", json_error.text);

    if (!json_is_array(root))
        LOG_GOTO(out_decref, rc = -EINVAL, "Invalid extents description");

    *count = json_array_size(root);
    if (*count == 0) {
        extents = NULL;
        LOG_GOTO(out_decref, rc = -EINVAL,
                 "json parser: extents array is empty");
    }

    extents_res_size = sizeof(struct extent) * (*count);
    result = xcalloc(1, extents_res_size);

    for (i = 0; i < *count; i++) {
        const char *tmp;
        const char *tmp2;

        child = json_array_get(root, i);

        result[i].uuid = json_dict2str(child, "extent_uuid");
        if (!result[i].uuid)
            LOG_GOTO(out_decref, rc = -EINVAL,
                     "Missing attribute 'extent_uuid'");

        result[i].layout_idx = json_dict2ll(child, "lyt_index");
        if (result[i].layout_idx < 0)
            LOG_GOTO(out_decref, rc = -EINVAL, "Missing attribute 'lyt_index'");

        tmp = json_dict2tmp_str(child, "state");
        if (!tmp)
            LOG_GOTO(out_decref, rc = -EINVAL, "Missing attribute 'state'");

        result[i].state = str2extent_state(tmp);
        result[i].size = json_dict2ll(child, "sz");
        if (result[i].size < 0)
            LOG_GOTO(out_decref, rc = -EINVAL, "Missing attribute 'sz'");

        result[i].address.buff = json_dict2str(child, "addr");
        if (!result[i].address.buff)
            LOG_GOTO(out_decref, rc = -EINVAL, "Missing attribute 'addr'");

        result[i].address.size = strlen(result[i].address.buff) + 1;

        tmp = json_dict2tmp_str(child, "fam");
        if (!tmp)
            LOG_GOTO(out_decref, rc = -EINVAL, "Missing attribute 'fam'");

        result[i].media.family = str2rsc_family(tmp);

        result[i].offset = json_dict2ll(child, "offsetof");
        if (result[i].offset == INT64_MIN)
            LOG_GOTO(out_decref, rc = -EINVAL, "Missing attribute 'offsetof'");

        /*XXX fs_type & address_type retrieved from media info */
        if (result[i].media.family == PHO_RSC_INVAL)
            LOG_GOTO(out_decref, rc = -EINVAL, "Invalid medium family");

        tmp = json_dict2tmp_str(child, "media");
        if (!tmp)
            LOG_GOTO(out_decref, rc = -EINVAL, "Missing attribute 'media'");

        tmp2 = json_dict2tmp_str(child, "library");
        if (!tmp2)
            LOG_GOTO(out_decref, rc = -EINVAL, "Missing attribute 'library'");

        pho_id_name_set(&result[i].media, tmp, tmp2);

        rc = dss_extent_hash_decode(&result[i], json_object_get(child, "hash"));
        if (rc)
            LOG_GOTO(out_decref, rc = -EINVAL, "Failed to set hash");

        pho_json_raw_to_attrs(&result[i].info,
                              json_object_get(child, "info"));

        tmp = json_dict2tmp_str(child, "creation_time");
        if (!tmp)
            LOG_GOTO(out_decref, rc = -EINVAL,
                     "Missing attribute 'creation_time'");

        rc = json_agg_str2timeval(tmp, &result[i].creation_time);
        if (rc)
            LOG_GOTO(out_decref, rc,
                     "Error when getting timeval from json attribute "
                     "'creation_time' '%s'", tmp);
    }

    *extents = result;
    rc = 0;

out_decref:
    if (rc)
        free(result);
    json_decref(root);
    return rc;
}

static int full_layout_from_pg_row(struct dss_handle *handle, void *void_layout,
                                   PGresult *res, int row_num)
{
    struct layout_info *layout = void_layout;
    int rc;

    (void) handle;

    layout->oid = PQgetvalue(res, row_num, 0);
    layout->uuid = PQgetvalue(res, row_num, 1);
    layout->version = atoi(PQgetvalue(res, row_num, 2));
    rc = layout_desc_decode(&layout->layout_desc, PQgetvalue(res, row_num, 3));
    if (rc)
        LOG_RETURN(rc, "dss_layout_desc decode error");
    layout->copy_name = PQgetvalue(res, row_num, 4);
    rc = layout_extents_decode(&layout->extents, &layout->ext_count,
                               PQgetvalue(res, row_num, 5));
    if (rc)
        LOG_RETURN(rc, "dss_extent decode error");

    return 0;
}

static void full_layout_result_free(void *void_layout)
{
    struct layout_info *layout = void_layout;

    if (!layout)
        return;

    /* Undo dss_layout_desc_decode */
    free(layout->layout_desc.mod_name);
    pho_attrs_free(&layout->layout_desc.mod_attrs);

    /* Undo dss_layout_extents_decode */
    layout_info_free_extents(layout);
}

const struct dss_resource_ops full_layout_ops = {
    .insert_query = NULL,
    .update_query = NULL,
    .select_query = full_layout_select_query,
    .delete_query = NULL,
    .create       = full_layout_from_pg_row,
    .free         = full_layout_result_free,
    .size         = sizeof(struct layout_info),
};
