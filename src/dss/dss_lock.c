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

#include <errno.h>
#include <unistd.h>
#include <sys/syscall.h>

#include <libpq-fe.h>

#include "dss_utils.h"
#include "pho_common.h"
#include "pho_dss.h"
#include "pho_type_utils.h"

#define LOCK_ID_LIST_ALLOCATE(_ids, _item_cnt)        \
do {                                                  \
    (_ids) = malloc((_item_cnt) * sizeof(*(_ids)));   \
    if (!(_ids))                                      \
        LOG_RETURN(-ENOMEM, "Couldn't allocate ids"); \
                                                      \
    for (i = 0; i < (_item_cnt); ++i)                 \
        (_ids)[i] = g_string_new("");                 \
} while (0)

#define LOCK_ID_LIST_FREE(_ids, _item_cnt)            \
do {                                                  \
    for (i = 0; i < (_item_cnt); ++i)                 \
        g_string_free((_ids)[i], true);               \
    free((_ids));                                     \
} while (0)                                           \

enum lock_query_idx {
    DSS_LOCK_QUERY,
    DSS_REFRESH_QUERY,
    DSS_UNLOCK_QUERY,
    DSS_UNLOCK_FORCE_QUERY,
    DSS_STATUS_QUERY,
};

static const char * const lock_query[] = {
    [DSS_LOCK_QUERY]         = "INSERT INTO lock (id, owner)"
                               " VALUES ('%s', '%s');",
    [DSS_REFRESH_QUERY]      = "DO $$"
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
                               " UPDATE lock SET timestamp = now()"
                               " WHERE id = lock_id AND owner = lock_owner;"
                               "END $$;",
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

static const char *dss_translate(enum dss_type type, const void *item_list,
                                 int pos)
{
    switch (type) {
    case DSS_DEVICE: {
        const struct dev_info *dev_ls = item_list;

        return dev_ls[pos].rsc.id.name;
    }
    case DSS_MEDIA: {
        const struct media_info *mda_ls = item_list;

        return mda_ls[pos].rsc.id.name;
    }
    case DSS_OBJECT: {
        const struct object_info *obj_ls = item_list;

        return obj_ls[pos].oid;
    }
    default:
        return NULL;
    }

    return NULL;
}

static int dss_build_lock_id_list(PGconn *conn, const void *item_list,
                                  int item_cnt, enum dss_type type,
                                  GString **ids)
{
    char         *escape_string;
    unsigned int  escape_len;
    int           rc = 0;
    const char   *name;
    int           i;

    for (i = 0; i < item_cnt; i++) {
        name = dss_translate(type, item_list, i);
        if (!name)
            return -EINVAL;

        escape_len = strlen(name) * 2 + 1;
        escape_string = malloc(escape_len);
        if (!escape_string)
            LOG_GOTO(cleanup, rc = -ENOMEM, "Memory allocation failed");

        PQescapeStringConn(conn, escape_string, name, escape_len, NULL);
        g_string_append_printf(ids[i], "%s_%s", dss_type_names[type],
                               escape_string);

        if (ids[i]->len > PHO_DSS_MAX_LOCK_ID_LEN)
            LOG_GOTO(cleanup, rc = -EINVAL, "lock_id name too long");

cleanup:
        free(escape_string);
        if (rc)
            break;
    }

    return rc;
}

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

static __thread uint64_t lock_number;

int dss_init_lock_owner(char **lock_owner)
{
    const char *hostname;
    int rc;

    *lock_owner = NULL;

    /* get hostname */
    hostname = get_hostname();
    if (!hostname)
        LOG_RETURN(-EADDRNOTAVAIL,
                   "Unable to get hostname to generate lock_owner");

    /* generate lock_owner */
    rc = asprintf(lock_owner, "%.213s:%.8lx:%.16lx:%.16lx",
                  hostname, syscall(SYS_gettid), time(NULL), lock_number);
    if (rc == -1)
        LOG_RETURN(-ENOMEM, "Unable to generate lock_owner");

    lock_number++;
    return 0;
}

int dss_init_oid_lock_id(const char *oid, char **oid_lock_id)
{
    int rc;

    rc = asprintf(oid_lock_id, "oid.%.1024s", oid);
    if (rc == -1)
        LOG_RETURN(-ENOMEM, "Unable to generate oid_lock_id");

    return 0;
}

static int basic_lock(struct dss_handle *handle, const char *lock_id,
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

static int basic_unlock(struct dss_handle *handle, const char *lock_id,
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

int dss_lock(struct dss_handle *handle, enum dss_type type,
             const void *item_list, int item_cnt, const char *lock_owner)
{
    PGconn      *conn = handle->dh_conn;
    GString    **ids;
    int          rc = 0;
    int          i;

    ENTRY;

    LOCK_ID_LIST_ALLOCATE(ids, item_cnt);

    rc = dss_build_lock_id_list(conn, item_list, item_cnt, type, ids);
    if (rc)
        LOG_GOTO(cleanup, rc, "Ids list build failed");

    for (i = 0; i < item_cnt; ++i) {
        rc = basic_lock(handle, ids[i]->str, lock_owner);
        if (rc) {
            pho_error(rc, "Failed to lock %s", ids[i]->str);
            break;
        }
    }

    if (rc) {
        for (--i; i >= 0; --i) {
            /* If a lock failure happens, we force every unlock */
            rc = basic_unlock(handle, ids[i]->str, NULL);
            if (rc)
                pho_error(rc, "Failed to unlock %s after lock failure, "
                          "database may be corrupted", ids[i]->str);
        }
    }

cleanup:
    LOCK_ID_LIST_FREE(ids, item_cnt);

    return rc;
}

int dss_lock_refresh(struct dss_handle *handle, const char *lock_id,
                     const char *lock_owner)
{
    GString *request = g_string_new("");
    PGconn *conn = handle->dh_conn;
    PGresult *res;
    int rc = 0;

    g_string_printf(request, lock_query[DSS_REFRESH_QUERY],
                    lock_id, lock_owner);

    rc = execute(conn, request, &res, PGRES_COMMAND_OK);

    PQclear(res);
    g_string_free(request, true);

    return rc;
}

int dss_unlock(struct dss_handle *handle, enum dss_type type,
               const void *item_list, int item_cnt, const char *lock_owner)
{
    PGconn      *conn = handle->dh_conn;
    GString    **ids;
    int          rc = 0;
    int          i;

    ENTRY;

    LOCK_ID_LIST_ALLOCATE(ids, item_cnt);

    rc = dss_build_lock_id_list(conn, item_list, item_cnt, type, ids);
    if (rc)
        LOG_GOTO(cleanup, rc, "Ids list build failed");

   for (i = 0; i < item_cnt; ++i) {
        int rc2;

        rc2 = basic_unlock(handle, ids[i]->str, lock_owner);
        if (rc2) {
            rc = rc ? : rc2;
            pho_error(rc2, "Failed to unlock %s", ids[i]->str);
        }
    }

cleanup:
    LOCK_ID_LIST_FREE(ids, item_cnt);

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
        LOG_GOTO(out_cleanup, rc = -ENOLCK, "No row found after request : %s",
                 request->str);

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
