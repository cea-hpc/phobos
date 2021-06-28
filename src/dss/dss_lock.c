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
} while (0)

struct simple_param {
    const char *lock_owner;
    int (*basic_func)(struct dss_handle *handle, const char *lock_id,
                      const char *lock_owner);
};

struct dual_param {
    struct pho_lock *locks;
    int (*basic_func)(struct dss_handle *handle, const char *lock_id,
                      struct pho_lock *locks);
};

struct dss_generic_call {
    union {
        struct simple_param simple;
        struct dual_param dual;
    } params;
    bool is_simple;
    int (*rollback_func)(struct dss_handle *handle, GString **ids,
                         int rollback_cnt);
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
    [DSS_STATUS_QUERY]       = "SELECT owner, timestamp FROM lock "
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

static int basic_refresh(struct dss_handle *handle, const char *lock_id,
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

static int basic_status(struct dss_handle *handle,
                        const char *lock_id, struct pho_lock *lock)
{
    GString *request = g_string_new("");
    PGconn *conn = handle->dh_conn;
    struct timeval lock_timestamp;
    PGresult *res;
    int rc = 0;

    g_string_printf(request, lock_query[DSS_STATUS_QUERY], lock_id);

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

    /* XXX: The below code is temporary, and will be changed in the
     * next patch
     */
    if (lock) {
        char *owner = PQgetvalue(res, 0, 0);
        char *lock_hostname = NULL;
        char *lock_owner = NULL;
        char *second_colon;
        char *first_colon;
        pid_t pid;

        str2timeval(PQgetvalue(res, 0, 1), &lock_timestamp);

        first_colon = strchr(owner, ':');
        if (first_colon++) {
            second_colon = strchr(first_colon, ':');

            lock_hostname = strndup(owner, first_colon - owner - 1);
            if (!lock_hostname)
                return -errno;

            lock_owner = second_colon ?
                            strndup(first_colon,
                                    second_colon - first_colon - 1) :
                            strdup(first_colon);
            if (!lock_owner)
                return -errno;

            pid = (pid_t) strtoll(lock_owner, NULL, 10);
            if (errno == EINVAL && errno == ERANGE)
                LOG_GOTO(out_cleanup, rc = -errno,
                         "Conversion error occurred: %d\n", errno);

            rc = init_pho_lock(lock, lock_hostname, pid, &lock_timestamp);

            free(lock_hostname);
            free(lock_owner);
        } else {
            rc = init_pho_lock(lock, owner, 0, &lock_timestamp);
        }
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

            rc2 = param->basic_func(handle, ids[i]->str, param->lock_owner);
        } else {
            struct dual_param *param = &callee->params.dual;

            rc2 = param->basic_func(handle, ids[i]->str,
                                    param->locks ? param->locks + i : NULL);
        }

        if (rc2) {
            rc = rc ? : rc2;
            pho_error(rc2, "Failed to %s %s", callee->action, ids[i]->str);
            if (callee->all_or_nothing)
                break;
        }
    }

    if (callee->all_or_nothing && rc)
        callee->rollback_func(handle, ids, i - 1);

cleanup:
    LOCK_ID_LIST_FREE(ids, item_cnt);

    return rc;
}

static int dss_lock_rollback(struct dss_handle *handle, GString **ids,
                             int rollback_cnt)
{
    int rc = 0;
    int i;

    for (i = rollback_cnt; i >= 0; --i) {
        int rc2;

        /* If a lock failure happens, we force every unlock */
        rc2 = basic_unlock(handle, ids[i]->str, NULL);
        if (rc2) {
            rc = rc ? : rc2;
            pho_error(rc2, "Failed to unlock %s after lock failure, "
                           "database may be corrupted", ids[i]->str);
        }
    }
    return rc;
}

int dss_lock(struct dss_handle *handle, enum dss_type type,
             const void *item_list, int item_cnt, const char *lock_owner)
{
    struct dss_generic_call callee = {
        .params = {
            .simple = {
                .lock_owner = lock_owner,
                .basic_func = basic_lock
            }
        },
        .is_simple = true,
        .all_or_nothing = true,
        .rollback_func = dss_lock_rollback,
        .action = "lock"
    };

    return dss_generic(handle, type, item_list, item_cnt, &callee);
}

int dss_lock_refresh(struct dss_handle *handle, enum dss_type type,
                     const void *item_list, int item_cnt,
                     const char *lock_owner)
{
    struct dss_generic_call callee = {
        .params = {
            .simple = {
                .lock_owner = lock_owner,
                .basic_func = basic_refresh
            }
        },
        .is_simple = true,
        .all_or_nothing = false,
        .action = "refresh"
    };

    return dss_generic(handle, type, item_list, item_cnt, &callee);
}

int dss_unlock(struct dss_handle *handle, enum dss_type type,
               const void *item_list, int item_cnt, const char *lock_owner)
{
    struct dss_generic_call callee = {
        .params = {
            .simple = {
                .lock_owner = lock_owner,
                .basic_func = basic_unlock
            }
        },
        .is_simple = true,
        .all_or_nothing = false,
        .action = "unlock"
    };

    return dss_generic(handle, type, item_list, item_cnt, &callee);
}

int dss_lock_status(struct dss_handle *handle, enum dss_type type,
                    const void *item_list, int item_cnt,
                    struct pho_lock *locks)
{
    struct dss_generic_call callee = {
        .params = {
            .dual = {
                .locks = locks,
                .basic_func = basic_status
            }
        },
        .is_simple = false,
        .all_or_nothing = false,
        .action = "status"
    };

    return dss_generic(handle, type, item_list, item_cnt, &callee);
}

int dss_hostname_from_lock_owner(const char *lock_owner, char **hostname)
{
    char *colon_index;

    /* parse lock_owner */
    colon_index = strchr(lock_owner, ':');

    /* checks that the lock string contains at least one ':' */
    if (!colon_index) {
        *hostname = NULL;
        LOG_RETURN(-EBADF, "Unable to get hostname from lock_owner %s",
                   lock_owner);
    }

    /* extract the first part of the lock_owner string */
    *hostname = strndup(lock_owner, colon_index - lock_owner);
    if (!*hostname)
        LOG_RETURN(-ENOMEM, "Unable to copy hostname from lock_owner %s",
                   lock_owner);

    /* success */
    return 0;
}
