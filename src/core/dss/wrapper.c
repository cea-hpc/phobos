/*
 *  All rights reserved (c) 2014-2024 CEA/DAM.
 *
 *  This file is part of Phobos.
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
 * \brief Phobos Distributed State Service API for wrapping specific DSS actions
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <errno.h>
#include <glib.h>
#include <libpq-fe.h>
#include <stdio.h>

#include "pho_common.h"
#include "pho_dss.h"
#include "pho_dss_wrapper.h"
#include "pho_type_utils.h"
#include "pho_cfg.h"

#include "dss_utils.h"
#include "filters.h"
#include "logs.h"

int dss_get_usable_devices(struct dss_handle *hdl, const enum rsc_family family,
                           const char *host, struct dev_info **dev_ls,
                           int *dev_cnt)
{
    struct dss_filter filter;
    char *host_filter = NULL;
    int rc;

    if (host) {
        rc = asprintf(&host_filter, "{\"DSS::DEV::host\": \"%s\"},", host);
        if (rc < 0)
            return -ENOMEM;
    }

    rc = dss_filter_build(&filter,
                          "{\"$AND\": ["
                          "  %s"
                          "  {\"DSS::DEV::adm_status\": \"%s\"},"
                          "  {\"DSS::DEV::family\": \"%s\"}"
                          "]}",
                          host ? host_filter : "",
                          rsc_adm_status2str(PHO_RSC_ADM_ST_UNLOCKED),
                          rsc_family2str(family));
    free(host_filter);
    if (rc)
        return rc;

    rc = dss_device_get(hdl, &filter, dev_ls, dev_cnt, NULL);
    dss_filter_free(&filter);
    return rc;
}

int dss_device_health(struct dss_handle *dss, const struct pho_id *device_id,
                      size_t max_health, size_t *health)
{
    return dss_resource_health(dss, device_id, DSS_DEVICE, max_health, health);
}

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
                              "{\"DSS::MDA::id\": \"%s\"}, "
                              "{\"DSS::MDA::library\": \"%s\"}"
                          "]}",
                          rsc_family2str(medium_id->family),
                          medium_id->name, medium_id->library);
    if (rc)
        LOG_RETURN(rc,
                   "Unable to build filter for media "FMT_PHO_ID,
                   PHO_ID(*medium_id));

    rc = dss_media_get(dss, &filter, medium_info, &cnt, NULL);
    dss_filter_free(&filter);
    if (rc)
        LOG_RETURN(rc,
                   "Error while getting medium info "FMT_PHO_ID,
                   PHO_ID(*medium_id));

    /* (family, id) is the primary key of the media table */
    assert(cnt <= 1);

    if (cnt == 0) {
        pho_warn("Medium "FMT_PHO_ID" is absent from media "
                 "table", PHO_ID(*medium_id));
        dss_res_free(*medium_info, cnt);
        return(-ENOENT);
    }

    return 0;
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
        pho_warn("Medium "FMT_PHO_ID" is admin locked", PHO_ID(*medium_id));
        GOTO(clean, rc = -EACCES);
    }

    if (!medium_info->flags.get) {
        pho_warn("Get are prevented by operation flag on this "
                 "medium "FMT_PHO_ID, PHO_ID(*medium_id));
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

int dss_medium_health(struct dss_handle *dss, const struct pho_id *medium_id,
                      size_t max_health, size_t *health)
{
    return dss_resource_health(dss, medium_id, DSS_MEDIA, max_health, health);
}

/**
 * Print additional information if the retrieval of a deprecated object failed.
 */
static void pho_error_oid_uuid_version(int error_code, const char *message,
                                       const char *oid, const char *uuid,
                                       int version)
{
    GString *string = g_string_new(NULL);
    int count = 0;

    count += (oid ? 1 : 0);
    count += (uuid ? 1 : 0);
    count += (version ? 1 : 0);

    g_string_append(string, message);
    g_string_append(string, ": ");

    if (oid)
        g_string_append_printf(string, "oid = '%s'%s",
                               oid, --count != 0 ? ", " : "");

    if (uuid)
        g_string_append_printf(string, "uuid = '%s'%s",
                               uuid, --count != 0 ? ", " : "");

    if (version)
        g_string_append_printf(string, "version = '%d'", version);

    pho_error(error_code, "%s", string->str);
    g_string_free(string, true);
}

/**
 * Find a deprecated object matching the given \p oid, \p uuid and \p version.
 *
 * \p oid and \p uuid cannot be both NULL.
 * If \p version is 0, will search the most recent object matching the given
 * \p oid or \p uuid.
 * If \p oid is non NULL but \p uuid is, and multiple objects with different
 * uuid are found, this call with return an error.
 *
 * \param[in]  hdl      A handle to the DSS
 * \param[in]  oid      The oid of the object to find, can be NULL
 * \param[in]  uuid     The uuid of the object to find, can be NULL
 * \param[in]  version  The version of the object to find, can be 0
 * \param[out] obj      The object found matching all conditions, or the most
 *                      recent if the version is not provided
 *
 * \return 0 on success, -ENOENT if not object matching the criteria is found
 *                       -EINVAL if the uuid is NULL but multiple objects with
 *                       different uuid are found
 *                       negative error code on error
 */
static int lazy_find_deprecated_object(struct dss_handle *hdl, const char *oid,
                                       const char *uuid, int version,
                                       struct object_info **obj)
{
    struct object_info *obj_iter;
    struct object_info *obj_list;
    struct dss_filter filter;
    struct object_info *curr;
    char *json_filter;
    int obj_cnt;
    int rc;

    ENTRY;

    build_object_json_filter(&json_filter, oid, uuid, version);

    rc = dss_filter_build(&filter, "%s", json_filter);
    free(json_filter);
    if (rc)
        LOG_RETURN(rc, "Cannot build filter");

    rc = dss_deprecated_object_get(hdl, &filter, &obj_list, &obj_cnt, NULL);
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

    build_object_json_filter(&json_filter, oid, uuid, version);

    rc = dss_filter_build(&filter, "%s", json_filter);
    free(json_filter);
    if (rc)
        LOG_RETURN(rc, "Cannot build filter");

    rc = dss_object_get(hdl, &filter, &obj_list, &obj_cnt, NULL);
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

static int dss_find_deprec_object(struct dss_handle *hdl,
                                  struct dss_filter *filter,
                                  const char *oid, const char *uuid,
                                  int version, struct object_info **obj)
{
    struct dss_sort sort = { "version", true, false, true };
    struct object_info *obj_list = NULL;
    int obj_cnt;
    int rc;

    ENTRY;

    rc = dss_deprecated_object_get(hdl, filter, &obj_list, &obj_cnt, &sort);
    if (rc)
        LOG_RETURN(rc, "Cannot fetch deprecated objid: '%s'", oid);

    if (obj_cnt == 0)
        LOG_GOTO(out_free, rc = -ENOENT, "No such deprecated object");

    if (obj_cnt > 1) {
        if (!uuid && !version)
            LOG_GOTO(out_free, rc = -EINVAL,
                     "Several object found for the objid : '%s'", oid);
        if (!uuid && version)
            LOG_GOTO(out_free, rc = -EINVAL,
                     "Several object found for the objid '%s' and "
                     "version %d", oid, version);
    }

    *obj = object_info_dup(obj_list);

out_free:
    dss_res_free(obj_list, obj_cnt);

    return rc;
}

int dss_find_object(struct dss_handle *hdl, const char *oid,
                    const char *uuid, int version, enum dss_obj_scope scope,
                    struct object_info **obj)
{
    GString *filter_str = g_string_new(NULL);
    struct object_info *obj_list = NULL;
    struct dss_filter filter;
    int obj_cnt;
    int rc;

    ENTRY;

    g_string_append_printf(filter_str,
                           "{\"$AND\": [ "
                           "{\"DSS::OBJ::oid\": \"%s\"}%s",
                           oid, (uuid != NULL || version != 0) ? ", " : "");

    if (uuid)
        g_string_append_printf(filter_str, "{\"DSS::OBJ::uuid\": \"%s\"}%s",
                               uuid, (version != 0) ? ", " : "");

    if (version)
        g_string_append_printf(filter_str, "{\"DSS::OBJ::version\": \"%d\"}",
                               version);

    g_string_append(filter_str, "]}");

    rc = dss_filter_build(&filter, "%s", filter_str->str);
    g_string_free(filter_str, true);
    if (rc)
        LOG_RETURN(rc, "Cannot build filter");

    if (scope == DSS_OBJ_DEPRECATED) {
        rc = dss_find_deprec_object(hdl, &filter, oid, uuid, version, obj);
    } else {
        rc = dss_object_get(hdl, &filter, &obj_list, &obj_cnt, NULL);
        if (rc)
            LOG_GOTO(out_free, rc, "Cannot fetch objid: '%s'", oid);

        assert(obj_cnt <= 1);

        if (obj_cnt == 1) {
            *obj = object_info_dup(obj_list);
        } else if (scope == DSS_OBJ_ALL) {
            /* If no object found in alive and deprec is True, search in
             * deprecated object table
             */
            rc = dss_find_deprec_object(hdl, &filter, oid, uuid, version, obj);
        } else {
            rc = -ENOENT;
        }
    }

out_free:
    dss_res_free(obj_list, obj_cnt);
    dss_filter_free(&filter);

    return rc;
}

/**
 * Create a list of oids to query.
 *
 * \param[in]  conn      A database connection, used to escape the oids
 * \param[out] list      The list that will contain the escaped oids
 * \param[in]  obj_list  A list of objects from which to extract the oids
 * \param[in]  obj_cnt   The length of \p obj_list
 *
 * \return 0 on success, -EINVAL if an escaping fails
 */
static int prepare_oid_list(PGconn *conn, GString *list,
                            struct object_info *obj_list, int obj_cnt)
{
    for (int i = 0; i < obj_cnt; ++i) {
        char *escaped_oid;

        escaped_oid = PQescapeLiteral(conn, obj_list[i].oid,
                                    strlen(obj_list[i].oid));
        if (!escaped_oid)
            LOG_RETURN(-EINVAL,
                       "Cannot escape litteral %s: %s",
                       obj_list[i].oid, PQerrorMessage(conn));

        g_string_append_printf(list, "oid = %s", escaped_oid);
        PQfreemem(escaped_oid);

        if (i + 1 != obj_cnt)
            g_string_append(list, " OR ");
    }

    return 0;
}

int dss_move_object_to_deprecated(struct dss_handle *handle,
                                  struct object_info *obj_list,
                                  int obj_cnt)
{
    PGconn *conn = handle->dh_conn;
    PGresult *res = NULL;
    GString *oid_list;
    GString *clause;
    int rc = 0;

    ENTRY;

    oid_list = g_string_new(NULL);

    rc = prepare_oid_list(conn, oid_list, obj_list, obj_cnt);
    if (rc)
        LOG_GOTO(free_oid_list, rc, "OID list could not be built");

    clause = g_string_new(NULL);

    g_string_append_printf(clause,
                           "WITH moved_object AS"
                           " (DELETE FROM object WHERE %s RETURNING"
                           "  oid, object_uuid, version, user_md,"
                           "  creation_time)"
                           " INSERT INTO deprecated_object"
                           "  (oid, object_uuid, version, user_md,"
                           "   creation_time)"
                           " SELECT * FROM moved_object",
                           oid_list->str);

    pho_debug("Executing request: '%s'", clause->str);

    res = PQexec(conn, clause->str);
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        rc = psql_state2errno(res);
        pho_error(rc, "Query '%s' failed: %s", clause->str,
                  PQresultErrorField(res, PG_DIAG_MESSAGE_PRIMARY));
    }

    PQclear(res);

    g_string_free(clause, true);

free_oid_list:
    g_string_free(oid_list, true);

    return rc;
}

/**
 * Create a list of conditions to select \p obj_cnt deprecated entries.
 * Only the uuid and version of each object in \p obj_list is used.
 */
static int prepare_uuid_version_list(PGconn *conn, GString *list,
                                     struct object_info *obj_list, int obj_cnt)
{
    for (int i = 0; i < obj_cnt; ++i) {
        char *escaped_uuid;

        escaped_uuid = PQescapeLiteral(conn, obj_list[i].uuid,
                                       strlen(obj_list[i].uuid));
        if (!escaped_uuid)
            LOG_RETURN(-EINVAL,
                       "Cannot escape litteral %s: %s",
                       obj_list[i].uuid, PQerrorMessage(conn));

        g_string_append_printf(list,
                               "object_uuid = %s AND version = '%d'",
                               escaped_uuid, obj_list[i].version);
        PQfreemem(escaped_uuid);

        if (i + 1 != obj_cnt)
            g_string_append(list, " OR ");
    }

    return 0;
}

int dss_move_deprecated_to_object(struct dss_handle *handle,
                                  struct object_info *obj_list,
                                  int obj_cnt)
{
    PGconn *conn = handle->dh_conn;
    PGresult *res = NULL;
    GString *oid_list;
    GString *clause;
    int rc = 0;

    ENTRY;

    oid_list = g_string_new(NULL);

    rc = prepare_uuid_version_list(conn, oid_list, obj_list, obj_cnt);
    if (rc)
        LOG_GOTO(free_oid_list, rc, "OID list could not be built");

    clause = g_string_new(NULL);

    g_string_append_printf(clause,
                           "WITH risen_object AS"
                           " (DELETE FROM deprecated_object WHERE %s"
                           "  RETURNING oid, object_uuid,"
                           "  version, user_md, creation_time)"
                           " INSERT INTO object (oid, object_uuid, "
                           "  version, user_md, creation_time)"
                           " SELECT * FROM risen_object",
                           oid_list->str);

    pho_debug("Executing request: '%s'", clause->str);

    res = PQexec(conn, clause->str);
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        rc = psql_state2errno(res);
        pho_error(rc, "Query '%s' failed: %s", clause->str,
                  PQresultErrorField(res, PG_DIAG_MESSAGE_PRIMARY));
    }

    PQclear(res);

    g_string_free(clause, true);

free_oid_list:
    g_string_free(oid_list, true);

    return rc;
}

int dss_update_extent_migrate(struct dss_handle *handle, const char *old_uuid,
                              const char *new_uuid)
{
    GString *request = g_string_new("BEGIN;");
    PGresult *res;
    int rc = 0;

    g_string_append_printf(request,
        "UPDATE layout SET extent_uuid = '%s' WHERE extent_uuid = '%s';"
        "UPDATE extent SET state = 'orphan' WHERE extent_uuid = '%s';"
        "UPDATE extent SET state = 'sync' WHERE extent_uuid = '%s';",
        new_uuid, old_uuid, old_uuid, new_uuid);

    rc = execute_and_commit_or_rollback(handle->dh_conn, request, &res,
                                        PGRES_COMMAND_OK);
    g_string_free(request, true);
    return rc;
}

int dss_update_extent_state(struct dss_handle *handle, const char **uuids,
                            int num_uuids, enum extent_state state)
{
    GString *request = g_string_new("BEGIN;");
    PGresult *res;
    int rc = 0;
    int i;

    if (num_uuids < 1)
        goto req_free;

    g_string_append_printf(request, "UPDATE extent SET state = '%s' WHERE ",
                           extent_state2str(state));

    for (i = 0; i < num_uuids; ++i)
        g_string_append_printf(request, "extent_uuid = '%s'%s", uuids[i],
                               i == num_uuids - 1 ? ";" : " OR ");

    rc = execute_and_commit_or_rollback(handle->dh_conn, request, &res,
                                        PGRES_COMMAND_OK);

req_free:
    g_string_free(request, true);
    return rc;
}

static int check_orphan(struct dss_handle *handle, const struct pho_id *tape)
{
    GString *request = g_string_new("BEGIN;");
    PGresult *res;
    int rc = 0;

    g_string_append_printf(request,
        "UPDATE extent SET state = 'orphan' "
        "WHERE extent_uuid IN ("
        "  SELECT extent.extent_uuid FROM extent "
        "    LEFT JOIN layout ON extent.extent_uuid = layout.extent_uuid "
        "  WHERE layout.extent_uuid IS NULL AND "
        "    extent.medium_id = '%s' AND extent.medium_family = '%s' AND "
        "    extent.medium_library = '%s');",
        tape->name, rsc_family2str(tape->family), tape->library);

    rc = execute_and_commit_or_rollback(handle->dh_conn, request, &res,
                                        PGRES_COMMAND_OK);
    g_string_free(request, true);
    return rc;
}

int dss_update_gc_for_tape(struct dss_handle *handle, const struct pho_id *tape)
{
    GString *request = g_string_new("BEGIN;");
    PGresult *res;
    int rc = 0;

    g_string_append_printf(request,
        "WITH objects AS ("
        "  DELETE FROM deprecated_object"
        "  WHERE object_uuid IN ("
        "    SELECT object_uuid FROM layout"
        "    INNER JOIN ("
        "      SELECT extent_uuid FROM extent"
        "      WHERE medium_id = '%s' AND medium_family = '%s' AND"
        "            medium_library = '%s'"
        "    ) AS inner_table USING (extent_uuid)"
        "    WHERE object_uuid = layout.object_uuid"
        "     AND version = layout.version"
        "  ) RETURNING object_uuid, version"
        ") "
        "DELETE FROM layout "
        "WHERE EXISTS ("
        "  SELECT 1 FROM objects"
        "  WHERE object_uuid = layout.object_uuid"
        "   AND version = layout.version"
        ");", tape->name, rsc_family2str(tape->family), tape->library);

    rc = execute_and_commit_or_rollback(handle->dh_conn, request, &res,
                                        PGRES_COMMAND_OK);
    g_string_free(request, true);
    if (rc)
        return rc;

    return check_orphan(handle, tape);
}

static int get_copy_from_dss(struct dss_handle *handle, const char *uuid,
                             int version, const char *copy_name,
                             struct copy_info **copy)
{
    struct copy_info *copy_list = NULL;
    struct dss_filter filter;
    int copy_cnt = 0;
    int rc = 0;

    rc = dss_filter_build(&filter,
                          "{\"$AND\": ["
                          "     {\"DSS::COPY::object_uuid\": \"%s\"},"
                          "     {\"DSS::COPY::version\": \"%d\"},"
                          "     {\"DSS::COPY::copy_name\": \"%s\"}"
                          "]}", uuid, version, copy_name);
    if (rc)
        LOG_RETURN(rc, "Cannot build filter");

    rc = dss_copy_get(handle, &filter, &copy_list, &copy_cnt, NULL);
    dss_filter_free(&filter);
    if (rc)
        LOG_RETURN(rc, "Cannot fetch copy '%s' for objuuid:'%s'",
                   copy_name, uuid);

    if (copy_cnt == 0) {
        dss_res_free(copy_list, copy_cnt);
        return -ENOENT;
    }

    *copy = copy_info_dup(copy_list);

    dss_res_free(copy_list, copy_cnt);

    return 0;
}

int dss_lazy_find_copy(struct dss_handle *handle, const char *uuid,
                       int version, const char *copy_name,
                       struct copy_info **copy)
{
    struct copy_info *copy_list = NULL;
    const char *default_copy = NULL;
    char **preferred_order = NULL;
    struct dss_filter filter;
    int copy_cnt = 0;
    size_t count = 0;
    int rc = 0;
    int i;

    ENTRY;

    if (copy_name) {
        rc = get_copy_from_dss(handle, uuid, version, copy_name, copy);
        if (rc == 0 && *copy == NULL)
            pho_error(rc = -ENOENT,
                      "Cannot fetch copy '%s'", copy_name);
        return rc;
    }

    rc = get_cfg_preferred_order(&preferred_order, &count);
    if (rc)
        return rc;

    for (i = 0; i < count; ++i) {
        rc = get_copy_from_dss(handle, uuid, version, preferred_order[i], copy);
        if (*copy != NULL && rc == 0)
            goto end;
    }

    rc = get_cfg_default_copy_name(&default_copy);
    if (rc)
        LOG_RETURN(rc, "Cannot get default copy name from conf");

    rc = get_copy_from_dss(handle, uuid, version, default_copy, copy);
    if (*copy != NULL && rc == 0)
        goto end;

    rc = dss_filter_build(&filter,
                          "{\"$AND\": ["
                          "     {\"DSS::COPY::object_uuid\": \"%s\"},"
                          "     {\"DSS::COPY::version\": \"%d\"}"
                          "]}", uuid, version);
    if (rc)
        LOG_RETURN(rc, "Cannot build filter");

    rc = dss_copy_get(handle, &filter, &copy_list, &copy_cnt, NULL);
    dss_filter_free(&filter);
    if (rc)
        LOG_RETURN(rc, "Cannot fetch copy for objuuid:'%s'", uuid);

    assert(copy_cnt >= 1);

    *copy = copy_info_dup(&copy_list[0]);

    dss_res_free(copy_list, copy_cnt);

end:
    for (i = 0; i < count; ++i)
        free(preferred_order[i]);
    free(preferred_order);

    return rc;
}

int dss_get_copy_from_object(struct dss_handle *handle,
                             struct copy_info **copy_list, int *copy_cnt,
                             const struct dss_filter *filter,
                             enum dss_obj_scope scope)
{
    GString *request = g_string_new("BEGIN;");
    GString *union_req = g_string_new(NULL);
    GString *clause = g_string_new(NULL);
    int rc = 0;

    g_string_append_printf(union_req, "(%s %s %s)",
            (scope == DSS_OBJ_ALIVE || scope == DSS_OBJ_ALL) ?
                "SELECT object_uuid, version, oid FROM object" : "",
            scope == DSS_OBJ_ALL ? "UNION" : "",
            (scope == DSS_OBJ_ALL || scope == DSS_OBJ_DEPRECATED) ?
                "SELECT object_uuid, version, oid FROM deprecated_object" : "");

    if (filter) {
        rc = clause_filter_convert(handle, clause, filter);
        if (rc)
            goto out;
    }

    g_string_append_printf(request,
        "SELECT object_uuid, version, copy_name, copy_status, creation_time, "
        "access_time FROM copy INNER JOIN %s as inner_table "
        "USING (object_uuid, version) %s;",
        union_req->str, clause->str != NULL ? clause->str : "");

    rc = dss_execute_generic_get(handle, DSS_COPY, request, (void **) copy_list,
                                 copy_cnt);

out:
    g_string_free(union_req, true);
    g_string_free(request, true);
    g_string_free(clause, true);

    return rc;
}

int dss_get_living_and_deprecated_objects(struct dss_handle *handle,
                                          const struct dss_filter *filter,
                                          struct dss_sort *sort,
                                          struct object_info **objs,
                                          int *n_objs)
{
    GString *request = g_string_new("BEGIN;");
    GString *clause = g_string_new(NULL);
    int rc = 0;

    if (filter) {
        rc = clause_filter_convert(handle, clause, filter);
        if (rc)
            goto out;
    }

    g_string_append_printf(request,
        "SELECT oid, object_uuid, version, user_md, creation_time, _grouping, "
        "deprec_time FROM deprecated_object UNION "
        "SELECT oid, object_uuid, version, user_md, creation_time, _grouping, "
        "Null FROM object %s",
        clause->str != NULL ? clause->str : "");

    dss_sort2sql(request, sort);
    g_string_append(request, ";");

    rc = dss_execute_generic_get(handle, DSS_DEPREC, request, (void **) objs,
                                 n_objs);

out:
    g_string_free(request, true);
    g_string_free(clause, true);

    return rc;
}
