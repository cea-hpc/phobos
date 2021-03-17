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
#include "pho_type_utils.h"

enum lock_query_idx {
    DSS_LOCK_QUERY,
    DSS_UNLOCK_QUERY,
    DSS_UNLOCK_FORCE_QUERY,
    DSS_STATUS_QUERY,
};

static const char * const lock_query[] = {
    [DSS_LOCK_QUERY]         = "INSERT INTO lock (id, owner)"
                               " VALUES ('%s', '%s');",
    [DSS_UNLOCK_QUERY]       = "DO $$"
                               " DECLARE lock_id TEXT:= '%s';"
                               "         lock_owner TEXT:="
                               "             (SELECT owner FROM lock"
                               "              WHERE id = lock_id);"
                               " BEGIN"
                               " IF lock_owner IS NULL THEN"
                               "  RAISE USING errcode = 'PHLK1';"
                               " END IF;"
                               " IF lock_owner <> '%s' THEN"
                               "  RAISE USING errcode = 'PHLK2';"
                               " END IF;"
                               " DELETE FROM lock"
                               "  WHERE id = lock_id AND owner = lock_owner;"
                               "END $$;",
    [DSS_UNLOCK_FORCE_QUERY] = "DO $$"
                               " DECLARE lock_id TEXT:= '%s';"
                               "         owner TEXT:= (SELECT owner FROM lock"
                               "                       WHERE id = lock_id);"
                               " BEGIN"
                               " IF owner IS NULL THEN"
                               "  RAISE USING errcode = 'PHLK1';"
                               " END IF;"
                               " DELETE FROM lock WHERE id = lock_id;"
                               "END $$;",
    [DSS_STATUS_QUERY]       = "SELECT id, owner, timestamp FROM lock "
                               " WHERE id = '%s';"
};

static int execute(PGconn *conn, GString *request, PGresult **res,
                   ExecStatusType tested)
{
    pho_debug("Executing request: '%s'", request->str);

    *res = PQexec(conn, request->str);
    if (PQresultStatus(*res) != tested)
        LOG_RETURN(psql_state2errno(*res), "Request failed: %s",
                   PQresultErrorField(*res, PG_DIAG_MESSAGE_PRIMARY));

    return 0;
}

int dss_lock(struct dss_handle *handle, const char *lock_id,
             const char *lock_owner)
{
    GString *request = g_string_new("");
    PGconn *conn = handle->dh_conn;
    PGresult *res;
    int rc;

    g_string_printf(request, lock_query[DSS_LOCK_QUERY], lock_id, lock_owner);

    rc = execute(conn, request, &res, PGRES_COMMAND_OK);

    PQclear(res);
    g_string_free(request, true);

    return rc;
}

int dss_unlock(struct dss_handle *handle, const char *lock_id,
               const char *lock_owner)
{
    GString *request = g_string_new("");
    PGconn *conn = handle->dh_conn;
    PGresult *res;
    int rc;

    if (lock_owner != NULL)
        g_string_printf(request, lock_query[DSS_UNLOCK_QUERY], lock_id,
                        lock_owner);
    else
        g_string_printf(request, lock_query[DSS_UNLOCK_FORCE_QUERY], lock_id);

    rc = execute(conn, request, &res, PGRES_COMMAND_OK);

    PQclear(res);
    g_string_free(request, true);

    return rc;
}

int dss_lock_status(struct dss_handle *handle, const char *lock_id,
                    char **lock_owner, struct timeval *lock_timestamp)
{
    GString *request = g_string_new("");
    PGconn *conn = handle->dh_conn;
    PGresult *res;
    int rc = 0;

    g_string_printf(request, lock_query[DSS_STATUS_QUERY], lock_id);

    rc = execute(conn, request, &res, PGRES_TUPLES_OK);
    if (rc)
        goto out_cleanup;

    if (PQntuples(res) == 0)
        LOG_GOTO(out_cleanup, rc = -ENOLCK, "No row found after request: %s",
                 PQresultErrorField(res, PG_DIAG_MESSAGE_PRIMARY));

    if (lock_owner != NULL) {
        *lock_owner = strdup(PQgetvalue(res, 0, 1));
        if (*lock_owner == NULL) {
            rc = -ENOMEM;
            goto out_cleanup;
        }
    }

    if (lock_timestamp != NULL)
        str2timeval(PQgetvalue(res, 0, 2), lock_timestamp);

out_cleanup:
    PQclear(res);
    g_string_free(request, true);

    return rc;
}
