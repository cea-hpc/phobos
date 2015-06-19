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

#ifdef _TEST
int dss_init(const char *conninfo, void **handle)
{
    *handle = (void *)0x1;
    return 0;
}

void dss_fini(void *handle)
{
}

/** Default devices for tests */
#define TEST_MTXIDX0       2
#define TEST_MTXIDX1       3

#define TEST_DRV0       "/dev/IBMtape0"
#define TEST_DRV1       "/dev/IBMtape1"

#define TEST_SERIAL0    "1013005381"
#define TEST_SERIAL1    "1014005381"

#define TEST_MNT0       "/mnt/tape0"
#define TEST_MNT1       "/mnt/tape1"

#define LTO6_MODEL      "ULTRIUM-TD6"

static struct dev_info test_dev[] = {
    { PHO_DEV_TAPE, "lto6", LTO6_MODEL, TEST_DRV0, TEST_SERIAL0,
      TEST_MTXIDX0, PHO_DEV_ADM_ST_UNLOCKED },
    { PHO_DEV_TAPE, "lto6", LTO6_MODEL, TEST_DRV1, TEST_SERIAL1,
      TEST_MTXIDX1, PHO_DEV_ADM_ST_UNLOCKED }
};
#define TEST_DEV_COUNT 2

static struct media_info test_med[] = {
    { .media = { .type = PHO_MED_TAPE, .id_u.label = "073220L6" } ,
      .fs_type = PHO_FS_LTFS, .addr_type = PHO_ADDR_HASH1, .nb_obj = 2,
      6291456000LL, 41474048LL * 1024, 2310174720LL * 1024 },
    { .media = { .type = PHO_MED_TAPE, .id_u.label = "073221L6" },
      .fs_type = PHO_FS_LTFS, .addr_type = PHO_ADDR_HASH1, .nb_obj = 2,
      6291456000LL, 14681088LL * 1024, 2336967680LL * 1024 }
};
#define TEST_TAPE_COUNT 2

int dss_get(void *handle, enum dss_type type, struct dss_crit *crit,
            int crit_cnt, void **item_list, int *item_cnt)
{
    int i;

    *item_cnt = 0;

    if (type == DSS_DEVICE) {
        *item_list = calloc(TEST_DEV_COUNT, sizeof(struct dev_info));
        if (*item_list == NULL)
            return -ENOMEM;
        *item_cnt = TEST_DEV_COUNT;

        for (i = 0 ; i < TEST_DEV_COUNT; i++)
            (*(struct dev_info **)item_list)[i] = test_dev[i];

        return 0;
    } else if (type == DSS_MEDIA) {
        for (i = 0 ; i < TEST_TAPE_COUNT; i++) {
            /* match tape label */
            if (crit_cnt > 0 && crit[0].crit_name == DSS_DEV_id
                && !strcmp(crit[0].crit_val.val_str,
                           test_med[i].media.id_u.label)) {
                *item_list = calloc(1, sizeof(struct media_info));
                if (*item_list == NULL)
                    return -ENOMEM;
                (*(struct media_info **)item_list)[0] = test_med[i];
                *item_cnt = 1;
                break;
            }
        }

        return 0;
    }

    return -ENOTSUP;
}

void dss_res_free(void *item_list, int item_cnt)
{
    free(item_list);
}
#else

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
            g_string_new("SELECT family, type, id, adm_status FROM device");
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
            pho_error(EIO, "Query (%s) failed: %s", clause->str,
                      PQerrorMessage(conn));
            PQclear(res);
            g_string_free(clause, true);
            return -EIO;
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
#endif
