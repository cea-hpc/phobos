/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 * Copyright 2014-2015 CEA/DAM. All Rights Reserved.
 */
/**
 * \brief  Phobos Distributed State Service API.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "pho_common.h"
#include "pho_dss.h"
#include <errno.h>
#include <stdlib.h>
#include <glib.h>
#include <libpq-fe.h>

struct dss_result {
    PGresult *pg_res;
    union {
        struct media_info media[0];
        struct dev_info   dev[0];
    } u;
};

#define res_of_item_list(_list) \
    container_of((_list), struct dss_result, u.media)


int dss_init(const char *conninfo, void **handle)
{
    PGconn *conn;

    *handle = NULL;

    conn = PQconnectdb(conninfo);

    if (PQstatus(conn) != CONNECTION_OK) {
        /** @todo: figure how to get an errno-like value out of it */
        pho_error(ENOTCONN, "Connection to database failed: %s",
                PQerrorMessage(conn));
        return -ENOTCONN;
    }

    *handle = conn;
    return 0;
}

void dss_fini(void *handle)
{
    PQfinish(handle);
}

/**
 * Converts one criteria to a psql WHERE clause
 * @param[in]   crit    a single criteria to handle
 * @param[out]  clause  clause is appended here
 */
static int dss_crit_to_pattern(struct dss_crit *crit, GString *clause)
{
    switch (crit->crit_name) {
    default:
        return -EINVAL;
    }

    return 0;
}

int dss_get(void *handle, enum dss_type type, struct dss_crit *crit,
            int crit_cnt, void **item_list, int *item_cnt)
{
    PGconn *conn = handle;
    PGresult *res;
    GString *clause;
    struct dss_result *dss_res;
    int i, rc;
    size_t res_size;

    *item_list = NULL;
    *item_cnt = 0;

    if (handle == NULL || item_list == NULL || item_cnt == NULL) {
        pho_error(EINVAL, "handle: %p, item_list: %p, item_cnt: %p",
                  handle, item_list, item_cnt);
        return -EINVAL;
    }

    switch (type) {
    case DSS_DEVICE:
        /* get everything if no criteria */
        clause =
            g_string_new("SELECT family, model, id, adm_status FROM device");
        if (crit_cnt > 0)
            g_string_append(clause, " WHERE ");

        while (crit_cnt > 0) {
            rc = dss_crit_to_pattern(crit, clause);
            if (rc) {
                pho_error(rc, "failed to append crit %d to clause %s",
                          crit->crit_name, clause->str);
                g_string_free(clause, true);
                return rc;
            }
            crit++;
            crit_cnt--;
            if (crit_cnt > 0)
                g_string_append(clause, " AND ");
        }

        pho_debug("Executing request: %s", clause->str);
        res = PQexec(conn, clause->str);
        if (PQresultStatus(res) != PGRES_TUPLES_OK) {
            rc = -ECOMM;
            pho_error(rc, "Query (%s) failed: %s", clause->str,
                      PQerrorMessage(conn));
            PQclear(res);
            g_string_free(clause, true);
            return rc;
        }
        g_string_free(clause, true);

        res_size = sizeof(struct dss_result) +
                   PQntuples(res) * sizeof(struct dev_info);
        dss_res = malloc(res_size);
        if (dss_res == NULL) {
            pho_error(ENOMEM, "dss_res allocation failed (size %z)", res_size);
            return -ENOMEM;
        }

        dss_res->pg_res = res;
        for (i = 0; i < PQntuples(res); i++) {
            dss_res->u.dev[i].family = str2dev_family(PQgetvalue(res, i, 0));
            dss_res->u.dev[i].type   = PQgetvalue(res, i, 1);
            dss_res->u.dev[i].path   = "";
            dss_res->u.dev[i].model  = "";
            dss_res->u.dev[i].serial = PQgetvalue(res, i, 2);
            dss_res->u.dev[i].changer_idx = -1;
            dss_res->u.dev[i].adm_status =
                str2adm_status(PQgetvalue(res, i, 3));
        }

        *item_list = dss_res->u.dev;
        *item_cnt = PQntuples(res);
        break;

    case DSS_MEDIA:
        return -ENOTSUP;

    default:
        return -EINVAL;
    }

    return 0;
}

void dss_res_free(void *item_list, int item_cnt)
{
    struct dss_result *dss_res;

    if (item_list) {
        dss_res = res_of_item_list(item_list);

        PQclear(dss_res->pg_res);
        free(dss_res);
    }
}
