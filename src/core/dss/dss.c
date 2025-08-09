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
 * \brief  Phobos Distributed State Service API.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <gmodule.h>
#include <libpq-fe.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>

#include "pho_dss.h"
#include "pho_dss_wrapper.h"
#include "pho_types.h"
#include "pho_type_utils.h"
#include "dss_utils.h"

#include "deprecated.h"
#include "device.h"
#include "dss_config.h"
#include "dss_utils.h"
#include "filters.h"
#include "media.h"
#include "resources.h"
#include "object.h"

#define SCHEMA_INFO "3.0"

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
 * Handle notices from PostgreSQL. These are messages sent in or by PSQL, which
 * can be handled by a client application, like the DSS.
 *
 * This handler takes in the message, strip the trailing newline and re-emit
 * it as a Phobos log.
 *
 * https://www.postgresql.org/docs/current/libpq-notice-processing.html
 *
 * \param[in] arg      An optional argument given the client application (NULL
 *                     in our case)
 * \param[in] message  The notice to handle
 */
static void dss_pg_logger(void *arg, const char *message)
{
    size_t mlen = strlen(message);

    (void) arg;

    if (message[mlen - 1] == '\n')
        mlen -= 1;

    pho_info("%*s", (int)mlen, message);
}

static inline int check_db_version(struct dss_handle *handle)
{
    const char *query = "SELECT * "
                        "FROM schema_info "
                        "WHERE version = '"SCHEMA_INFO"';";
    PGresult *res;
    int rc = 0;

    rc = execute(handle->dh_conn, query, &res, PGRES_TUPLES_OK);

    if (!rc && PQntuples(res) != 1) {
        pho_error(rc ? rc : (rc = -EINVAL),
                  "Database schema version is not correct, "
                  "version '%s' is requested", SCHEMA_INFO);
    }
    PQclear(res);

    return rc;
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

    return check_db_version(handle);
}

void dss_fini(struct dss_handle *handle)
{
    PQfinish(handle->dh_conn);
}

static void _dss_result_free(struct dss_result *dss_res, int item_cnt)
{
    size_t item_size;
    int i;

    item_size = get_resource_size(dss_res->item_type);

    for (i = 0; i < item_cnt; i++)
        free_resource(dss_res->item_type, dss_res->items.raw + i * item_size);

    PQclear(dss_res->pg_res);
    free(dss_res);

}

int dss_execute_generic_get(struct dss_handle *handle, enum dss_type type,
                            GString *clause, void **item_list, int *item_cnt)
{
    PGconn *conn = handle->dh_conn;
    struct dss_result *dss_res;
    size_t dss_res_size;
    size_t item_size;
    PGresult *res;
    int rc = 0;
    int i;

    pho_debug("Executing request: '%s'", clause->str);

    rc = execute(conn, clause->str, &res, PGRES_TUPLES_OK);
    if (rc)
        return rc;

    item_size = get_resource_size(type);

    dss_res_size = sizeof(struct dss_result) + PQntuples(res) * item_size;
    dss_res = xcalloc(1, dss_res_size);

    dss_res->item_type = type;
    dss_res->pg_res = res;

    for (i = 0; i < PQntuples(res); i++) {
        void *item_ptr = (char *)&dss_res->items.raw + i * item_size;

        rc = create_resource(type, handle, item_ptr, res, i);
        if (rc)
            return rc;
    }

    *item_list = &dss_res->items.raw;
    *item_cnt = PQntuples(res);

    if (rc)
        /* Only free elements that were initialized, this also frees res */
        _dss_result_free(dss_res, i);

    return rc;
}

static int dss_generic_get(struct dss_handle *handle, enum dss_type type,
                           const struct dss_filter **filters, int filters_count,
                           void **item_list, int *item_cnt,
                           struct dss_sort *sort)
{
    PGconn *conn = handle->dh_conn;
    GString *clause = NULL;
    GString **conditions;
    size_t item_size;
    int rc = 0;
    int i = 0;

    ENTRY;

    if (conn == NULL || item_list == NULL || item_cnt == NULL)
        LOG_RETURN(-EINVAL, "dss - conn: %p, item_list: %p, item_cnt: %p",
                   conn, item_list, item_cnt);

    *item_list = NULL;
    *item_cnt  = 0;

    conditions = xmalloc(sizeof(*conditions) * filters_count);
    for (i = 0; i < filters_count; ++i) {
        conditions[i] = g_string_new(NULL);
        rc = clause_filter_convert(handle, conditions[i], filters[i]);
        if (rc) {
            for (; i >= 0; --i)
                g_string_free(conditions[i], true);

            free(conditions);
            return rc;
        }
    }

    clause = g_string_new(NULL);
    rc = get_select_query(type, conditions, filters_count, clause, sort);
    for (i = 0; i < filters_count; ++i)
        g_string_free(conditions[i], true);

    free(conditions);
    if (rc) {
        g_string_free(clause, true);
        return rc;
    }

    rc = dss_execute_generic_get(handle, type, clause, item_list, item_cnt);

    item_size = get_resource_size(type);
    if (sort && !sort->psql_sort) {
        if (type == DSS_FULL_LAYOUT && !strcmp(sort->attr, "size")) {
            quicksort(item_list, 0, *item_cnt - 1, item_size, sort->reverse,
                      cmp_size);
        }
    }

    g_string_free(clause, true);

    return rc;
}

static int dss_generic_set(struct dss_handle *handle, enum dss_type type,
                           void *item_list, int item_cnt,
                           enum dss_set_action action)
{
    PGconn *conn = handle->dh_conn;
    GString *request;
    int rc = 0;

    ENTRY;

    if (conn == NULL ||
        (type != DSS_LOGS && (item_list == NULL || item_cnt == 0)))
        LOG_RETURN(-EINVAL, "conn: %p, item_list: %p, item_cnt: %d",
                   conn, item_list, item_cnt);

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
    case DSS_SET_DELETE:
        rc = get_delete_query(type, item_list, item_cnt, request);
        break;
    default:
        LOG_GOTO(out_cleanup, rc = -ENOTSUP,
                 "unsupported DSS request action %#x", action);
    }

    if (rc)
        LOG_GOTO(out_cleanup, rc, "SQL request build failed");

    rc = execute_and_commit_or_rollback(conn, request, NULL, PGRES_COMMAND_OK);

out_cleanup:
    g_string_free(request, true);
    return rc;
}

static int dss_generic_update(struct dss_handle *handle, enum dss_type type,
                              void *src_list, void *dst_list, int item_cnt,
                              uint64_t fields)
{
    PGconn *conn = handle->dh_conn;
    GString *request;
    int rc = 0;

    ENTRY;

    if (conn == NULL ||
        (type != DSS_LOGS &&
         (src_list == NULL || dst_list == NULL || item_cnt == 0)))
        LOG_RETURN(-EINVAL,
                   "conn: %p, src_list: %p, dst_list: %p, item_cnt: %d",
                   conn, src_list, dst_list, item_cnt);

    request = g_string_new("BEGIN;");

    rc = get_update_query(type, conn, src_list, dst_list,  item_cnt,
                          fields, request);
    if (rc)
        LOG_GOTO(out_cleanup, rc, "SQL request build failed");

    rc = execute_and_commit_or_rollback(conn, request, NULL, PGRES_COMMAND_OK);

out_cleanup:
    g_string_free(request, true);
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

/*
 * DEVICE FUNCTIONS
 */

int dss_device_insert(struct dss_handle *handle, struct dev_info *device_list,
                      int device_count)
{
    return dss_generic_set(handle, DSS_DEVICE, (void *) device_list,
                           device_count, DSS_SET_INSERT);
}

int dss_device_update(struct dss_handle *handle, struct dev_info *src_list,
                      struct dev_info *dst_list, int device_count,
                      int64_t fields)
{
    return dss_generic_update(handle, DSS_DEVICE, (void *) src_list,
                              (void *) dst_list, device_count, fields);
}

int dss_device_get(struct dss_handle *handle, const struct dss_filter *filter,
                   struct dev_info **device_list, int *device_count,
                   struct dss_sort *sort)
{
    return dss_generic_get(handle, DSS_DEVICE,
                           (const struct dss_filter*[]) {filter, NULL}, 1,
                           (void **)device_list, device_count, sort);
}

int dss_device_delete(struct dss_handle *handle, struct dev_info *device_list,
                      int device_count)
{
    return dss_generic_set(handle, DSS_DEVICE, (void *)device_list,
                           device_count, DSS_SET_DELETE);
}

/*
 * MEDIA FUNCTIONS
 */

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

int dss_media_insert(struct dss_handle *handle, struct media_info *media_list,
                     int media_count)
{
    return dss_generic_set(handle, DSS_MEDIA, (void *) media_list,
                           media_count, DSS_SET_INSERT);
}

int dss_media_update(struct dss_handle *handle, struct media_info *src_list,
                     struct media_info *dst_list, int media_count,
                     uint64_t fields)
{
    int rc;
    int i;

    if (!fields) {
        pho_warn("Tried updating media without specifying any field");
        return 0;
    }

    for (i = 0; i < media_count; i++) {
        if (src_list[i].rsc.id.family == PHO_RSC_DIR) {
            rc = _normalize_path(src_list[i].rsc.id.name);
            if (rc)
                return rc;
        }
    }

    /**
     * The only set action that needs a lock is the stats update action. Indeed,
     * we need to get the full existing stat SQL column to fill only new fields
     * and keep old value.
     *
     * All other set actions (insert, delete, or update of other fields) are
     * atomic SQL requests and don't need any lock and prefetch.
     */
    if (IS_STAT(fields)) {

        rc = media_update_lock_retry(handle, src_list, media_count);
        if (rc)
            LOG_RETURN(rc, "Error when locking media to %s",
                       dss_set_actions_names[DSS_SET_UPDATE]);

        for (i = 0; i < media_count; i++) {
            struct media_info *medium_info;

            rc = dss_one_medium_get_from_id(handle, &src_list[i].rsc.id,
                                            &medium_info);
            if (rc)
                LOG_GOTO(clean, rc,
                         "Error on getting medium_info "FMT_PHO_ID
                         " to update stats",
                         PHO_ID(src_list[i].rsc.id));

            if (NB_OBJ & fields)
                medium_info->stats.nb_obj = dst_list[i].stats.nb_obj;

            if (NB_OBJ_ADD & fields)
                medium_info->stats.nb_obj += dst_list[i].stats.nb_obj;


            /*
             * DSS stats values must be positive values even if previous
             * approximations compute negative values.
             */
            if (medium_info->stats.nb_obj < 0)
                medium_info->stats.nb_obj = 0;

            if (LOGC_SPC_USED & fields)
                medium_info->stats.logc_spc_used =
                    dst_list[i].stats.logc_spc_used;

            if (LOGC_SPC_USED_ADD & fields)
                medium_info->stats.logc_spc_used +=
                    dst_list[i].stats.logc_spc_used;

            if (medium_info->stats.logc_spc_used < 0)
                medium_info->stats.logc_spc_used = 0;

            if (PHYS_SPC_USED & fields) {
                medium_info->stats.phys_spc_used =
                    dst_list[i].stats.phys_spc_used;
                if (medium_info->stats.phys_spc_used < 0)
                    medium_info->stats.phys_spc_used = 0;
            }

            if (PHYS_SPC_FREE & fields) {
                medium_info->stats.phys_spc_free =
                    dst_list[i].stats.phys_spc_free;
                if (medium_info->stats.phys_spc_free < 0)
                    medium_info->stats.phys_spc_free = 0;
            }

            dst_list[i].stats = medium_info->stats;
            dss_res_free(medium_info, 1);
        }
    }

    rc = dss_generic_update(handle, DSS_MEDIA, (void *)src_list,
                            (void *)dst_list, media_count, fields);

clean:
    if (IS_STAT(fields)) {
        int rc2;

        rc2 = dss_unlock(handle, DSS_MEDIA_UPDATE_LOCK, src_list, media_count,
                         false);
        if (rc2) {
            pho_error(rc2, "Error when unlocking media at end of %s",
                      dss_set_actions_names[DSS_SET_UPDATE]);
            rc = rc ? : rc2;
        }
    }

    return rc;
}

int dss_media_get(struct dss_handle *handle, const struct dss_filter *filter,
                  struct media_info **media_list, int *media_count,
                  struct dss_sort *sort)
{
    return dss_generic_get(handle, DSS_MEDIA,
                           (const struct dss_filter*[]) {filter, NULL}, 1,
                           (void **) media_list, media_count, sort);
}

int dss_media_delete(struct dss_handle *handle, struct media_info *media_list,
                     int media_count)
{
    return dss_generic_set(handle, DSS_MEDIA, (void *) media_list,
                           media_count, DSS_SET_DELETE);
}

/*
 * LAYOUT FUNCTIONS
 */

int dss_layout_get(struct dss_handle *handle, const struct dss_filter *filter,
                   struct layout_info **layouts, int *layout_count)
{
    return dss_generic_get(handle, DSS_LAYOUT,
                           (const struct dss_filter*[]) {filter, NULL}, 1,
                           (void **)layouts, layout_count, NULL);
}

int dss_layout_insert(struct dss_handle *handle,
                      struct layout_info *layout_list,
                      int layout_count)
{
    return dss_generic_set(handle, DSS_LAYOUT, (void *)layout_list,
                           layout_count, DSS_SET_INSERT);
}

int dss_layout_delete(struct dss_handle *handle,
                      struct layout_info *layout_list, int layout_count)
{
    return dss_generic_set(handle, DSS_LAYOUT, (void *)layout_list,
                           layout_count, DSS_SET_DELETE);
}

/*
 * FULL LAYOUT FUNCTIONS
 */

int dss_full_layout_get(struct dss_handle *hdl, const struct dss_filter *object,
                        const struct dss_filter *med_lib,
                        struct layout_info **layouts, int *layout_count,
                        struct dss_sort *sort)
{
    return dss_generic_get(hdl, DSS_FULL_LAYOUT,
                           (const struct dss_filter*[])
                           {object, med_lib}, 2, (void **)layouts,
                           layout_count, sort);
}

/*
 * EXTENT FUNCTIONS
 */

int dss_extent_get(struct dss_handle *handle, const struct dss_filter *filter,
                   struct extent **extents, int *extent_count)
{
    return dss_generic_get(handle, DSS_EXTENT,
                           (const struct dss_filter*[]) {filter, NULL}, 1,
                           (void **)extents, extent_count, NULL);
}

int dss_extent_insert(struct dss_handle *handle, struct extent *extents,
                   int extent_count)
{
    return dss_generic_set(handle, DSS_EXTENT, (void *)extents, extent_count,
                           DSS_SET_INSERT);
}

int dss_extent_update(struct dss_handle *handle, struct extent *src_extents,
                      struct extent *dst_extents, int extent_count)
{
    return dss_generic_update(handle, DSS_EXTENT, (void *) src_extents,
                              (void *) dst_extents, extent_count, 0);
}

int dss_extent_delete(struct dss_handle *handle, struct extent *extents,
                      int extent_count)
{
    return dss_generic_set(handle, DSS_EXTENT, (void *)extents, extent_count,
                           DSS_SET_DELETE);
}

/*
 * OBJECT FUNCTION
 */

int dss_object_insert(struct dss_handle *handle,
                      struct object_info *object_list,
                      int object_count, enum dss_set_action action)
{
    if (action != DSS_SET_INSERT && action != DSS_SET_FULL_INSERT)
        LOG_RETURN(-ENOTSUP,
                   "Only actions available for object insert are normal insert "
                   "and full insert");

    return dss_generic_set(handle, DSS_OBJECT, (void *)object_list,
                           object_count, action);
}

int dss_object_update(struct dss_handle *handle, struct object_info *src_list,
                      struct object_info *dst_list, int object_count,
                      int64_t fields)
{
    return dss_generic_update(handle, DSS_OBJECT, (void *)src_list,
                              (void *)dst_list, object_count, fields);
}

int dss_object_get(struct dss_handle *handle, const struct dss_filter *filter,
                   struct object_info **object_list, int *object_count,
                   struct dss_sort *sort)
{
    return dss_generic_get(handle, DSS_OBJECT,
                           (const struct dss_filter*[]) {filter, NULL}, 1,
                           (void **)object_list, object_count, sort);
}

int dss_object_delete(struct dss_handle *handle,
                      struct object_info *object_list,
                      int object_count)
{
    return dss_generic_set(handle, DSS_OBJECT, (void *)object_list,
                           object_count, DSS_SET_DELETE);
}

int dss_object_rename(struct dss_handle *handle,
                      struct object_info *objects, int objects_count,
                      struct object_info *deprec_objects, int deprec_count,
                      char *new_oid)
{
    GString *request = g_string_new("BEGIN;");
    struct object_info old_object_name = {
        .oid = (objects_count ? objects[0].oid : deprec_objects[0].oid)
    };
    struct object_info new_object_name = {
        .oid = new_oid
    };
    int rc2 = 0;
    int rc = 0;

    rc = dss_lock(handle, DSS_OBJECT, &old_object_name, 1);
    if (rc)
        LOG_RETURN(rc, "Unable to lock objects with name '%s'",
                   old_object_name.oid);

    rc = dss_lock(handle, DSS_OBJECT, &new_object_name, 1);
    if (rc)
        LOG_RETURN(rc, "Unable to lock objects with name '%s'",
                   new_object_name.oid);

    /* Set new_oid as the oid field of object lists */
    for (int i = 0; i < objects_count; i++)
        objects[i].oid = strdup(new_oid);
    for (int i = 0; i < deprec_count; i++)
        deprec_objects[i].oid = strdup(new_oid);

    rc = get_update_query(DSS_OBJECT, handle->dh_conn, objects, objects,
                          objects_count, DSS_OBJECT_UPDATE_OID, request);
    if (rc)
        LOG_GOTO(out_unlock, rc,
                 "Failed to create update query for '%d' object(s)",
                 objects_count);

    rc = get_update_query(DSS_DEPREC, handle->dh_conn, deprec_objects,
                          deprec_objects, deprec_count, DSS_OBJECT_UPDATE_OID,
                          request);
    if (rc)
        LOG_GOTO(out_unlock, rc,
                 "Failed to create update query for '%d' deprec object(s)",
                 deprec_count);

    rc = execute_and_commit_or_rollback(handle->dh_conn, request, NULL,
                                        PGRES_COMMAND_OK);
    if (rc)
        LOG_RETURN(rc, "Unable to rename objects from '%s' to '%s'",
                   old_object_name.oid, new_object_name.oid);

out_unlock:
    rc = dss_unlock(handle, DSS_OBJECT, &old_object_name, 1, true);
    if (rc)
        LOG_RETURN(rc, "Unable to lock objects with name '%s'",
                   old_object_name.oid);

    rc = dss_unlock(handle, DSS_OBJECT, &new_object_name, 1, true);
    if (rc)
        LOG_RETURN(rc, "Unable to lock objects with name '%s'",
                   new_object_name.oid);

    g_string_free(request, true);

    return rc ? rc : rc2;
}

/*
 * DEPRECATED FUNCTION
 */

int dss_deprecated_object_insert(struct dss_handle *handle,
                                 struct object_info *object_list,
                                 int object_count)
{
    return dss_generic_set(handle, DSS_DEPREC, (void *) object_list,
                           object_count, DSS_SET_INSERT);
}

int dss_deprecated_object_update(struct dss_handle *handle,
                                 struct object_info *src_list,
                                 struct object_info *dst_list,
                                 int object_count, int64_t fields)
{
    return dss_generic_update(handle, DSS_DEPREC, (void *) src_list,
                              (void *) dst_list, object_count, fields);
}

int dss_deprecated_object_get(struct dss_handle *handle,
                              const struct dss_filter *filter,
                              struct object_info **object_list,
                              int *object_count, struct dss_sort *sort)
{
    return dss_generic_get(handle, DSS_DEPREC,
                           (const struct dss_filter*[]) {filter, NULL}, 1,
                           (void **)object_list, object_count, sort);
}

int dss_deprecated_object_delete(struct dss_handle *handle,
                                 struct object_info *object_list,
                                 int object_count)
{
    return dss_generic_set(handle, DSS_DEPREC, (void *)object_list,
                           object_count, DSS_SET_DELETE);
}

/*
 * COPY FUNCTION
 */
int dss_copy_insert(struct dss_handle *handle, struct copy_info *copy_list,
                    int copy_count)
{
    return dss_generic_set(handle, DSS_COPY, (void *)copy_list, copy_count,
                           DSS_SET_INSERT);
}

int dss_copy_update(struct dss_handle *handle, struct copy_info *src_list,
                    struct copy_info *dst_list, int copy_count, int64_t fields)
{
    return dss_generic_update(handle, DSS_COPY, (void *)src_list,
                              (void *)dst_list, copy_count, fields);
}

int dss_copy_get(struct dss_handle *handle, const struct dss_filter *filter,
                 struct copy_info **copy_list, int *copy_count,
                 struct dss_sort *sort)
{
    return dss_generic_get(handle, DSS_COPY,
                           (const struct dss_filter*[]) {filter, NULL}, 1,
                           (void **)copy_list, copy_count, sort);
}

int dss_copy_delete(struct dss_handle *handle, struct copy_info *copy_list,
                    int copy_count)
{
    return dss_generic_set(handle, DSS_COPY, (void *)copy_list,
                           copy_count, DSS_SET_DELETE);
}

/*
 * LOGS FUNCTION
 */

int dss_logs_get(struct dss_handle *hdl, const struct dss_filter *filter,
                 struct pho_log **logs_ls, int *logs_cnt)
{
    return dss_generic_get(hdl, DSS_LOGS,
                           (const struct dss_filter*[]) {filter, NULL}, 1,
                           (void **)logs_ls, logs_cnt, NULL);
}

int dss_logs_insert(struct dss_handle *hdl, struct pho_log *logs, int log_cnt)
{
    return dss_generic_set(hdl, DSS_LOGS, (void *) logs, log_cnt,
                           DSS_SET_INSERT);
}

int dss_logs_delete(struct dss_handle *handle, const struct dss_filter *filter)
{
    GString *clause = NULL;
    int rc;

    if (filter == NULL)
        return dss_generic_set(handle, DSS_LOGS, NULL, 0, DSS_SET_DELETE);

    clause = g_string_new(NULL);

    rc = clause_filter_convert(handle, clause, filter);
    if (rc) {
        g_string_free(clause, true);
        return rc;
    }

    rc = dss_generic_set(handle, DSS_LOGS, (void *) clause, 0, DSS_SET_DELETE);
    g_string_free(clause, true);

    return rc;
}
