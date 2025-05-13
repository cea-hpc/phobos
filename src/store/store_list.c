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
 * \brief  Phobos Object Store implementation of non data transfer API calls
 */

#include "phobos_store.h"
#include "pho_cfg.h"
#include "pho_dss.h"
#include "pho_dss_wrapper.h"

#include <glib.h>

/**
 * Construct the metadata string for the object list filter.
 *
 * The caller must ensure metadata_str is initialized before calling.
 *
 * \param[in,out]   metadata_str    Empty metadata string.
 * \param[in]       metadata        Metadata filter.
 * \param[in]       n_metadata      Number of requested metadata.
 */
static void phobos_construct_metadata(GString *metadata_str,
                                      const char **metadata, int n_metadata)
{
    int i;

    for (i = 0; i < n_metadata; ++i)
        g_string_append_printf(metadata_str,
                               "{\"$KVINJSON\": "
                                   "  {\"DSS::OBJ::user_md\": \"%s\"}}%s",
                               metadata[i],
                               (i + 1 != n_metadata) ? "," : "");
}

/**
 * Construct the resource string for the object list filter.
 *
 * The caller must ensure res_str is initialized before calling.
 *
 * \param[in,out]   res_str     Empty resource string.
 * \param[in]       res         Resource filter.
 * \param[in]       n_res       Number of requested ressources.
 * \param[in]       is_pattern  True if search done using POSIX pattern.
 */
static void phobos_construct_res(GString *res_str, const char **res,
                                 int n_res, bool is_pattern)
{
    char *res_prefix = (is_pattern ? "{\"$REGEXP\" : " : "");
    char *res_suffix = (is_pattern ? "}" : "");
    int i;

    g_string_append_printf(res_str, "{\"$OR\" : [");

    for (i = 0; i < n_res; ++i)
        g_string_append_printf(res_str,
                               "%s {\"DSS::OBJ::oid\":\"%s\"} %s %s",
                               res_prefix,
                               res[i],
                               res_suffix,
                               (i + 1 != n_res) ? "," : "");

    g_string_append_printf(res_str, "]}");
}

/**
 * Construct the status string for the copy list filter.
 *
 * \param[in,out]   status_str      Empty status string.
 * \param[in]       status_filter   Status filter number.
 *
 * The status_filter number binary representation indicates which status
 * to filter.
 */
static void phobos_construct_status(GString *status_str, int status_filter)
{
    /**
     * The status_filter can be represented in binary as `cri` where c, r, i
     * are either 0 or 1. If one field is 1, the filter must incorporate the
     * corresponding status (c <-> complete, r <-> readable, i <-> incomplete)
     */
    bool i = (status_filter & DSS_STATUS_FILTER_INCOMPLETE);
    bool r = (status_filter & DSS_STATUS_FILTER_READABLE)/2;
    bool c = (status_filter & DSS_STATUS_FILTER_COMPLETE)/4;

    g_string_append_printf(status_str, "{\"$OR\" : [");

    g_string_append_printf(status_str, "%s %s",
                           i ? " {\"DSS::COPY::copy_status\":\"incomplete\"} "
                             : "",
                           i && (r || c) ? "," : "");
    g_string_append_printf(status_str, "%s %s",
                           r ? " {\"DSS::COPY::copy_status\":\"readable\"} "
                             : "",
                           r && c ? "," : "");
    g_string_append_printf(status_str, "%s",
                           c ? " {\"DSS::COPY::copy_status\":\"complete\"} " :
                               "");
    g_string_append_printf(status_str, "]}");
}

static int phobos_construct_obj_filter(struct pho_list_filters *filters,
                                       char **filter)
{
    GString *filter_str = NULL;
    GString *metadata_str;
    GString *res_str;
    int count = 0;
    int rc = 0;

    metadata_str = g_string_new(NULL);
    res_str = g_string_new(NULL);

    count += filters->n_metadata ? 1 : 0;
    count += filters->version ? 1 : 0;
    count += filters->n_res ? 1 : 0;
    count += filters->uuid ? 1 : 0;

    if (count > 0)
        filter_str = g_string_new("{\"$AND\" : [");
    /**
     * If there are at least one metadata, we construct a string containing
     * each metadata.
     */
    if (filters->n_metadata) {
        phobos_construct_metadata(metadata_str, filters->metadata,
                                  filters->n_metadata);
        g_string_append_printf(filter_str, "%s %s", metadata_str->str,
                               --count != 0 ? "," : "");
    }

    /**
     * If there are at least one resource, we construct a string containing
     * each request.
     */
    if (filters->n_res) {
        phobos_construct_res(res_str, filters->res, filters->n_res,
                             filters->is_pattern);
        g_string_append_printf(filter_str, "%s %s", res_str->str,
                               --count != 0 ? "," : "");
    }

    if (filters->version)
        g_string_append_printf(filter_str,
                               "{\"DSS::OBJ::version\": \"%d\"} %s",
                               filters->version, --count != 0 ? "," : "");

    if (filters->uuid)
        g_string_append_printf(filter_str,
                               "{\"DSS::OBJ::uuid\": \"%s\"} %s",
                               filters->uuid, --count != 0 ? "," : "");

    if (filter_str != NULL) {
        g_string_append(filter_str, "]}");
        *filter = xstrdup(filter_str->str);
        g_string_free(filter_str, TRUE);
    }

    g_string_free(metadata_str, TRUE);
    g_string_free(res_str, TRUE);

    return rc;
}

int phobos_store_object_list(struct pho_list_filters *filters,
                             enum dss_obj_scope scope,
                             struct object_info **objs, int *n_objs,
                             struct dss_sort *sort)
{
    struct dss_filter *filter_ptr = NULL;
    char *json_filter = NULL;
    struct dss_filter filter;
    struct dss_handle dss;
    int rc = 0;

    rc = pho_cfg_init_local(NULL);
    if (rc && rc != -EALREADY)
        return rc;

    rc = dss_init(&dss);
    if (rc != 0)
        return rc;

    rc = phobos_construct_obj_filter(filters,  &json_filter);
    if (rc)
        LOG_RETURN(rc, "Failed to build the object filter");

    if (json_filter != NULL) {
        rc = dss_filter_build(&filter, "%s", json_filter);
        if (rc)
            GOTO(err, rc);
        filter_ptr = &filter;
        free(json_filter);
    }

    switch (scope) {
    case DSS_OBJ_ALIVE:
        rc = dss_object_get(&dss, filter_ptr, objs, n_objs, sort);
        break;
    case DSS_OBJ_DEPRECATED:
        rc = dss_deprecated_object_get(&dss, filter_ptr, objs, n_objs, sort);
        break;
    case DSS_OBJ_ALL:
        rc = dss_get_living_and_deprecated_objects(&dss, filter_ptr, sort, objs,
                                                   n_objs);
        break;
    }

    if (rc)
        pho_error(rc, "Cannot fetch objects");

    dss_filter_free(filter_ptr);

err:
    dss_fini(&dss);

    return rc;
}

void phobos_store_object_list_free(struct object_info *objs, int n_objs)
{
    dss_res_free(objs, n_objs);
}

int phobos_store_copy_list(struct pho_list_filters *filters,
                           enum dss_obj_scope scope,
                           struct copy_info **copy, int *n_copy,
                           struct dss_sort *sort)
{
    struct dss_filter *filter_ptr = NULL;
    struct dss_filter filter;
    struct dss_handle dss;
    GString *filter_str;
    GString *status_str;
    GString *res_str;
    int count = 0;
    int rc;

    if (filters->status_filter <= 0 ||
        filters->status_filter > DSS_STATUS_FILTER_ALL)
        LOG_RETURN(-EINVAL,
                   "status_filter must be an integer between %d and %d",
                   DSS_STATUS_FILTER_INCOMPLETE, DSS_STATUS_FILTER_ALL);

    rc = pho_cfg_init_local(NULL);
    if (rc && rc != -EALREADY)
        return rc;

    rc = dss_init(&dss);
    if (rc != 0)
        return rc;

    filter_str = g_string_new("{\"$AND\" : [");
    status_str = g_string_new(NULL);
    res_str = g_string_new(NULL);

    count += (filters->status_filter != DSS_STATUS_FILTER_ALL ? 1 : 0);
    count += (filters->copy_name ? 1 : 0);
    count += (filters->version ? 1 : 0);
    count += (filters->n_res ? 1 : 0);
    count += (filters->uuid ? 1 : 0);

    /**
     * If there are at least one resource, we construct a string containing
     * each request.
     */
    if (filters->n_res) {
        phobos_construct_res(res_str, filters->res, filters->n_res, false);
        g_string_append_printf(filter_str, "%s %s", res_str->str,
                               --count != 0 ? "," : "");
    }

    /**
     * No need to construct the status filter if all copy_status are wanted,
     * which happens if status_filter number is set to
     * DSS_STATUS_FILTER_ALL(= 111 in binary)
     */
    if (filters->status_filter != DSS_STATUS_FILTER_ALL) {
        phobos_construct_status(status_str, filters->status_filter);
        g_string_append_printf(filter_str, "%s %s", status_str->str,
                               --count != 0 ? "," : "");
    }

    if (filters->uuid)
        g_string_append_printf(filter_str,
                               "{\"DSS::COPY::object_uuid\": \"%s\"} %s",
                               filters->uuid, --count != 0 ? "," : "");

    if (filters->copy_name)
        g_string_append_printf(filter_str,
                               "{\"DSS::COPY::copy_name\": \"%s\"} %s",
                               filters->copy_name, --count != 0 ? "," : "");
    if (filters->version)
        g_string_append_printf(filter_str,
                               "{\"DSS::COPY::version\": \"%d\"} %s",
                               filters->version, --count != 0 ? "," : "");

    g_string_append(filter_str, "]}");

    if (filters->n_res || filters->status_filter != DSS_STATUS_FILTER_ALL ||
        filters->copy_name || filters->version || filters->uuid) {

        rc = dss_filter_build(&filter, "%s", filter_str->str);
        if (rc)
            GOTO(err, rc);

        filter_ptr = &filter;
    }

    rc = dss_get_copy_from_object(&dss, copy, n_copy, filter_ptr, scope);
    if (rc)
        pho_error(rc, "Cannot fetch copies");

    dss_filter_free(filter_ptr);

err:
    g_string_free(filter_str, true);
    g_string_free(status_str, true);
    g_string_free(res_str, true);
    dss_fini(&dss);

    return rc;
}

void phobos_store_copy_list_free(struct copy_info *copy, int n_copy)
{
    dss_res_free(copy, n_copy);
}
