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

#include "dss_utils.h"
#include "pho_common.h"
#include "pho_dss.h"
#include "pho_type_utils.h"

enum log_query_idx {
    DSS_EMIT_LOG,
};

static const char * const log_query[] = {
    [DSS_EMIT_LOG] = "INSERT INTO logs "
                     "(family, device, medium, errno, cause, message) "
                     "VALUES ('%s', '%s', '%s', %d, '%s', '%s');",
};

int dss_emit_log(struct dss_handle *handle, struct pho_log *log)
{
    GString *request = g_string_new("");
    PGconn *conn = handle->dh_conn;
    char *message;
    PGresult *res;
    int rc;

    message = json_dumps(log->message, 0);
    if (!message)
        LOG_GOTO(free_request, rc = -ENOMEM,
                 "Failed to dump log message as json");

    g_string_printf(request, log_query[DSS_EMIT_LOG],
                    rsc_family2str(log->device.family), log->device.name,
                    log->medium.name, log->error_number,
                    operation_type2str(log->cause), message);
    free(message);

    rc = execute(conn, request, &res, PGRES_COMMAND_OK);
    PQclear(res);

free_request:
    g_string_free(request, true);

    return rc;
}
