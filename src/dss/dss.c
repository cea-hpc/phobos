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

#include "pho_dss.h"
#include <errno.h>
#include <stdlib.h>
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
#else
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

int dss_get(void *handle, enum dss_type type, struct dss_crit *crit,
            int crit_cnt, void **item_list, int *item_cnt)
{
    return -ENOTSUP;
}
#endif
