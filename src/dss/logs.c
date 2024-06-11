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
 * \brief  Object resource file of Phobos's Distributed State Service.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <jansson.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>

#include <libpq-fe.h>

#include "dss_utils.h"
#include "pho_common.h"
#include "pho_dss.h"
#include "pho_type_utils.h"

#include "logs.h"

static int logs_insert_query(PGconn *conn, void *void_log, int item_cnt,
                             int64_t fields, GString *request)
{
    unsigned int escape_len;
    char *escape_string;
    char *message;
    int rc;

    (void) fields;

    g_string_append_printf(
        request,
        "INSERT INTO logs (family, device, medium, library, errno, cause,"
        "                  message)"
        " VALUES "
    );

    for (int i = 0; i < item_cnt; ++i) {
        struct pho_log *log = ((struct pho_log *) void_log) + i;

        message = json_dumps(log->message, 0);
        if (!message)
            LOG_RETURN(rc = -ENOMEM, "Failed to dump log message as json");

        escape_len = strlen(message) * 2 + 1;
        escape_string = xmalloc(escape_len);

        PQescapeStringConn(conn, escape_string, message, escape_len, NULL);

        g_string_append_printf(
            request,
            "('%s', '%s', '%s', '%s', %d, '%s', '%s')",
            rsc_family2str(log->device.family), log->device.name,
            log->medium.name, log->device.library, log->error_number,
            operation_type2str(log->cause), escape_string
        );

        free(message);
        free(escape_string);

        if (i < item_cnt - 1)
            g_string_append(request, ", ");
    }

    g_string_append(request, ";");

    return 0;
}

static int logs_select_query(GString **conditions, int n_conditions,
                             GString *request)
{
    g_string_append(request,
                    "SELECT family, device, medium, library, errno, cause,"
                    "       message, time"
                    " FROM logs");

    if (n_conditions == 1)
        g_string_append(request, conditions[0]->str);
    else if (n_conditions >= 2)
        return -ENOTSUP;

    g_string_append(request, ";");

    return 0;
}

static int logs_delete_query(void *void_log_filter, int item_cnt,
                             GString *request)
{
    /* The delete can be conditionned on multiple fields which are only known
     * at run-time, so instead of a taking a specific filter to delete here, we
     * take a GString representing the conditions to use.
     */
    GString *conditions = void_log_filter;

    (void) item_cnt;

    g_string_append(request, "DELETE FROM logs");

    if (conditions != NULL)
        g_string_append(request, conditions->str);

    g_string_append(request, ";");

    return 0;
}

static int logs_from_pg_row(struct dss_handle *handle, void *item,
                            PGresult *res, int row_num)
{
    struct pho_log *log = item;
    int rc = 0;

    (void)handle;

    log->device.family = str2rsc_family(PQgetvalue(res, row_num, 0));
    log->medium.family = log->device.family;
    pho_id_name_set(&log->device, PQgetvalue(res, row_num, 1),
                    PQgetvalue(res, row_num, 3));
    pho_id_name_set(&log->medium, PQgetvalue(res, row_num, 2),
                    PQgetvalue(res, row_num, 3));
    log->error_number = atoi(PQgetvalue(res, row_num, 4));
    log->cause = str2operation_type(PQgetvalue(res, row_num, 5));
    log->message = json_loads(get_str_value(res, row_num, 6), 0, NULL);
    if (log->message == NULL)
        LOG_RETURN(rc = -ENOMEM, "Failed to convert message in log to json");

    return str2timeval(get_str_value(res, row_num, 7), &log->time);
}

static void logs_result_free(void *item)
{
    struct pho_log *log = item;

    destroy_log_message(log);
}

const struct dss_resource_ops logs_ops = {
    .insert_query = logs_insert_query,
    .update_query = NULL,
    .select_query = logs_select_query,
    .delete_query = logs_delete_query,
    .create       = logs_from_pg_row,
    .free         = logs_result_free,
    .size         = sizeof(struct pho_log),
};

int create_logs_filter(struct pho_log_filter *log_filter,
                       struct dss_filter **dss_log_filter)
{
    int remaining_criteria = 0;
    GString *filter_str;
    int rc;

    if (log_filter == NULL) {
        *dss_log_filter = NULL;
        return 0;
    }

    /* Check if multiple conditions are demanded */
    if (log_filter->device.family != PHO_RSC_NONE)
        remaining_criteria++;
    if (log_filter->medium.family != PHO_RSC_NONE)
        remaining_criteria++;
    if (log_filter->error_number != NULL)
        remaining_criteria++;
    if (log_filter->cause != PHO_OPERATION_INVALID)
        remaining_criteria++;
    if (log_filter->start.tv_sec != 0)
        remaining_criteria++;
    if (log_filter->end.tv_sec != 0)
        remaining_criteria++;

    if (remaining_criteria == 0) {
        *dss_log_filter = NULL;
        return 0;
    }

    filter_str = g_string_new(NULL);
    g_string_append_printf(filter_str, "{\"$AND\": [");

    if (log_filter->device.family != PHO_RSC_NONE ||
        log_filter->medium.family != PHO_RSC_NONE) {
        const char *family = log_filter->device.family != PHO_RSC_NONE ?
                             rsc_family2str(log_filter->device.family) :
                             rsc_family2str(log_filter->medium.family);

        g_string_append_printf(filter_str,
                               "{\"DSS::LOG::family\": \"%s\"},",
                               family);
    }

    if (log_filter->device.family != PHO_RSC_NONE) {
        remaining_criteria--;
        g_string_append_printf(filter_str,
                               "{\"DSS::LOG::device\": \"%s\"}%s",
                               log_filter->device.name,
                               remaining_criteria ? "," : "");
    }

    if (log_filter->medium.family != PHO_RSC_NONE) {
        remaining_criteria--;
        g_string_append_printf(filter_str,
                               "{\"DSS::LOG::medium\": \"%s\"}%s",
                               log_filter->medium.name,
                               remaining_criteria ? "," : "");
    }

    if (log_filter->error_number != NULL) {
        remaining_criteria--;
        g_string_append_printf(filter_str,
                               "{\"DSS::LOG::errno\": \"%d\"}%s",
                               *log_filter->error_number,
                               remaining_criteria ? "," : "");
    }

    if (log_filter->cause != -1) {
        remaining_criteria--;
        g_string_append_printf(filter_str,
                               "{\"DSS::LOG::cause\": \"%s\"}%s",
                               operation_type2str(log_filter->cause),
                               remaining_criteria ? "," : "");
    }

    if (log_filter->start.tv_sec != 0) {
        char time_str[32];

        remaining_criteria--;
        timeval2str(&log_filter->start, time_str);

        g_string_append_printf(filter_str,
                               "{\"$GTE\": {\"DSS::LOG::start\": \"%s\"}}%s",
                               time_str, remaining_criteria ? "," : "");
    }

    if (log_filter->end.tv_sec != 0) {
        char time_str[32];

        remaining_criteria--;
        timeval2str(&log_filter->end, time_str);

        g_string_append_printf(filter_str,
                               "{\"$LTE\": {\"DSS::LOG::end\": \"%s\"}}%s",
                               time_str, remaining_criteria ? "," : "");
    }

    g_string_append_printf(filter_str, "]}");
    rc = dss_filter_build(*dss_log_filter, "%s", filter_str->str);
    g_string_free(filter_str, true);
    return rc;
}

/* Return a string representation of \p log.
 *
 * \param[in]  log   Log to convert to string
 *
 * \return     Pointer to the string representation, must be passed to free(3)
 *             Return NULL on allocation failure
 */
static const char *pho_log2str(struct pho_log *log)
{
    const char *message;
    char *repr;
    int rc;

    message = json_dumps(log->message, 0);

    rc = asprintf(&repr, "%s: '%s', '%s' (rc=%d): %s: %s",
                  operation_type2str(log->cause),
                  log->device.name,
                  log->medium.name,
                  log->error_number,
                  strerror(log->error_number),
                  message);
    if (rc == -1)
        return NULL;

    free((void *) message);

    return repr;
}

void emit_log_after_action(struct dss_handle *dss,
                           struct pho_log *log,
                           enum operation_type action,
                           int rc)
{
    log->error_number = rc;
    if (rc) {
        if (log->message && json_object_size(log->message) != 0 &&
            action != log->cause) {
            json_t *message = json_object();

            /* Add context only if operation != from intented action to
             * avoid redundant data.
             */
            json_object_set_new(message, operation_type2str(action),
                                log->message);
            log->message = message;
        }
    }

    if (should_log(log, action)) {
        GString *request;
        int rc2;

        request = g_string_new("BEGIN;");

        logs_insert_query(dss->dh_conn, log, 1, 0, request);
        rc2 = execute_and_commit_or_rollback(dss->dh_conn, request, NULL,
                                             PGRES_COMMAND_OK);

        if (rc2) {
            const char *log_str = pho_log2str(log);

            pho_error(rc2, "Failed to emit log: %s", log_str);
            free((void *) log_str);
        }
        /* Ignore emit errors */
    }

    if (log->message)
        json_decref(log->message);
}

static ssize_t count_health(struct pho_log *logs, size_t count,
                            size_t max_health)
{
    ssize_t health = max_health;
    size_t i = 0;

    if (count == 0)
        /* no logs yet, new medium */
        return max_health;

    /* skip successes before first error, they should not count in the health */
    while (i < count && !logs[i].error_number)
        i++;

    if (i == count)
        /* no errors */
        return max_health;

    for (; i < count; i++) {
        if (logs[i].error_number)
            health--;
        else
            health++;

        health = clamp(health, 0, max_health);
    }

    return health;
}

int dss_resource_health(struct dss_handle *dss,
                        const struct pho_id *medium_id,
                        enum dss_type resource, size_t max_health,
                        size_t *health)
{
    struct pho_log_filter log_filter = {0};
    struct dss_filter *pfilter;
    struct dss_filter filter;
    struct pho_log *logs;
    int count;
    int rc;

    pfilter = &filter;
    switch (resource) {
    case DSS_MEDIA:
        log_filter.device.family = PHO_RSC_NONE;
        pho_id_copy(&log_filter.medium, medium_id);
        break;
    case DSS_DEVICE:
        log_filter.medium.family = PHO_RSC_NONE;
        pho_id_copy(&log_filter.device, medium_id);
        break;
    default:
        LOG_RETURN(-EINVAL, "Ressource type %s does not have a health counter",
                   dss_type2str(resource));
    }

    log_filter.cause = PHO_OPERATION_INVALID;

    rc = create_logs_filter(&log_filter, &pfilter);
    if (rc)
        return rc;

    rc = dss_logs_get(dss, &filter, &logs, &count);
    dss_filter_free(&filter);
    if (rc)
        return rc;

    *health = count_health(logs, count, max_health);
    dss_res_free(logs, count);

    return 0;
}
