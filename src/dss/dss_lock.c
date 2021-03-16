/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2021 CEA/DAM.
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
 * \brief  Phobos Distributed State Service API for the generic lock.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "pho_dss.h"

#include <libpq-fe.h>

#include "dss_utils.h"
#include "pho_common.h"

enum lock_query_idx {
    DSS_LOCK_QUERY,
};

static const char * const lock_query[] = {
    [DSS_LOCK_QUERY] = "INSERT INTO lock (id, owner) VALUES ('%s', '%s');",
};

int dss_lock(struct dss_handle *handle, const char *lock_id,
             const char *lock_owner)
{
    GString *request = g_string_new("");
    PGconn *conn = handle->dh_conn;
    PGresult *res;
    int rc = 0;

    g_string_printf(request, lock_query[DSS_LOCK_QUERY], lock_id, lock_owner);

    pho_debug("Executing request: '%s'", request->str);

    res = PQexec(conn, request->str);
    if (PQresultStatus(res) != PGRES_COMMAND_OK)
        LOG_GOTO(cleanup, rc = psql_state2errno(res), "Request failed: %s",
                 PQresultErrorField(res, PG_DIAG_MESSAGE_PRIMARY));

cleanup:
    PQclear(res);
    g_string_free(request, true);
    return rc;
}
