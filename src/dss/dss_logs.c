/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2023 CEA/DAM.
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
 * \brief  Phobos Distributed State Service API for logging.
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

#include "dss_logs.h"
#include "dss_utils.h"
#include "pho_common.h"
#include "pho_dss.h"
#include "pho_type_utils.h"

int dss_emit_log(struct dss_handle *handle, struct pho_log *log)
{
    GString *request = g_string_new("");
    PGconn *conn = handle->dh_conn;
    unsigned int escape_len;
    char *escape_string;
    char *message;
    PGresult *res;
    int rc;

    message = json_dumps(log->message, 0);
    if (!message)
        LOG_GOTO(free_request, rc = -ENOMEM,
                 "Failed to dump log message as json");

    escape_len = strlen(message) * 2 + 1;
    escape_string = xmalloc(escape_len);

    PQescapeStringConn(conn, escape_string, message, escape_len, NULL);

    g_string_printf(request, log_query[DSS_EMIT_LOG],
                    rsc_family2str(log->device.family), log->device.name,
                    log->medium.name, log->error_number,
                    operation_type2str(log->cause), escape_string);
    free(message);
    free(escape_string);

    rc = execute(conn, request->str, &res, PGRES_COMMAND_OK);
    PQclear(res);

free_request:
    g_string_free(request, true);

    return rc;
}

int dss_logs_from_pg_row(struct dss_handle *handle, void *item,
                         PGresult *res, int row_num)
{
    struct pho_log *log = item;
    int rc = 0;

    (void)handle;

    log->device.family = str2rsc_family(PQgetvalue(res, row_num, 0));
    log->medium.family = log->device.family;
    pho_id_name_set(&log->device, PQgetvalue(res, row_num, 1));
    pho_id_name_set(&log->medium, PQgetvalue(res, row_num, 2));
    log->error_number = atoi(PQgetvalue(res, row_num, 3));
    log->cause = str2operation_type(PQgetvalue(res, row_num, 4));
    log->message = json_loads(get_str_value(res, row_num, 5), 0, NULL);
    if (log->message == NULL)
        LOG_RETURN(rc = -ENOMEM, "Failed to convert message in log to json");

    return str2timeval(get_str_value(res, row_num, 6), &log->time);
}

void dss_logs_result_free(void *item)
{
    struct pho_log *log = item;

    destroy_log_message(log);
}

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
    if (log_filter->cause != -1)
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
