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

    rc = dss_device_get(hdl, &filter, dev_ls, dev_cnt);
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

    build_object_json_filter(&json_filter, oid, uuid, version);

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
                           "  lyt_info, obj_status)"
                           " INSERT INTO deprecated_object"
                           "  (oid, object_uuid, version, user_md,"
                           "   lyt_info, obj_status)"
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
                           "  version, user_md, lyt_info, obj_status)"
                           " INSERT INTO object (oid, object_uuid, "
                           "  version, user_md, lyt_info, obj_status)"
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


