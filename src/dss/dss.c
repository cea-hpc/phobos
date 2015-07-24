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
#include <jansson.h>

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

/**
 * helper arrays to build SQL query
 */
static const char * const base_query[] = {
    [DSS_DEVICE] = "SELECT family, model, id, adm_status,"
                   " host, path, changer_idx FROM device",
    [DSS_MEDIA]  = "SELECT family, model, id, adm_status,"
                   " address_type, fs_type, fs_status, stats FROM media",
};

static const size_t const res_size[] = {
    [DSS_DEVICE] = sizeof(struct dev_info),
    [DSS_MEDIA]  = sizeof(struct media_info),
};

/**
 * Extract media statistics from json
 *
 * \param[in]  stats  media stats to be filled with stats
 * \param[in]  json   String with json media stats
 *
 * \return 0 on success, negative error code on failure.
 */
static int dss_media_stats_decode(struct media_stats *stats, const char *json)
{
    json_t *root;
    json_error_t json_error;
    int parse_error;
    size_t jflags = JSON_REJECT_DUPLICATES;

    root = json_loads(json, jflags, &json_error);
    if (!root)
        LOG_RETURN(-EINVAL, "Failed to parse json data: %s",
                  json_error.text);

    if (!json_is_object(root))
        return -EINVAL;

    parse_error = 0;
    stats->nb_obj =
       json_dict2uint64(root, "nb_obj", &parse_error);
    stats->logc_spc_used =
       json_dict2uint64(root, "logc_spc_used", &parse_error);
    stats->phys_spc_used =
       json_dict2uint64(root, "phys_spc_used", &parse_error);
    stats->phys_spc_free =
       json_dict2uint64(root, "phys_spc_free", &parse_error);
    json_decref(root);

    if (parse_error > 0)
        LOG_RETURN(EINVAL, "Json parser: missing mandatory fields"
                          " in media stats, count: %d", parse_error);

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
    size_t dss_res_size;

    *item_list = NULL;
    *item_cnt = 0;

    if (handle == NULL || item_list == NULL || item_cnt == NULL) {
        rc = -EINVAL;
        pho_error(rc, "handle: %p, item_list: %p, item_cnt: %p",
                  handle, item_list, item_cnt);
        return rc;
    }

    if (type != DSS_DEVICE && type != DSS_MEDIA) {
        rc = -ENOTSUP;
        pho_error(rc, "unsupported DSS request type %#x", type);
        return rc;
    }

    /* get everything if no criteria */
    clause = g_string_new(base_query[type]);
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

    dss_res_size = sizeof(struct dss_result) + PQntuples(res) * res_size[type];
    dss_res = malloc(dss_res_size);
    if (dss_res == NULL) {
        pho_error(ENOMEM, "dss_res allocation failed (size %z)", dss_res_size);
        return -ENOMEM;
    }


    switch (type) {
    case DSS_DEVICE:
        dss_res->pg_res = res;
        for (i = 0; i < PQntuples(res); i++) {
            struct dev_info *p_dev = &dss_res->u.dev[i];

            p_dev->family = str2dev_family(PQgetvalue(res, i, 0));
            p_dev->model  = PQgetvalue(res, i, 1);
            p_dev->serial = PQgetvalue(res, i, 2);
            p_dev->adm_status =
                str2adm_status(PQgetvalue(res, i, 3));
            p_dev->host   = PQgetvalue(res, i, 4);
            p_dev->path   = PQgetvalue(res, i, 5);
            /** @todo replace atoi by proper pq function */
            p_dev->changer_idx = atoi(PQgetvalue(res, i, 6));
        }

        *item_list = dss_res->u.dev;
        *item_cnt = PQntuples(res);
        break;

    case DSS_MEDIA:
        dss_res->pg_res = res;
        for (i = 0; i < PQntuples(res); i++) {
            struct media_info *p_media = &dss_res->u.media[i];

            p_media->id.type = str2dev_family(PQgetvalue(res, i, 0));
            p_media->model = PQgetvalue(res, i, 1);
            media_id_set(&p_media->id, PQgetvalue(res, i, 2));
            p_media->adm_status = media_str2adm_status(PQgetvalue(res, i, 3));
            p_media->addr_type = str2address_type(PQgetvalue(res, i, 4));
            p_media->fs_type = str2fs_type(PQgetvalue(res, i, 5));
            p_media->fs_status = str2fs_status(PQgetvalue(res, i, 6));
            rc = dss_media_stats_decode(&p_media->stats, PQgetvalue(res, i, 7));
            if (rc)
                return rc;

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
