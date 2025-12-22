/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2025 CEA/DAM.
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
#include <string.h>

#include <libpq-fe.h>

#include "dss_utils.h"
#include "dss_lock.h"
#include "pho_common.h"
#include "pho_dss.h"
#include "pho_type_utils.h"

#define LOCK_ID_LIST_ALLOCATE(_ids, _item_cnt)        \
do {                                                  \
    (_ids) = xmalloc((_item_cnt) * sizeof(*(_ids)));  \
    for (i = 0; i < (_item_cnt); ++i)                 \
        (_ids)[i] = g_string_new("");                 \
} while (0)

#define LOCK_ID_LIST_FREE(_ids, _item_cnt)            \
do {                                                  \
    for (i = 0; i < (_item_cnt); ++i)                 \
        g_string_free((_ids)[i], true);               \
    free((_ids));                                     \
} while (0)

enum lock_query_idx {
    DSS_CLEAN_DEVICE_QUERY,
    DSS_CLEAN_MEDIA_QUERY,
    DSS_PURGE_ALL_LOCKS_QUERY,
};

#define POSSESS_LOCK (1 << 0) /** caller wants to possess the lock, and remove
                                * its weak state
                                */
#define WEAK_LOCK    (1 << 1) /** caller wants to set the weak state of
                                * the lock
                                */
#define LOCATE_LOCK  (1 << 2) /* caller wants to set/update a locate lock */
#define FORCE_LOCK   (1 << 3) /* caller wants to force a lock unset */

#define WHERE_ID " WHERE type = '\%s'::lock_type AND id = '\%s' "

static int get_nb_affected_rows(PGresult *res)
{
    char *nb_rows_affected_str = PQcmdTuples(res);

    return *nb_rows_affected_str ? atoi(nb_rows_affected_str) : 0;
}

static const char * const lock_query[] = {
    [DSS_CLEAN_DEVICE_QUERY] = "WITH id_host AS (SELECT id || '_' || library "
                               "                        AS id, host "
                               "                     FROM device "
                               "                   WHERE family = '%s') "
                               "DELETE FROM lock "
                               "  WHERE type = 'device'::lock_type "
                               "    AND id IN (SELECT id FROM id_host) "
                               "    AND hostname = '%s'"
                               "    AND (hostname != "
                               "           (SELECT host FROM id_host "
                               "              WHERE lock.id = id_host.id) "
                               "         OR owner != %d);",
    [DSS_CLEAN_MEDIA_QUERY]  = "DELETE FROM lock "
                               "  WHERE hostname = '%s' "
                               "    AND owner != %d "
                               "    AND ((type = 'media'::lock_type "
                               "          AND id NOT IN (%s)) "
                               /** Since the operations corresponding to these
                                * locks cannot be continued, we must unlock
                                * all of them to allow the LRS to update the
                                * media
                                */
                               "         OR type = 'media_update'::lock_type);",
    [DSS_PURGE_ALL_LOCKS_QUERY] = "TRUNCATE TABLE lock; "
};

static const char *dss_translate_prefix(enum dss_type type,
                                        const void *item_list,
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
    case DSS_DEPREC: {
        const struct object_info *obj_ls = item_list;

        return obj_ls[pos].uuid;
    }
    default:
        return NULL;
    }

    return NULL;
}

static const char *dss_translate_suffix(enum dss_type type,
                                        const void *item_list,
                                        int pos)
{
    switch (type) {
    case DSS_DEVICE: {
        const struct dev_info *dev_ls = item_list;

        return dev_ls[pos].rsc.id.library;
    }
    case DSS_MEDIA:
    case DSS_MEDIA_UPDATE_LOCK: {
        const struct media_info *mda_ls = item_list;

        return mda_ls[pos].rsc.id.library;
    }
    default:
        return NULL;
    }

    return NULL;
}

static void append_escaped_to_id(PGconn *conn, const char *string, GString *id)
{
    unsigned int escape_len;
    char *escape_string;

    escape_len = strlen(string) * 2 + 1;
    escape_string = xmalloc(escape_len);

    PQescapeStringConn(conn, escape_string, string, escape_len, NULL);
    g_string_append(id, escape_string);
    free(escape_string);
}

static int dss_build_lock_id_list(PGconn *conn, const void *item_list,
                                  int item_cnt, enum dss_type type,
                                  GString **ids)
{
    const char   *name;
    int           i;

    for (i = 0; i < item_cnt; i++) {
        name = dss_translate_prefix(type, item_list, i);
        if (!name)
            LOG_RETURN(-EINVAL, "no lock id prefix found");

        append_escaped_to_id(conn, name, ids[i]);

        name = dss_translate_suffix(type, item_list, i);
        if (name) {
            g_string_append(ids[i], "_");
            append_escaped_to_id(conn, name, ids[i]);
        }

        if (ids[i]->len > PHO_DSS_MAX_LOCK_ID_LEN)
            LOG_RETURN(-EINVAL, "lock_id name too long");
    }

    return 0;
}

static int basic_lock(struct dss_handle *handle, enum dss_type lock_type,
                      const char *lock_id, int owner,
                      const char *hostname, int lock_flags)
{
    GString *request = g_string_new("");
    PGconn *conn = handle->dh_conn;
    PGresult *res;
    int rc;

    lock_type = lock_type != DSS_DEPREC ? lock_type : DSS_OBJECT;

    g_string_printf(request,
                    "INSERT INTO lock "
                    "  (type, id, is_weak, owner, hostname, last_locate) "
                    "  VALUES ('%s'::lock_type, '%s', %s, %d, '%s', %s)"
                    ";",
                    dss_type_names[lock_type], lock_id,
                    lock_flags & WEAK_LOCK ? "TRUE" : "FALSE",
                    owner, hostname,
                    lock_flags & LOCATE_LOCK ? "now()" : "NULL");

    rc = execute(conn, request->str, &res, PGRES_COMMAND_OK);

    PQclear(res);
    g_string_free(request, true);

    return rc;
}

static int basic_refresh(struct dss_handle *handle, enum dss_type lock_type,
                         const char *lock_id, int owner,
                         const char *hostname, int lock_flags)
{
    GString *request = g_string_new("");
    PGconn *conn = handle->dh_conn;
    PGresult *res;
    int rc;

    if (lock_flags & POSSESS_LOCK)
        g_string_printf(request,
                        "INSERT INTO lock AS l "
                        "  (type, id, is_weak, owner, hostname) "
                        "  VALUES('%s'::lock_type, '%s', FALSE, %d, '%s') "
                        "  ON CONFLICT (type, id) "
                        "    DO UPDATE "
                        "      SET timestamp = now(), "
                        "        owner = EXCLUDED.owner, "
                        "        is_weak = FALSE "
                        "      WHERE l.is_weak = TRUE AND "
                        "        l.hostname = EXCLUDED.hostname "
                        ";",
                        dss_type_names[lock_type], lock_id, owner, hostname);
    else if (lock_flags & LOCATE_LOCK)
        g_string_printf(request,
                        "UPDATE lock "
                        "  SET last_locate = now() "
                           WHERE_ID
                        ";",
                        dss_type_names[lock_type], lock_id);
    else if (lock_flags & WEAK_LOCK)
        g_string_printf(request,
                        "UPDATE lock "
                        "  SET is_weak = TRUE "
                           WHERE_ID
                        "        AND owner = %d AND hostname = '%s'"
                        ";",
                        dss_type_names[lock_type], lock_id, owner, hostname);
    else
        g_string_printf(request,
                        "UPDATE lock "
                        "  SET timestamp = now() "
                           WHERE_ID
                        "        AND owner = %d AND hostname = '%s'"
                        ";",
                        dss_type_names[lock_type], lock_id, owner, hostname);

    rc = execute(conn, request->str, &res, PGRES_COMMAND_OK);
    if (rc)
        goto out;

    if (get_nb_affected_rows(res) == 0)
        rc = -ENOLCK;

out:
    PQclear(res);
    g_string_free(request, true);

    return rc;

}

static int basic_unlock(struct dss_handle *handle, enum dss_type lock_type,
                        const char *lock_id, int owner,
                        const char *hostname, int lock_flags)
{
    GString *request = g_string_new("");
    PGconn *conn = handle->dh_conn;
    PGresult *res;
    int rc;

    if (lock_flags & FORCE_LOCK)
        g_string_printf(request,
                        "DELETE FROM lock "
                           WHERE_ID
                        ";",
                        dss_type_names[lock_type], lock_id);
    else
        g_string_printf(request,
                        "DELETE FROM lock "
                           WHERE_ID
                        "  AND ((owner = %d AND hostname = '%s') "
                        "       OR is_weak = TRUE)"
                        ";",
                        dss_type_names[lock_type], lock_id,
                        owner, hostname);

    rc = execute(conn, request->str, &res, PGRES_COMMAND_OK);
    if (rc)
        goto out;

    if (get_nb_affected_rows(res) == 0)
        rc = -ENOLCK;

out:
    PQclear(res);
    g_string_free(request, true);

    return rc;
}

static int basic_status(struct dss_handle *handle, enum dss_type lock_type,
                        const char *lock_id, struct pho_lock *lock)
{
    GString *request = g_string_new("");
    PGconn *conn = handle->dh_conn;
    struct timeval last_locate_ts;
    struct timeval lock_ts;
    PGresult *res;
    int rc;

    g_string_printf(request,
                    "SELECT hostname, owner, timestamp, last_locate, is_weak "
                    "  FROM lock "
                       WHERE_ID
                    ";",
                    dss_type_names[lock_type], lock_id);

    rc = execute(conn, request->str, &res, PGRES_TUPLES_OK);
    if (rc)
        goto out_cleanup;

    if (PQntuples(res) == 0) {
        pho_debug("Requested lock '%s' was not found, request: '%s'",
                  lock_id, request->str);
        rc = -ENOLCK;
        if (lock) {
            lock->hostname = NULL;
            lock->owner = 0;
        }
        goto out_cleanup;
    }

    if (lock) {
        str2timeval(PQgetvalue(res, 0, 2), &lock_ts);
        str2timeval(PQgetvalue(res, 0, 3), &last_locate_ts);
        init_pho_lock(lock,
                      PQgetvalue(res, 0, 0),
                      (int) strtoll(PQgetvalue(res, 0, 1), NULL, 10),
                      &lock_ts,
                      &last_locate_ts,
                      psqlstrbool2bool(*PQgetvalue(res, 0, 4)));
    }

out_cleanup:
    PQclear(res);
    g_string_free(request, true);

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
        rc2 = basic_unlock(handle, type, ids[i]->str, 0, NULL, FORCE_LOCK);
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
              int lock_pid, bool is_weak, bool is_locate)
{
    PGconn *conn = handle->dh_conn;
    int lock_flags = 0;
    GString **ids;
    int rc = 0;
    int i;

    ENTRY;

    LOCK_ID_LIST_ALLOCATE(ids, item_cnt);

    rc = dss_build_lock_id_list(conn, item_list, item_cnt, type, ids);
    if (rc)
        LOG_GOTO(cleanup, rc, "Ids list build failed");

    if (is_weak)
        lock_flags |= WEAK_LOCK;
    if (is_locate)
        lock_flags |= LOCATE_LOCK;

    for (i = 0; i < item_cnt; ++i) {
        int rc2;

        rc2 = basic_lock(handle, type, ids[i]->str, lock_pid,
                         lock_hostname, lock_flags);

        if (rc2) {
            rc = rc ? : rc2;
            pho_debug("Failed to lock %s (%s)", ids[i]->str, strerror(-rc2));
            break;
        }
    }

    if (rc)
        dss_lock_rollback(handle, type, ids, i - 1);

cleanup:
    LOCK_ID_LIST_FREE(ids, item_cnt);

    return rc;
}

int dss_lock(struct dss_handle *handle, enum dss_type type,
             const void *item_list, int item_cnt)
{
    const char *hostname;
    int pid;
    int rc;

    rc = fill_host_owner(&hostname, &pid);
    if (rc)
        LOG_RETURN(rc, "Couldn't retrieve hostname");

    return _dss_lock(handle, type, item_list, item_cnt, hostname, pid,
                     false, false);
}

int dss_lock_weak(struct dss_handle *handle, enum dss_type type,
                  const void *item_list, int item_cnt, const char *hostname,
                  bool is_locate)
{
    int pid;

    pid = getpid();

    return _dss_lock(handle, type, item_list, item_cnt, hostname, pid, true,
                     is_locate);
}

int _dss_lock_refresh(struct dss_handle *handle, enum dss_type type,
                      const void *item_list, int item_cnt,
                      const char *lock_hostname, int lock_owner,
                      bool locate, bool take_possession, bool set_weak)
{
    PGconn *conn = handle->dh_conn;
    int lock_flags = 0;
    GString **ids;
    int rc = 0;
    int i;

    ENTRY;

    LOCK_ID_LIST_ALLOCATE(ids, item_cnt);

    rc = dss_build_lock_id_list(conn, item_list, item_cnt, type, ids);
    if (rc)
        LOG_GOTO(cleanup, rc, "Ids list build failed");

    if (locate)
        lock_flags |= LOCATE_LOCK;
    if (take_possession)
        lock_flags |= POSSESS_LOCK;
    if (set_weak)
        lock_flags |= WEAK_LOCK;

    for (i = 0; i < item_cnt; ++i) {
        int rc2;

        rc2 = basic_refresh(handle, type, ids[i]->str,
                            lock_owner, lock_hostname, lock_flags);

        if (rc2) {
            rc = rc ? : rc2;
            pho_debug("Failed to refresh %s (%s)", ids[i]->str, strerror(-rc2));
        }
    }

cleanup:
    LOCK_ID_LIST_FREE(ids, item_cnt);

    return rc;
}

int dss_lock_refresh(struct dss_handle *handle, enum dss_type type,
                     const void *item_list, int item_cnt, bool locate)
{
    const char *hostname;
    int pid;

    if (fill_host_owner(&hostname, &pid))
        LOG_RETURN(-EINVAL, "Couldn't retrieve hostname");

    return _dss_lock_refresh(handle, type, item_list, item_cnt, hostname, pid,
                             locate, false, false);
}

int dss_lock_take_ownership(struct dss_handle *handle, enum dss_type type,
                            const void *item_list, int item_cnt)
{
    const char *hostname;
    int pid;

    if (fill_host_owner(&hostname, &pid))
        LOG_RETURN(-EINVAL, "Couldn't retrieve hostname");

    return _dss_lock_refresh(handle, type, item_list, item_cnt, hostname, pid,
                             false, true, false);
}

int dss_lock_set_weak(struct dss_handle *handle, enum dss_type type,
                      const void *item_list, int item_cnt)
{
    const char *hostname;
    int pid;

    if (fill_host_owner(&hostname, &pid))
        LOG_RETURN(-EINVAL, "Couldn't retrieve hostname");

    return _dss_lock_refresh(handle, type, item_list, item_cnt, hostname, pid,
                             false, false, true);
}

int _dss_unlock(struct dss_handle *handle, enum dss_type type,
                const void *item_list, int item_cnt, const char *lock_hostname,
                int lock_owner)
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

            rc2 = basic_unlock(handle, type, ids[i]->str, lock_owner,
                               lock_hostname,
                               !lock_owner && !lock_hostname ? FORCE_LOCK : 0);

        if (rc2) {
            rc = rc ? : rc2;
            pho_debug("Failed to unlock %s (%s)", ids[i]->str, strerror(-rc2));
        }
    }

cleanup:
    LOCK_ID_LIST_FREE(ids, item_cnt);

    return rc;
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

        rc2 = basic_status(handle, type, ids[i]->str, locks ? locks + i : NULL);

        if (rc2) {
            rc = rc ? : rc2;
            pho_debug("Failed to status %s (%s)", ids[i]->str, strerror(-rc2));
        }
    }

cleanup:
    LOCK_ID_LIST_FREE(ids, item_cnt);

    return rc;
}

int dss_lock_device_clean(struct dss_handle *handle, const char *lock_family,
                          const char *lock_hostname, int lock_owner)
{
    GString *request = g_string_new("");
    PGconn *conn = handle->dh_conn;
    PGresult *res;
    int rc = 0;

    ENTRY;

    g_string_printf(request, lock_query[DSS_CLEAN_DEVICE_QUERY],
                    lock_family, lock_hostname, lock_owner);
    rc = execute(conn, request->str, &res, PGRES_COMMAND_OK);

    PQclear(res);
    g_string_free(request, true);

    return rc;
}

int dss_lock_media_clean(struct dss_handle *handle,
                         const struct media_info *media, int media_cnt,
                         const char *lock_hostname, int lock_owner)
{
    GString *request = g_string_new("");
    GString *ids = g_string_new("");
    PGconn *conn = handle->dh_conn;
    PGresult *res;
    int rc = 0;
    int i;

    ENTRY;

    if (media_cnt == 0) {
        g_string_append(ids, "''");
    } else {
        for (i = 0; i < media_cnt; ++i) {
            g_string_append_printf(ids, "'%s'", media[i].rsc.id.name);
            if (i + 1 < media_cnt)
                g_string_append(ids, ", ");
        }
    }

    g_string_printf(request, lock_query[DSS_CLEAN_MEDIA_QUERY],
                    lock_hostname, lock_owner, ids->str);
    rc = execute(conn, request->str, &res, PGRES_COMMAND_OK);

    PQclear(res);
    g_string_free(request, true);
    g_string_free(ids, true);

    return rc;
}

int dss_lock_clean_select(struct dss_handle *handle,
                          const char *lock_hostname, const char *lock_type,
                          const char *dev_family, char **lock_ids, int n_ids)
{
    GString *request = g_string_new("");
    PGconn *conn = handle->dh_conn;
    bool and_clause = false;
    PGresult *res;
    int rc = 0;
    int i;

    ENTRY;

    g_string_printf(request, "DELETE FROM lock WHERE ");

    if (n_ids > 0) {
        g_string_append_printf(request, "(");
        for (i = 0; i < n_ids - 1; ++i)
            g_string_append_printf(request, "id = '%s' OR ", lock_ids[i]);
        g_string_append_printf(request, " id = '%s')", lock_ids[n_ids - 1]);
        and_clause = true;
    }

    if (lock_type) {
        if (and_clause)
            g_string_append_printf(request, " AND ");

        g_string_append_printf(request, " type = '%s'::lock_type", lock_type);
        if (dev_family) {
            lock_type = (strcmp(lock_type, "media_update") == 0) ? "media"
                                                                 : lock_type;
            g_string_append_printf(request, " AND id IN "
                                            "         (SELECT id || '_' || "
                                            "                 library FROM %s "
                                            "          WHERE family = '%s')",
                                            lock_type, dev_family);
        }
        and_clause = true;
    }

    if (lock_hostname) {
        if (and_clause)
            g_string_append_printf(request, " AND ");

        g_string_append_printf(request, " hostname = '%s' ", lock_hostname);
    }

    g_string_append_printf(request, " RETURNING *;");
    rc = execute(conn, request->str, &res, PGRES_TUPLES_OK);

    pho_info("%d lock(s) cleaned.", PQntuples(res));

    PQclear(res);
    g_string_free(request, true);

    return rc;
}

int dss_lock_clean_all(struct dss_handle *handle)
{
    GString *request = g_string_new("");
    PGconn *conn = handle->dh_conn;
    PGresult *res;
    int rc = 0;

    ENTRY;

    g_string_printf(request, "%s", lock_query[DSS_PURGE_ALL_LOCKS_QUERY]);
    rc = execute(conn, request->str, &res, PGRES_COMMAND_OK);

    PQclear(res);
    g_string_free(request, true);

    return rc;
}
