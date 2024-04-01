/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2022 CEA/DAM.
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
 * \brief  Phobos Distributed State Service API.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "dss_utils.h"
#include "pho_common.h"
#include "pho_type_utils.h"
#include "pho_dss.h"
#include "pho_cfg.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <inttypes.h>
#include <string.h>
#include <glib.h>
#include <libpq-fe.h>
#include <jansson.h>
#include <stdbool.h>
#include <string.h>
#include <strings.h>
#include <assert.h>
#include <gmodule.h>
#include <stdint.h>

#include "device.h"
#include "dss_config.h"
#include "extent.h"
#include "filters.h"
#include "logs.h"
#include "media.h"
#include "object.h"
#include "resources.h"

static int dss_full_layout_from_pg_row(struct dss_handle *handle,
                                       void *void_layout, PGresult *res,
                                       int row_num);
static void dss_full_layout_result_free(void *void_layout);

struct dss_result {
    PGresult *pg_res;
    enum dss_type item_type;
    union {
        char   raw[0];
        struct media_info   media[0];
        struct dev_info     dev[0];
        struct object_info  object[0];
        struct layout_info  layout[0];
    } items;
};

#define res_of_item_list(_list) \
    container_of((_list), struct dss_result, items)

/**
 * Handle notices from PostgreSQL. Strip the trailing newline and re-emit them
 * through phobos log API.
 */
static void dss_pg_logger(void *arg, const char *message)
{
    size_t mlen = strlen(message);

    if (message[mlen - 1] == '\n')
        mlen -= 1;

    pho_info("%*s", (int)mlen, message);
}

int dss_init(struct dss_handle *handle)
{
    const char *conn_str;
    int rc;

    /* init static config parsing */
    rc = parse_supported_tape_models();
    if (rc && rc != -EALREADY)
        return rc;

    conn_str = get_connection_string();
    if (conn_str == NULL)
        return -EINVAL;

    handle->dh_conn = PQconnectdb(conn_str);

    if (PQstatus(handle->dh_conn) != CONNECTION_OK) {
        rc = -ENOTCONN;
        pho_error(rc, "Connection to database failed: %s",
                  PQerrorMessage(handle->dh_conn));
        PQfinish(handle->dh_conn);
        handle->dh_conn = NULL;
        return rc;
    }

    (void)PQsetNoticeProcessor(handle->dh_conn, dss_pg_logger, NULL);

    return 0;
}

void dss_fini(struct dss_handle *handle)
{
    PQfinish(handle->dh_conn);
}

/**
 * helper arrays to build SQL query
 */
static const char * const select_query[] = {
    [DSS_FULL_LAYOUT] =
                   "SELECT oid, object_uuid, version, lyt_info,"
                   " json_agg(json_build_object("
                   "  'extent_uuid', extent_uuid, 'sz', size,"
                   "  'offsetof', offsetof, 'fam', medium_family,"
                   "  'state', state, 'media', medium_id, 'addr', address,"
                   "  'hash', hash, 'info', info, 'lyt_index', layout_index))"
                   " FROM extent"
                   " RIGHT JOIN ("
                   "  SELECT oid, object_uuid, version, lyt_info, extent_uuid,"
                   "         layout_index"
                   "   FROM layout"
                   "   LEFT JOIN ("
                   "    SELECT oid, object_uuid, version, lyt_info FROM object"
                   "    UNION SELECT oid, object_uuid, version, lyt_info"
                   "     FROM deprecated_object"
                   "   ) AS inner_table USING (object_uuid, version)",
    /* The layout query ends with select_inner_end_query and
     * select_outer_end_query.
     */
};

static const char * const select_inner_end_query[] = {
    [DSS_FULL_LAYOUT] = " ORDER BY layout_index)"
                        " AS outer_table USING (extent_uuid)",
};

static const char * const select_outer_end_query[] = {
    [DSS_FULL_LAYOUT] = " GROUP BY oid, object_uuid, version, lyt_info"
                        " ORDER BY oid, version, object_uuid",
};

static const size_t res_size[] = {
    [DSS_FULL_LAYOUT] = sizeof(struct layout_info),
};

typedef int (*res_pg_constructor_t)(struct dss_handle *handle, void *item,
                                    PGresult *res, int row_num);
static const res_pg_constructor_t res_pg_constructor[] = {
    [DSS_FULL_LAYOUT] = dss_full_layout_from_pg_row,
};

typedef void (*res_destructor_t)(void *item);
static const res_destructor_t res_destructor[] = {
    [DSS_FULL_LAYOUT] = dss_full_layout_result_free,
};

enum dss_move_queries {
    DSS_MOVE_INVAL = -1,
    DSS_MOVE_OBJECT_TO_DEPREC = 0,
    DSS_MOVE_DEPREC_TO_OBJECT = 1,
};

static const char * const move_query[] = {
    [DSS_MOVE_OBJECT_TO_DEPREC] = "WITH moved_object AS"
                                  " (DELETE FROM object WHERE %s RETURNING"
                                  "  oid, object_uuid, version, user_md,"
                                  "  lyt_info, obj_status)"
                                  " INSERT INTO deprecated_object"
                                  "  (oid, object_uuid, version, user_md,"
                                  "   lyt_info, obj_status)"
                                  " SELECT * FROM moved_object",
    [DSS_MOVE_DEPREC_TO_OBJECT] = "WITH risen_object AS"
                                  " (DELETE FROM deprecated_object WHERE %s"
                                  "  RETURNING oid, object_uuid,"
                                  "  version, user_md, lyt_info, obj_status)"
                                  " INSERT INTO object (oid, object_uuid, "
                                  "  version, user_md, lyt_info, obj_status)"
                                  " SELECT * FROM risen_object",
};

/**
 * Extract layout type and parameters from json
 *
 */
static int dss_layout_desc_decode(struct module_desc *desc, const char *json)
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

/**
 * Extract extents from json
 *
 * \param[out] extents extent list
 * \param[out] count  number of extents decoded
 * \param[in]  json   String with json media stats
 *
 * \return 0 on success, negative error code on failure.
 */
static int dss_layout_extents_decode(struct extent **extents, int *count,
                                     const char *json)
{
    json_t          *root;
    json_t          *child;
    json_error_t     json_error;
    struct extent   *result = NULL;
    size_t           extents_res_size;
    int              rc;
    int              i;
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

        /* offsetof */
        result[i].offset = json_dict2ll(child, "offsetof");
        if (result[i].offset == INT64_MIN)
            LOG_GOTO(out_decref, rc = -EINVAL, "Missing attribute 'offsetof'");

        /*XXX fs_type & address_type retrieved from media info */
        if (result[i].media.family == PHO_RSC_INVAL)
            LOG_GOTO(out_decref, rc = -EINVAL, "Invalid medium family");

        tmp = json_dict2tmp_str(child, "media");
        if (!tmp)
            LOG_GOTO(out_decref, rc = -EINVAL, "Missing attribute 'media'");

        pho_id_name_set(&result[i].media, tmp);

        rc = dss_extent_hash_decode(&result[i], json_object_get(child, "hash"));
        if (rc)
            LOG_GOTO(out_decref, rc = -EINVAL, "Failed to set hash");
    }

    *extents = result;
    rc = 0;

out_decref:
    if (rc)
        free(result);
    json_decref(root);
    return rc;
}

static inline bool is_type_supported(enum dss_type type)
{
    switch (type) {
    case DSS_OBJECT:
    case DSS_DEPREC:
    case DSS_LAYOUT:
    case DSS_FULL_LAYOUT:
    case DSS_EXTENT:
    case DSS_DEVICE:
    case DSS_MEDIA:
    case DSS_LOGS:
        return true;

    default:
        return false;
    }
}

/**
 * Fill a layout_info and its extents from the information in the `row_num`th
 * row of `res`.
 */
static int dss_full_layout_from_pg_row(struct dss_handle *handle,
                                       void *void_layout, PGresult *res,
                                       int row_num)
{
    struct layout_info *layout = void_layout;
    int rc;

    (void)handle;

    layout->oid = PQgetvalue(res, row_num, 0);
    layout->uuid = PQgetvalue(res, row_num, 1);
    layout->version = atoi(PQgetvalue(res, row_num, 2));
    rc = dss_layout_desc_decode(&layout->layout_desc,
                                PQgetvalue(res, row_num, 3));
    if (rc) {
        pho_error(rc, "dss_layout_desc decode error");
        return rc;
    }

    rc = dss_layout_extents_decode(&layout->extents,
                                   &layout->ext_count,
                                   PQgetvalue(res, row_num, 4));
    if (rc) {
        pho_error(rc, "dss_extent tags decode error");
        return rc;
    }

    return 0;
}

/**
 * Free the resources associated with layout_info built from a PGresult.
 */
static void dss_full_layout_result_free(void *void_layout)
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

static void _dss_result_free(struct dss_result *dss_res, int item_cnt)
{
    res_destructor_t dtor;
    size_t item_size;
    int i;

    item_size = get_resource_size(dss_res->item_type);
    if (item_size == -ENOTSUP)
        item_size = res_size[dss_res->item_type];

    dtor = get_free_function(dss_res->item_type);
    if (dtor == NULL)
        dtor = res_destructor[dss_res->item_type];

    for (i = 0; i < item_cnt; i++)
        dtor(dss_res->items.raw + i * item_size);

    PQclear(dss_res->pg_res);
    free(dss_res);

}

static int dss_generic_get(struct dss_handle *handle, enum dss_type type,
                           const struct dss_filter **filters,
                           void **item_list, int *item_cnt)
{
    PGconn *conn = handle->dh_conn;
    struct dss_result *dss_res;
    GString *conditions = NULL;
    GString *clause = NULL;
    size_t dss_res_size;
    size_t item_size;
    PGresult *res;
    int rc = 0;
    int i = 0;

    ENTRY;

    if (conn == NULL || item_list == NULL || item_cnt == NULL)
        LOG_RETURN(-EINVAL, "dss - conn: %p, item_list: %p, item_cnt: %p",
                   conn, item_list, item_cnt);

    *item_list = NULL;
    *item_cnt  = 0;

    if (!is_type_supported(type))
        LOG_RETURN(-ENOTSUP, "Unsupported DSS request type %#x", type);

    conditions = g_string_new(NULL);
    rc = clause_filter_convert(handle, conditions, filters[0]);
    if (rc) {
        g_string_free(conditions, true);
        return rc;
    }

    clause = g_string_new(NULL);
    rc = get_select_query(type, conditions, clause);
    if (rc == -ENOTSUP) {
        g_string_append(clause, select_query[type]);
        g_string_append(clause, conditions->str);
        rc = 0;
    }

    g_string_free(conditions, true);
    if (rc) {
        g_string_free(clause, true);
        return rc;
    }

    if (type == DSS_FULL_LAYOUT) {
        g_string_append(clause, select_inner_end_query[DSS_FULL_LAYOUT]);
        rc = clause_filter_convert(handle, clause, filters[1]);
        if (rc) {
            g_string_free(clause, true);
            return rc;
        }
        g_string_append(clause, select_outer_end_query[type]);
    }

    pho_debug("Executing request: '%s'", clause->str);

    res = PQexec(conn, clause->str);
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        rc = psql_state2errno(res);
        pho_error(rc, "Query '%s' failed: %s", clause->str,
                  PQresultErrorField(res, PG_DIAG_MESSAGE_PRIMARY));
        PQclear(res);
        g_string_free(clause, true);
        return rc;
    }

    g_string_free(clause, true);

    item_size = get_resource_size(type);
    if (item_size == -ENOTSUP)
        item_size = res_size[type];

    dss_res_size = sizeof(struct dss_result) + PQntuples(res) * item_size;
    dss_res = xcalloc(1, dss_res_size);

    dss_res->item_type = type;
    dss_res->pg_res = res;

    for (i = 0; i < PQntuples(res); i++) {
        void *item_ptr = (char *)&dss_res->items.raw + i * item_size;

        rc = create_resource(type, handle, item_ptr, res, i);
        if (rc == -ENOTSUP)
            rc = res_pg_constructor[type](handle, item_ptr, res, i);

        if (rc)
            goto out;
    }

    *item_list = &dss_res->items.raw;
    *item_cnt = PQntuples(res);

out:
    if (rc)
        /* Only free elements that were initialized, this also frees res */
        _dss_result_free(dss_res, i);

    return rc;
}

/**
 * fields is only used by DSS_SET_UPDATE on DSS_MEDIA
 */
static int dss_generic_set(struct dss_handle *handle, enum dss_type type,
                           void *item_list, int item_cnt,
                           enum dss_set_action action, uint64_t fields)
{
    PGconn      *conn = handle->dh_conn;
    GString     *request;
    int          error = 0;
    int          rc = 0;
    ENTRY;

    if (conn == NULL ||
        (type != DSS_LOGS && (item_list == NULL || item_cnt == 0)))
        LOG_RETURN(-EINVAL, "conn: %p, item_list: %p, item_cnt: %d",
                   conn, item_list, item_cnt);

    if (action == DSS_SET_FULL_INSERT &&
        !(type == DSS_OBJECT || type == DSS_LAYOUT))
        LOG_RETURN(-ENOTSUP, "Full insert request is not supported for %s",
                   dss_type_names[type]);

    if (action == DSS_SET_UPDATE_OBJ_STATUS && type != DSS_OBJECT)
        LOG_RETURN(-ENOTSUP,
                   "Specific obj_status update is not supported for %s",
                   dss_type_names[type]);

    request = g_string_new("BEGIN;");

    switch (action) {
    case DSS_SET_INSERT:
        rc = get_insert_query(type, conn, item_list, item_cnt, INSERT_OBJECT,
                              request);
        break;
    case DSS_SET_FULL_INSERT:
        rc = get_insert_query(type, conn, item_list, item_cnt,
                              INSERT_FULL_OBJECT, request);
        break;
    case DSS_SET_UPDATE:
        rc = get_update_query(type, conn, item_list, item_cnt, fields, request);
        break;
    case DSS_SET_DELETE:
        rc = get_delete_query(type, item_list, item_cnt, request);
        break;
    default:
        rc = -ENOTSUP;
    }

    // XXX: This will be removed when every resource type has its own file
    if (rc != -ENOTSUP) {
        if (rc)
            LOG_GOTO(out_cleanup, rc, "SQL request failed");

        goto exec_request;
    } else {
        rc = 0;
    }

    switch (type) {
    // XXX: will be removed in the cleanup patch
    default:
        LOG_GOTO(out_cleanup, rc = -ENOTSUP,
                 "unsupported DSS request type %#x", type);

    }

    if (error)
        LOG_GOTO(out_cleanup, rc = -EINVAL,
                 "JSON parsing failed: %d errors found", error);

exec_request:
    rc = execute_and_commit_or_rollback(conn, request, NULL, PGRES_COMMAND_OK);

out_cleanup:
    g_string_free(request, true);
    return rc;
}

static enum dss_move_queries move_query_type(enum dss_type type_from,
                                             enum dss_type type_to)
{
    if (type_from == DSS_OBJECT && type_to == DSS_DEPREC)
        return DSS_MOVE_OBJECT_TO_DEPREC;

    if (type_from == DSS_DEPREC && type_to == DSS_OBJECT)
        return DSS_MOVE_DEPREC_TO_OBJECT;

    return DSS_MOVE_INVAL;
}

static int dss_prepare_oid_list(PGconn *conn, GString *list,
                                struct object_info *obj_list, int obj_cnt)
{
    int i;

    for (i = 0; i < obj_cnt; ++i) {
        char *tmp = PQescapeLiteral(conn, obj_list[i].oid,
                                    strlen(obj_list[i].oid));

        if (!tmp)
            LOG_RETURN(-EINVAL,
                       "Cannot escape litteral %s: %s",
                       obj_list[i].oid, PQerrorMessage(conn));

        g_string_append_printf(list, "oid=%s", tmp);
        PQfreemem(tmp);
        if (i + 1 != obj_cnt)
            g_string_append(list, " OR ");
    }

    return 0;
}

/**
 * In obj_list, each obj must have an uuid and a version.
 */
static int dss_prepare_uuid_version_list(PGconn *conn, GString *list,
                                         struct object_info *obj_list,
                                         int obj_cnt)
{
    int i;

    for (i = 0; i < obj_cnt; ++i) {
        /* uuid */
        char *sql_uuid = PQescapeLiteral(conn, obj_list[i].uuid,
                                         strlen(obj_list[i].uuid));

        if (!sql_uuid)
            LOG_RETURN(-EINVAL,
                       "Cannot escape litteral %s: %s",
                       obj_list[i].uuid, PQerrorMessage(conn));

        g_string_append_printf(list, "object_uuid=%s AND ", sql_uuid);
        PQfreemem(sql_uuid);

        /* version */
        g_string_append_printf(list, "version='%d'", obj_list[i].version);

        if (i + 1 != obj_cnt)
            g_string_append(list, " OR ");
    }

    return 0;
}

int dss_object_move(struct dss_handle *handle, enum dss_type type_from,
                    enum dss_type type_to, struct object_info *obj_list,
                    int obj_cnt)
{
    PGconn      *conn = handle->dh_conn;
    enum dss_move_queries move_type;
    GString     *clause;
    GString     *key_list;
    PGresult    *res = NULL;
    int          rc = 0;

    ENTRY;

    move_type = move_query_type(type_from, type_to);
    if (!conn || move_type == DSS_MOVE_INVAL)
        LOG_RETURN(-EINVAL, "dss - conn: %p, move_type: %d", conn, move_type);

    clause = g_string_new(NULL);
    key_list = g_string_new(NULL);

    switch (move_type) {
    case DSS_MOVE_OBJECT_TO_DEPREC:
        rc = dss_prepare_oid_list(conn, key_list, obj_list, obj_cnt);
        if (rc)
            LOG_GOTO(err, rc, "OID list could not be built");

        break;
    case DSS_MOVE_DEPREC_TO_OBJECT:
        rc = dss_prepare_uuid_version_list(conn, key_list, obj_list, obj_cnt);
        if (rc)
            LOG_GOTO(err, rc, "Version list could not be built from deprec");

        break;
    default:
            LOG_GOTO(err, -EINVAL, "Unsupported move type");
    }

    g_string_append_printf(clause, move_query[move_type], key_list->str);

    pho_debug("Executing request: '%s'", clause->str);

    res = PQexec(conn, clause->str);
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        rc = psql_state2errno(res);
        pho_error(rc, "Query '%s' failed: %s", clause->str,
                  PQresultErrorField(res, PG_DIAG_MESSAGE_PRIMARY));
    }

    PQclear(res);

err:
    g_string_free(key_list, true);
    g_string_free(clause, true);
    return rc;
}

void dss_res_free(void *item_list, int item_cnt)
{
    struct dss_result *dss_res;

    if (!item_list)
        return;

    dss_res = res_of_item_list(item_list);
    _dss_result_free(dss_res, item_cnt);
}

int dss_device_get(struct dss_handle *hdl, const struct dss_filter *filter,
                   struct dev_info **dev_ls, int *dev_cnt)
{
    return dss_generic_get(hdl, DSS_DEVICE,
                           (const struct dss_filter*[]) {filter, NULL},
                           (void **)dev_ls, dev_cnt);
}

int dss_media_get(struct dss_handle *hdl, const struct dss_filter *filter,
                  struct media_info **med_ls, int *med_cnt)
{
    return dss_generic_get(hdl, DSS_MEDIA,
                           (const struct dss_filter*[]) {filter, NULL},
                           (void **)med_ls, med_cnt);
}

int dss_layout_get(struct dss_handle *hdl, const struct dss_filter *filter,
                   struct layout_info **layouts, int *layout_count)
{
    return dss_generic_get(hdl, DSS_LAYOUT,
                           (const struct dss_filter*[]) {filter, NULL},
                           (void **)layouts, layout_count);
}

int dss_full_layout_get(struct dss_handle *hdl, const struct dss_filter *object,
                        const struct dss_filter *media,
                        struct layout_info **layouts, int *layout_count)
{
    return dss_generic_get(hdl, DSS_FULL_LAYOUT,
                           (const struct dss_filter*[]) {object, media},
                           (void **)layouts, layout_count);
}

int dss_extent_get(struct dss_handle *hdl, const struct dss_filter *filter,
                   struct extent **extents, int *extent_count)
{
    return dss_generic_get(hdl, DSS_EXTENT,
                           (const struct dss_filter*[]) {filter, NULL},
                           (void **)extents, extent_count);
}

int dss_object_get(struct dss_handle *hdl, const struct dss_filter *filter,
                   struct object_info **obj_ls, int *obj_cnt)
{
    return dss_generic_get(hdl, DSS_OBJECT,
                           (const struct dss_filter*[]) {filter, NULL},
                           (void **)obj_ls, obj_cnt);
}

int dss_deprecated_object_get(struct dss_handle *hdl,
                              const struct dss_filter *filter,
                              struct object_info **obj_ls, int *obj_cnt)
{
    return dss_generic_get(hdl, DSS_DEPREC,
                           (const struct dss_filter*[]) {filter, NULL},
                           (void **)obj_ls, obj_cnt);
}

int dss_logs_get(struct dss_handle *hdl, const struct dss_filter *filter,
                 struct pho_log **logs_ls, int *logs_cnt)
{
    return dss_generic_get(hdl, DSS_LOGS,
                           (const struct dss_filter*[]) {filter, NULL},
                           (void **)logs_ls, logs_cnt);
}

int dss_device_insert(struct dss_handle *hdl, struct dev_info *dev_ls,
                      int dev_cnt)
{
    return dss_generic_set(hdl, DSS_DEVICE, (void *)dev_ls, dev_cnt,
                           DSS_SET_INSERT, 0);
}

int dss_device_delete(struct dss_handle *hdl, struct dev_info *dev_ls,
                      int dev_cnt)
{
    return dss_generic_set(hdl, DSS_DEVICE, (void *)dev_ls, dev_cnt,
                           DSS_SET_DELETE, 0);
}

int dss_device_update(struct dss_handle *hdl, struct dev_info *dev_ls,
                      int dev_cnt, int64_t fields)
{
    return dss_generic_set(hdl, DSS_DEVICE, (void *)dev_ls, dev_cnt,
                           DSS_SET_UPDATE, fields);
}

static int media_update_lock_retry(struct dss_handle *hdl,
                                   struct media_info *med_ls, int med_cnt)
{
    unsigned int nb_try = 0;
    int rc = -1;

    while (rc && nb_try < MAX_UPDATE_LOCK_TRY) {
        rc = dss_lock(hdl, DSS_MEDIA_UPDATE_LOCK, med_ls, med_cnt);
        if (rc != -EEXIST)
            break;

        pho_warn("DSS_MEDIA_UPDATE_LOCK is already locked: waiting %u ms",
                 UPDATE_LOCK_SLEEP_MICRO_SECONDS);
        nb_try++;
        usleep(UPDATE_LOCK_SLEEP_MICRO_SECONDS);
    }

    return rc;
}

/**
 * output medium_info must be cleaned by calling dss_res_free(medium_info, 1)
 */
int dss_one_medium_get_from_id(struct dss_handle *dss,
                               const struct pho_id *medium_id,
                               struct media_info **medium_info)
{
    struct dss_filter filter;
    int cnt;
    int rc;

    /* get medium info from medium id */
    rc = dss_filter_build(&filter,
                          "{\"$AND\": ["
                              "{\"DSS::MDA::family\": \"%s\"}, "
                              "{\"DSS::MDA::id\": \"%s\"}"
                          "]}",
                          rsc_family2str(medium_id->family),
                          medium_id->name);
    if (rc)
        LOG_RETURN(rc,
                   "Unable to build filter for media family %s and name %s",
                   rsc_family2str(medium_id->family), medium_id->name);

    rc = dss_media_get(dss, &filter, medium_info, &cnt);
    dss_filter_free(&filter);
    if (rc)
        LOG_RETURN(rc,
                   "Error while getting medium info for family %s and name %s",
                   rsc_family2str(medium_id->family), medium_id->name);

    /* (family, id) is the primary key of the media table */
    assert(cnt <= 1);

    if (cnt == 0) {
        pho_warn("Medium (family %s, name %s) is absent from media table",
                 rsc_family2str(medium_id->family), medium_id->name);
        dss_res_free(*medium_info, cnt);
        return(-ENOENT);
    }

    return 0;
}

int dss_media_set(struct dss_handle *hdl, struct media_info *med_ls,
                  int med_cnt, enum dss_set_action action, uint64_t fields)
{
    int rc;

    if (action == DSS_SET_UPDATE && !fields) {
        pho_warn("Tried updating media without specifying any field");
        return 0;
    }

    /**
     * The only set action that needs a lock is the stats update action. Indeed,
     * we need to get the full existing stat SQL column to fill only new fields
     * and keep old value.
     *
     * All other set actions (insert, delete, or update of other fields) are
     * atomic SQL requests and don't need any lock and prefetch.
     */
    if (action == DSS_SET_UPDATE && IS_STAT(fields)) {
        int i;

        rc = media_update_lock_retry(hdl, med_ls, med_cnt);
        if (rc)
            LOG_RETURN(rc, "Error when locking media to %s",
                       dss_set_actions_names[action]);

        for (i = 0; i < med_cnt; i++) {
            struct media_info *medium_info;

            rc = dss_one_medium_get_from_id(hdl, &med_ls[i].rsc.id,
                                            &medium_info);
            if (rc)
                LOG_GOTO(clean, rc,
                         "Error on getting medium_info (family %s, name %s) to "
                         "update stats",
                         rsc_family2str(med_ls[i].rsc.id.family),
                         med_ls[i].rsc.id.name);

            if (NB_OBJ & fields)
                medium_info->stats.nb_obj = med_ls[i].stats.nb_obj;

            if (NB_OBJ_ADD & fields)
                medium_info->stats.nb_obj += med_ls[i].stats.nb_obj;

            if (LOGC_SPC_USED & fields)
                medium_info->stats.logc_spc_used =
                    med_ls[i].stats.logc_spc_used;

            if (LOGC_SPC_USED_ADD & fields)
                medium_info->stats.logc_spc_used +=
                    med_ls[i].stats.logc_spc_used;

            if (PHYS_SPC_USED & fields)
                medium_info->stats.phys_spc_used =
                    med_ls[i].stats.phys_spc_used;

            if (PHYS_SPC_FREE & fields)
                medium_info->stats.phys_spc_free =
                    med_ls[i].stats.phys_spc_free;

            med_ls[i].stats = medium_info->stats;
            dss_res_free(medium_info, 1);
        }
    }

    rc = dss_generic_set(hdl, DSS_MEDIA, (void *)med_ls, med_cnt, action,
                         fields);

clean:
    if (action == DSS_SET_UPDATE && IS_STAT(fields)) {
        int rc2;

        rc2 = dss_unlock(hdl, DSS_MEDIA_UPDATE_LOCK, med_ls, med_cnt, false);
        if (rc2) {
            pho_error(rc2, "Error when unlocking media at end of %s",
                      dss_set_actions_names[action]);
            rc = rc ? : rc2;
        }
    }

    return rc;
}

int dss_extent_set(struct dss_handle *hdl, struct extent *extents,
                   int extent_count, enum dss_set_action action)
{
    return dss_generic_set(hdl, DSS_EXTENT, (void *)extents, extent_count,
                           action, 0);
}

int dss_layout_set(struct dss_handle *hdl, struct layout_info *layouts,
                   int layout_count, enum dss_set_action action)
{
    return dss_generic_set(hdl, DSS_LAYOUT, (void *)layouts, layout_count,
                           action, 0);
}

int dss_object_set(struct dss_handle *hdl, struct object_info *obj_ls,
                   int obj_cnt, enum dss_set_action action)
{
    return dss_generic_set(hdl, DSS_OBJECT, (void *)obj_ls, obj_cnt, action, 0);
}

int dss_object_update(struct dss_handle *hdl, struct object_info *obj_ls,
                      int obj_cnt, int64_t fields)
{
    return dss_generic_set(hdl, DSS_OBJECT, (void *)obj_ls, obj_cnt,
                           DSS_SET_UPDATE, fields);
}

int dss_deprecated_object_set(struct dss_handle *hdl,
                              struct object_info *obj_ls, int obj_cnt,
                              enum dss_set_action action)
{
    return dss_generic_set(hdl, DSS_DEPREC, (void *)obj_ls, obj_cnt, action, 0);
}

static void pho_error_oid_uuid_version(int error_code, const char *msg,
                                       const char *oid, const char *uuid,
                                       int version)
{
    if (oid && uuid) {
        if (version) {
            pho_error(error_code, "%s, oid %s, uuid %s, version %d",
                      msg, oid, uuid, version);
        } else {
            pho_error(error_code, "%s, oid %s, uuid %s", msg, oid, uuid);
        }
    } else {
        if (version) {
            pho_error(error_code, "%s, %s %s, version %d",
                      msg, oid ? "oid" : "uuid", oid ? : uuid, version);
        } else {
            pho_error(error_code, "%s, %s %s",
                      msg, oid ? "oid" : "uuid", oid ? : uuid);
        }
    }
}

static int build_json_filter(char **filter, const char *oid,
                             const char *uuid, int version)
{
    int rc = 0;

    if (uuid && oid) {
        if (version) {
            rc = asprintf(filter,
                          "{\"$AND\": ["
                              "{\"DSS::OBJ::oid\": \"%s\"},"
                              "{\"DSS::OBJ::uuid\": \"%s\"},"
                              "{\"DSS::OBJ::version\": \"%d\"}"
                          "]}",
                          oid, uuid, version);
        } else {
            rc = asprintf(filter,
                          "{\"$AND\": ["
                              "{\"DSS::OBJ::oid\": \"%s\"},"
                              "{\"DSS::OBJ::uuid\": \"%s\"}"
                          "]}",
                          oid, uuid);
        }
    } else {
        if (version && !oid) {
            rc = asprintf(filter,
                          "{\"$AND\": ["
                              "{\"DSS::OBJ::uuid\": \"%s\"},"
                              "{\"DSS::OBJ::version\": \"%d\"}"
                          "]}",
                          uuid, version);
        } else {
            rc = asprintf(filter, "{\"DSS::OBJ::%s\": \"%s\"}",
                          oid ? "oid" : "uuid", oid ? : uuid);
        }
    }
    return rc;
}

static int lazy_find_deprecated_object(struct dss_handle *hdl,
                                       const char *oid, const char *uuid,
                                       int version, struct object_info **obj)
{
    struct object_info *obj_iter;
    struct object_info *obj_list;
    struct dss_filter filter;
    struct object_info *curr;
    char *json_filter;
    int obj_cnt;
    int rc;

    ENTRY;

    rc = build_json_filter(&json_filter, oid, uuid, version);
    if (rc < 0)
        LOG_RETURN(rc = -ENOMEM, "Cannot allocate filter string");

    rc = dss_filter_build(&filter, "%s", json_filter);
    free(json_filter);
    if (rc)
        LOG_RETURN(rc, "Cannot build filter");

    rc = dss_deprecated_object_get(hdl, &filter, &obj_list, &obj_cnt);
    dss_filter_free(&filter);
    if (rc) {
        pho_error_oid_uuid_version(rc, "Unable to get deprecated object",
                                   oid, uuid, version);
        return rc;
    }

    if (obj_cnt == 0)
        LOG_GOTO(out_free, rc = -ENOENT, "No object found");

    curr = obj_list;
    if (obj_cnt > 1) {
        /* search the most recent object or the one matching
         * version if != 0.
         */
        for (obj_iter = obj_list + 1; obj_iter != obj_list + obj_cnt;
             ++obj_iter) {
            /* check unicity of uuid */
            if (!uuid && strcmp(curr->uuid, obj_iter->uuid))
                LOG_GOTO(out_free, rc = -EINVAL,
                        "Multiple deprecated uuids found "
                        "%s and %s",
                        curr->uuid, obj_iter->uuid);

            if (!version && curr->version < obj_iter->version)
                /* we found a more recent object */
                curr = obj_iter;
            else if (version == obj_iter->version)
                /* we found the matching version */
                curr = obj_iter;
        }
    }

    if (version != 0 && curr->version != version)
        LOG_GOTO(out_free, rc = -ENOENT, "No matching version found");

    *obj = object_info_dup(curr);

out_free:
    dss_res_free(obj_list, obj_cnt);
    return rc;
}

int dss_lazy_find_object(struct dss_handle *hdl, const char *oid,
                         const char *uuid, int version,
                         struct object_info **obj)
{
    struct object_info *obj_list = NULL;
    struct dss_filter filter;
    char *json_filter;
    int obj_cnt;
    int rc;

    ENTRY;

    rc = build_json_filter(&json_filter, oid, uuid, version);
    if (rc < 0)
        LOG_RETURN(rc = -ENOMEM, "Cannot allocate filter string");

    rc = dss_filter_build(&filter, "%s", json_filter);
    free(json_filter);
    if (rc)
        LOG_RETURN(rc, "Cannot build filter");

    rc = dss_object_get(hdl, &filter, &obj_list, &obj_cnt);
    dss_filter_free(&filter);
    if (rc)
        LOG_RETURN(rc, "Cannot fetch objid: '%s'", oid);

    assert(obj_cnt <= 1);

    /* If an object was found in the object table, we try to match it with the
     * given uuid and/or version. Otherwise, we look in the deprecated_object
     * table.
     */
    if (obj_cnt == 1 &&
        /* If the oid is not provided, the uuid is and the filter
         * will handle the version. No need to check.
         */
        ((!oid) ||
         /* If oid is provided, the filter will handle the uuid,
          * but the version is not necessarily used in the filter.
          * Check that the version is correct.
          */
         (oid && (!version || version == obj_list->version)))) {

        *obj = object_info_dup(obj_list);
    } else {
        if (obj_cnt == 1) {
            /* Target the current generation if uuid not provided.
             * At this point, version != 0.
             */
            if (!uuid)
                uuid = obj_list->uuid;
        }

        if (version || uuid) {
            rc = lazy_find_deprecated_object(hdl, oid, uuid, version, obj);
            if (rc == -ENOENT)
                LOG_GOTO(out_free, rc, "No such object objid: '%s'", oid);
            else if (rc)
                LOG_GOTO(out_free, rc, "Error while trying to get object: '%s'",
                         oid);
        } else {
            LOG_GOTO(out_free, rc = -ENOENT, "No such object objid: '%s'", oid);
        }
    }

out_free:
    dss_res_free(obj_list, obj_cnt);
    return rc;
}

int dss_medium_locate(struct dss_handle *dss, const struct pho_id *medium_id,
                      char **hostname, struct media_info **_medium_info)
{
    struct media_info *medium_info;
    int rc;

    *hostname = NULL;
    rc = dss_one_medium_get_from_id(dss, medium_id, &medium_info);
    if (rc)
        LOG_RETURN(rc, "Unable to get medium_info to locate");

    /* check ADMIN STATUS to see if the medium is available */
    if (medium_info->rsc.adm_status != PHO_RSC_ADM_ST_UNLOCKED) {
        pho_warn("Medium (family %s, name %s) is admin locked",
                 rsc_family2str(medium_id->family), medium_id->name);
        GOTO(clean, rc = -EACCES);
    }

    if (!medium_info->flags.get) {
        pho_warn("Get are prevented by operation flag on this medium "
                 "(family %s, name %s)",
                 rsc_family2str(medium_id->family), medium_id->name);
        GOTO(clean, rc = -EPERM);
    }

    if (_medium_info != NULL)
        *_medium_info = media_info_dup(medium_info);

    /* medium without any lock */
    if (!medium_info->lock.owner) {
        if (medium_info->rsc.id.family == PHO_RSC_DIR)
            GOTO(clean, rc = -ENODEV);

        *hostname = NULL;
        GOTO(clean, rc = 0);
    }

    /* get lock hostname */
    *hostname = xstrdup(medium_info->lock.hostname);

clean:
    dss_res_free(medium_info, 1);

    return rc;
}

int dss_logs_insert(struct dss_handle *hdl, struct pho_log *logs, int log_cnt)
{
    return dss_generic_set(hdl, DSS_LOGS, (void *) logs, log_cnt,
                           DSS_SET_INSERT, 0);
}

int dss_logs_delete(struct dss_handle *handle, const struct dss_filter *filter)
{
    GString *clause = NULL;
    int rc;

    if (filter == NULL)
        return dss_generic_set(handle, DSS_LOGS, NULL, 0, DSS_SET_DELETE, 0);

    clause = g_string_new(NULL);

    rc = clause_filter_convert(handle, clause, filter);
    if (rc) {
        g_string_free(clause, true);
        return rc;
    }

    rc = dss_generic_set(handle, DSS_LOGS, (void *) clause, 0, DSS_SET_DELETE,
                         0);
    g_string_free(clause, true);

    return rc;
}
