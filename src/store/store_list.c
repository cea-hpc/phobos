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

int phobos_store_object_list(const char **res, int n_res, bool is_pattern,
                             const char **metadata, int n_metadata,
                             bool deprecated, struct object_info **objs,
                             int *n_objs, struct dss_sort *sort)
{
    struct dss_filter *filter_ptr = NULL;
    struct dss_filter filter;
    struct dss_handle dss;
    GString *metadata_str;
    GString *res_str;
    int rc;

    rc = pho_cfg_init_local(NULL);
    if (rc && rc != -EALREADY)
        return rc;

    rc = dss_init(&dss);
    if (rc != 0)
        return rc;

    metadata_str = g_string_new(NULL);
    res_str = g_string_new(NULL);

    /**
     * If there are at least one metadata, we construct a string containing
     * each metadata.
     */
    if (n_metadata)
        phobos_construct_metadata(metadata_str, metadata, n_metadata);

    /**
     * If there are at least one resource, we construct a string containing
     * each request.
     */
    if (n_res)
        phobos_construct_res(res_str, res, n_res, is_pattern);

    if (n_res || n_metadata) {
        /**
         * Finally, if the request has at least one metadata or one resource, we
         * build the filter in the following way:
         * if there is more than one metadata or exactly one metadata and
         * resource, then using an AND is necessary
         * (which correspond to the first and last "%s").
         * After that, we add to the filter the resource metadata
         * if any is present, which are the second and fourth "%s".
         * Finally, commas may be necessary depending on the number of fields
         * (metadata or resource) wanted (third "%s").
         */
        rc = dss_filter_build(&filter,
                              "%s %s %s %s %s",
                              "{\"$AND\" : [",
                              res_str->str != NULL ? res_str->str : "",
                              ((n_res > 0) && (n_metadata > 0))
                                    ? ", " : "",
                              metadata_str->str != NULL ?
                                metadata_str->str : "",
                              "]}");
        if (rc)
            GOTO(err, rc);

        filter_ptr = &filter;
    }

    if (deprecated)
        rc = dss_deprecated_object_get(&dss, filter_ptr, objs, n_objs, sort);
    else
        rc = dss_object_get(&dss, filter_ptr, objs, n_objs, sort);

    if (rc)
        pho_error(rc, "Cannot fetch objects");

    dss_filter_free(filter_ptr);

err:
    g_string_free(metadata_str, TRUE);
    g_string_free(res_str, TRUE);
    dss_fini(&dss);

    return rc;
}

void phobos_store_object_list_free(struct object_info *objs, int n_objs)
{
    dss_res_free(objs, n_objs);
}

int phobos_store_copy_list(const char **res, int n_res, char *uuid,
                           int version, char *copy_name,
                           enum dss_obj_scope scope, int status_filter,
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

    if (status_filter <= 0 || status_filter > DSS_STATUS_FILTER_ALL)
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

    count += (status_filter != DSS_STATUS_FILTER_ALL ? 1 : 0);
    count += (copy_name ? 1 : 0);
    count += (version ? 1 : 0);
    count += (n_res ? 1 : 0);
    count += (uuid ? 1 : 0);

    /**
     * If there are at least one resource, we construct a string containing
     * each request.
     */
    if (n_res) {
        phobos_construct_res(res_str, res, n_res, false);
        g_string_append_printf(filter_str, "%s %s", res_str->str,
                               --count != 0 ? "," : "");
    }

    /**
     * No need to construct the status filter if all copy_status are wanted,
     * which happens if status_filter number is set to
     * DSS_STATUS_FILTER_ALL(= 111 in binary)
     */
    if (status_filter != DSS_STATUS_FILTER_ALL) {
        phobos_construct_status(status_str, status_filter);
        g_string_append_printf(filter_str, "%s %s", status_str->str,
                               --count != 0 ? "," : "");
    }

    if (uuid)
        g_string_append_printf(filter_str,
                               "{\"DSS::COPY::object_uuid\": \"%s\"} %s",
                               uuid, --count != 0 ? "," : "");

    if (copy_name)
        g_string_append_printf(filter_str,
                               "{\"DSS::COPY::copy_name\": \"%s\"} %s",
                               copy_name, --count != 0 ? "," : "");
    if (version)
        g_string_append_printf(filter_str,
                               "{\"DSS::COPY::version\": \"%d\"} %s",
                               version, --count != 0 ? "," : "");

    g_string_append(filter_str, "]}");

    if (n_res || status_filter != DSS_STATUS_FILTER_ALL ||
        copy_name || version || uuid) {

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
