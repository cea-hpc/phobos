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

#include "deprecated.h"
#include "device.h"
#include "dss_config.h"
#include "dss_utils.h"
#include "filters.h"
#include "media.h"
#include "resources.h"
#include "object.h"

#define SCHEMA_INFO "2.1"

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

static int dss_generic_get(struct dss_handle *handle, enum dss_type type,
                           const struct dss_filter **filters,
                           void **item_list, int *item_cnt)
{
    PGconn *conn = handle->dh_conn;
    struct dss_result *dss_res;
    GString *clause = NULL;
    GString **conditions;
    size_t dss_res_size;
    int n_conditions;
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

    conditions = xmalloc(sizeof(*conditions) * 2);
    conditions[0] = g_string_new(NULL);
    rc = clause_filter_convert(handle, conditions[0], filters[0]);
    if (rc) {
        g_string_free(conditions[0], true);
        return rc;
    }

    if (type == DSS_FULL_LAYOUT) {
        conditions[1] = g_string_new(NULL);
        rc = clause_filter_convert(handle, conditions[1], filters[1]);
        if (rc) {
            g_string_free(conditions[0], true);
            g_string_free(conditions[1], true);
            return rc;
        }

        n_conditions = 2;
    } else {
        n_conditions = 1;
    }

    clause = g_string_new(NULL);
    rc = get_select_query(type, conditions, n_conditions, clause);
    g_string_free(conditions[0], true);

    if (type == DSS_FULL_LAYOUT)
        g_string_free(conditions[1], true);

    free(conditions);
    if (rc) {
        g_string_free(clause, true);
        return rc;
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

    dss_res_size = sizeof(struct dss_result) + PQntuples(res) * item_size;
    dss_res = xcalloc(1, dss_res_size);

    dss_res->item_type = type;
    dss_res->pg_res = res;

    for (i = 0; i < PQntuples(res); i++) {
        void *item_ptr = (char *)&dss_res->items.raw + i * item_size;

        rc = create_resource(type, handle, item_ptr, res, i);
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
    PGconn *conn = handle->dh_conn;
    GString *request;
    int rc = 0;

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
        LOG_GOTO(out_cleanup, rc = -ENOTSUP,
                 "unsupported DSS request action %#x", action);
    }

    if (rc)
        LOG_GOTO(out_cleanup, rc, "SQL request failed");

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
