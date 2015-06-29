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
static int dss_crit_to_pattern(PGconn *conn, const struct dss_crit *crit,
                               GString *clause)
{

    char *escape_string;
    unsigned int escape_len;

    g_string_append_printf(clause, "%s %s ",
                           dss_fields2str(crit->crit_name),
                           dss_cmp2str(crit->crit_cmp));

    switch (dss_fields2type(crit->crit_name)) {
    /** @todo: Use macro from inttypes.h PRIu64, ... ? */
    case DSS_VAL_BIGINT:
        g_string_append_printf(clause, "'%lld'", crit->crit_val.val_bigint);
        break;
    case DSS_VAL_INT:
        g_string_append_printf(clause, "'%ld'", crit->crit_val.val_int);
        break;
    case DSS_VAL_BIGUINT:
        g_string_append_printf(clause, "'%llu'", crit->crit_val.val_biguint);
        break;
    case DSS_VAL_UINT:
        g_string_append_printf(clause, "'%lu'", crit->crit_val.val_uint);
        break;
    case DSS_VAL_ENUM:
        g_string_append_printf(clause, "'%s'",
                               dss_fields_enum2str(crit->crit_name,
                                                   crit->crit_val.val_int));
        break;

    case DSS_VAL_JSON:
        /* You need to filter on a key, not the whole json set*/
        return -EINVAL;

    case DSS_VAL_STR:
        /**
         *  According to libpq #1.3.2
         *  "point to" a buffer that is able to hold at least
         *  one more byte than twice the value of length,
         *  otherwise the behavior is undefined.
         */
        escape_len = sizeof(char)*strlen(crit->crit_val.val_str)*2+1;
        escape_string = malloc(escape_len);
        /** @todo: check error in case of encoding issue ? */
        PQescapeStringConn(conn, escape_string, crit->crit_val.val_str,
                           escape_len, NULL);
        g_string_append_printf(clause, "'%s'", escape_string);
        free(escape_string);
        break;
    case DSS_VAL_UNKNOWN:
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
    char *buf;
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
            rc = dss_crit_to_pattern(conn, crit, clause);
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
        /* get everything if no criteria */
        clause =
            g_string_new("SELECT id, adm_status FROM media");
        if (crit_cnt > 0)
            g_string_append(clause, " WHERE ");

        while (crit_cnt > 0) {
            rc = dss_crit_to_pattern(conn, crit, clause);
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
                   PQntuples(res) * sizeof(struct media_info);
        dss_res = malloc(res_size);
        if (dss_res == NULL) {
            pho_error(ENOMEM, "dss_res allocation failed (size %z)", res_size);
            return -ENOMEM;
        }

        dss_res->pg_res = res;
        for (i = 0; i < PQntuples(res); i++) {
            buf = PQgetvalue(res, i, 0);
            strncpy(dss_res->u.media[i].media.id_u.label, buf,
                    sizeof(dss_res->u.media[i].media.id_u.label));
            /** @todo
                dss_res->u.dev[i].fs_fype = PQgetvalue(res, i, 1);
                dss_res->u.dev[i].address_type   = PQgetvalue(res, i, 1);
                dss_res->u.media[i].adm_status =
                str2adm_status(PQgetvalue(res, i, 1));
            */
        }

        *item_list = dss_res->u.media;
        *item_cnt = PQntuples(res);

        break;


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
