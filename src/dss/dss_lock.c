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
#include <sys/types.h>

#include <libpq-fe.h>

#include "dss_utils.h"
#include "dss_lock.h"
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
} while (0)

struct simple_param {
    struct pho_lock *locks;
    int (*basic_func)(struct dss_handle *handle, enum dss_type lock_type,
                      const char *lock_id, struct pho_lock *locks);
};

struct dual_param {
    const char *lock_hostname;
    int lock_owner;
    int (*basic_func)(struct dss_handle *handle, enum dss_type lock_type,
                      const char *lock_id, int lock_owner,
                      const char *lock_hostname);
};

/** This structure serves as an abstraction for the basic_* calls */
struct dss_generic_call {
    union {
        struct simple_param simple;
        struct dual_param dual;
    } params;
    bool is_simple;
    int (*rollback_func)(struct dss_handle *handle, enum dss_type type,
                         GString **ids, int rollback_cnt);
    bool all_or_nothing;
    const char *action;
};

enum lock_query_idx {
    DSS_LOCK_QUERY,
    DSS_REFRESH_QUERY,
    DSS_UNLOCK_QUERY,
    DSS_UNLOCK_FORCE_QUERY,
    DSS_STATUS_QUERY,
};

#define DECLARE_BLOCK " DECLARE lock_type lock_type:= '%s'::lock_type;" \
                      "         lock_id TEXT:= '%s';"                   \
                      "         lock_hostname TEXT:="                   \
                      "             (SELECT hostname FROM lock"         \
                      "              WHERE type = lock_type AND "       \
                      "                    id = lock_id);"              \
                      "         lock_owner INTEGER:="                   \
                      "             (SELECT owner FROM lock"            \
                      "              WHERE type = lock_type AND "       \
                      "                    id = lock_id);"              \

#define CHECK_VALID_OWNER_HOSTNAME " IF lock_owner IS NULL OR "         \
                                   "    lock_hostname IS NULL THEN"     \
                                   "  RAISE USING errcode = 'PHLK1';"   \
                                   " END IF;"

#define CHECK_OWNER_HOSTNAME_EXISTS " IF lock_owner <> '%d' OR "        \
                                    "    lock_hostname <> '%s' THEN"    \
                                    "  RAISE USING errcode = 'PHLK2';"  \
                                    " END IF;"

#define WHERE_CONDITION " WHERE type = lock_type AND id = lock_id AND " \
                        "       owner = lock_owner AND "                \
                        "       hostname = lock_hostname;"

static const char * const lock_query[] = {
    [DSS_LOCK_QUERY]         = "INSERT INTO lock (type, id, owner, hostname)"
                               " VALUES ('%s'::lock_type, '%s', %d, '%s');",
    [DSS_REFRESH_QUERY]      = "DO $$"
                                 DECLARE_BLOCK
                               " BEGIN"
                                 CHECK_VALID_OWNER_HOSTNAME
                                 CHECK_OWNER_HOSTNAME_EXISTS
                               " UPDATE lock SET timestamp = now()"
                                 WHERE_CONDITION
                               "END $$;",
    [DSS_UNLOCK_QUERY]       = "DO $$"
                                 DECLARE_BLOCK
                               " BEGIN"
                                 CHECK_VALID_OWNER_HOSTNAME
                                 CHECK_OWNER_HOSTNAME_EXISTS
                               " DELETE FROM lock"
                                 WHERE_CONDITION
                               "END $$;",
    [DSS_UNLOCK_FORCE_QUERY] = "DO $$"
                                  DECLARE_BLOCK
                               "  BEGIN"
                                  CHECK_VALID_OWNER_HOSTNAME
                               "  DELETE FROM lock"
                               "   WHERE type = lock_type AND id = lock_id;"
                               "END $$;",
    [DSS_STATUS_QUERY]       = "SELECT hostname, owner, timestamp FROM lock "
                               "  WHERE type = '%s'::lock_type AND id = '%s';"
};

static const char *dss_translate(enum dss_type type, const void *item_list,
                                 int pos)
{
    switch (type) {
    case DSS_DEVICE: {
        const struct dev_info *dev_ls = item_list;

        return dev_ls[pos].rsc.id.name;
    }
    case DSS_MEDIA:
    case DSS_MEDIA_UPDATE_LOCK: {
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
        g_string_append(ids[i], escape_string);

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

static int basic_lock(struct dss_handle *handle, enum dss_type lock_type,
                      const char *lock_id, int lock_owner,
                      const char *lock_hostname)
{
    GString *request = g_string_new("");
    PGconn *conn = handle->dh_conn;
    PGresult *res;
    int rc;

    g_string_printf(request, lock_query[DSS_LOCK_QUERY],
                    dss_type_names[lock_type], lock_id, lock_owner,
                    lock_hostname);

    rc = execute(conn, request, &res, PGRES_COMMAND_OK);

    PQclear(res);
    g_string_free(request, true);

    return rc;
}

static int basic_refresh(struct dss_handle *handle, enum dss_type lock_type,
                         const char *lock_id, int lock_owner,
                         const char *lock_hostname)
{
    GString *request = g_string_new("");
    PGconn *conn = handle->dh_conn;
    PGresult *res;
    int rc = 0;

    g_string_printf(request, lock_query[DSS_REFRESH_QUERY],
                    dss_type_names[lock_type], lock_id, lock_owner,
                    lock_hostname);

    rc = execute(conn, request, &res, PGRES_COMMAND_OK);

    PQclear(res);
    g_string_free(request, true);

    return rc;
}

static int basic_unlock(struct dss_handle *handle, enum dss_type lock_type,
                        const char *lock_id, int lock_owner,
                        const char *lock_hostname)
{
    GString *request = g_string_new("");
    PGconn *conn = handle->dh_conn;
    PGresult *res;
    int rc;

    if (lock_owner)
        g_string_printf(request, lock_query[DSS_UNLOCK_QUERY],
                        dss_type_names[lock_type], lock_id, lock_owner,
                        lock_hostname);
    else
        g_string_printf(request, lock_query[DSS_UNLOCK_FORCE_QUERY],
                        dss_type_names[lock_type], lock_id);

    rc = execute(conn, request, &res, PGRES_COMMAND_OK);

    PQclear(res);
    g_string_free(request, true);

    return rc;
}

static int basic_status(struct dss_handle *handle, enum dss_type lock_type,
                        const char *lock_id, struct pho_lock *lock)
{
    GString *request = g_string_new("");
    PGconn *conn = handle->dh_conn;
    struct timeval lock_timestamp;
    PGresult *res;
    int rc = 0;

    g_string_printf(request, lock_query[DSS_STATUS_QUERY],
                    dss_type_names[lock_type], lock_id);

    rc = execute(conn, request, &res, PGRES_TUPLES_OK);
    if (rc)
        goto out_cleanup;

    if (PQntuples(res) == 0) {
        pho_debug("Requested lock '%s' was not found, request: '%s' ",
                  lock_id, request->str);
        rc = -ENOLCK;
        if (lock) {
            lock->hostname = NULL;
            lock->owner = 0;
        }
        goto out_cleanup;
    }

    if (lock) {
        str2timeval(PQgetvalue(res, 0, 2), &lock_timestamp);
        rc = init_pho_lock(lock, PQgetvalue(res, 0, 0),
                           (int) strtoll(PQgetvalue(res, 0, 1), NULL, 10),
                           &lock_timestamp);
    }

out_cleanup:
    PQclear(res);
    g_string_free(request, true);

    return rc;
}

static int dss_generic(struct dss_handle *handle, enum dss_type type,
                       const void *item_list, int item_cnt,
                       struct dss_generic_call *callee)
{
    PGconn *conn = handle->dh_conn;
    GString **ids;
    int rc = 0;
    int i;

    ENTRY;

    LOCK_ID_LIST_ALLOCATE(ids, item_cnt);

    rc = dss_build_lock_id_list(conn, item_list, item_cnt, type, ids);
    if (rc)
        LOG_GOTO(cleanup, rc, "Ids list build failed");

    for (i = 0; i < item_cnt; ++i) {
        int rc2;

        if (callee->is_simple) {
            struct simple_param *param = &callee->params.simple;

            rc2 = param->basic_func(handle, type, ids[i]->str,
                                    param->locks ? param->locks + i : NULL);

        } else {
            struct dual_param *param = &callee->params.dual;

            rc2 = param->basic_func(handle, type, ids[i]->str,
                                    param->lock_owner, param->lock_hostname);
        }

        if (rc2) {
            rc = rc ? : rc2;
            pho_debug("Failed to %s %s (%s)", callee->action, ids[i]->str,
                                              strerror(-rc2));
            if (callee->all_or_nothing)
                break;
        }
    }

    if (callee->all_or_nothing && rc)
        callee->rollback_func(handle, type, ids, i - 1);

cleanup:
    LOCK_ID_LIST_FREE(ids, item_cnt);

    return rc;
}

static int dss_lock_rollback(struct dss_handle *handle, enum dss_type type,
                             GString **ids, int rollback_cnt)
{
    int rc = 0;
    int i;

    for (i = rollback_cnt; i >= 0; --i) {
        int rc2;

        /* If a lock failure happens, we force every unlock */
        rc2 = basic_unlock(handle, type, ids[i]->str, 0, NULL);
        if (rc2) {
            rc = rc ? : rc2;
            pho_error(rc2, "Failed to unlock %s after lock failure, "
                           "database may be corrupted", ids[i]->str);
        }
    }
    return rc;
}

int _dss_lock(struct dss_handle *handle, enum dss_type type,
              const void *item_list, int item_cnt, const char *lock_hostname,
              int lock_pid)
{
    struct dss_generic_call callee = {
        .params = {
            .dual = {
                .lock_hostname = lock_hostname,
                .lock_owner = lock_pid,
                .basic_func = basic_lock
            }
        },
        .is_simple = false,
        .all_or_nothing = true,
        .rollback_func = dss_lock_rollback,
        .action = "lock"
    };

    return dss_generic(handle, type, item_list, item_cnt, &callee);
}

int dss_lock(struct dss_handle *handle, enum dss_type type,
             const void *item_list, int item_cnt)
{
    const char *hostname;
    int pid;

    if (fill_host_owner(&hostname, &pid))
        LOG_RETURN(-EINVAL, "Couldn't retrieve hostname");

    return _dss_lock(handle, type, item_list, item_cnt, hostname, pid);
}

int _dss_lock_refresh(struct dss_handle *handle, enum dss_type type,
                      const void *item_list, int item_cnt,
                      const char *lock_hostname, int lock_owner)
{
    struct dss_generic_call callee = {
        .params = {
            .dual = {
                .lock_hostname = lock_hostname,
                .lock_owner = lock_owner,
                .basic_func = basic_refresh
            }
        },
        .is_simple = false,
        .all_or_nothing = false,
        .action = "refresh"
    };

    return dss_generic(handle, type, item_list, item_cnt, &callee);
}

int dss_lock_refresh(struct dss_handle *handle, enum dss_type type,
                     const void *item_list, int item_cnt)
{
    const char *hostname;
    int pid;

    if (fill_host_owner(&hostname, &pid))
        LOG_RETURN(-EINVAL, "Couldn't retrieve hostname");

    return _dss_lock_refresh(handle, type, item_list, item_cnt, hostname, pid);
}

int _dss_unlock(struct dss_handle *handle, enum dss_type type,
                const void *item_list, int item_cnt, const char *lock_hostname,
                int lock_owner)
{
    struct dss_generic_call callee = {
        .params = {
            .dual = {
                .lock_hostname = lock_hostname,
                .lock_owner = lock_owner,
                .basic_func = basic_unlock
            }
        },
        .is_simple = false,
        .all_or_nothing = false,
        .action = "unlock"
    };

    return dss_generic(handle, type, item_list, item_cnt, &callee);
}

int dss_unlock(struct dss_handle *handle, enum dss_type type,
               const void *item_list, int item_cnt, bool force_unlock)
{
    const char *hostname = NULL;
    int pid = 0;

    if (!force_unlock && fill_host_owner(&hostname, &pid))
        LOG_RETURN(-EINVAL, "Couldn't retrieve hostname");

    return _dss_unlock(handle, type, item_list, item_cnt, hostname, pid);
}

int dss_lock_status(struct dss_handle *handle, enum dss_type type,
                    const void *item_list, int item_cnt,
                    struct pho_lock *locks)
{
    struct dss_generic_call callee = {
        .params = {
            .simple = {
                .locks = locks,
                .basic_func = basic_status
            }
        },
        .is_simple = true,
        .all_or_nothing = false,
        .action = "status"
    };

    return dss_generic(handle, type, item_list, item_cnt, &callee);
}
