/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 * Copyright 2014-2015 CEA/DAM. All Rights Reserved.
 */
/**
 * \brief test object store
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "pho_dss.h"
#include "pho_common.h"
#include <errno.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <libgen.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    int rc;
    void *dss_handle;
    enum dss_type type;
    void *item_list;
    struct dev_info   *dev;
    struct media_info *media;
    int item_cnt;
    struct dss_crit *crit;
    int crit_cnt;
    int i;

    pho_log_level_set(PHO_LOG_VERB);

    if (argc < 3 || argc > 4) {
        fprintf(stderr, "Usage: %s ACTION TYPE [ CRIT ]\n", argv[0]);
        fprintf(stderr, "where  ACTION := { get }\n");
        fprintf(stderr, "       TYPE := { dev | media }\n");
        exit(1);
    }

    if (!strcmp(argv[1], "get")) {
        if (!strcmp(argv[2], "dev")) {
            type = DSS_DEVICE;
        } else if (!strcmp(argv[2], "media")) {
            type = DSS_MEDIA;
        } else {
            fprintf(stderr, "verb dev|media expected at '%s'\n", argv[2]);
            exit(EINVAL);
        }
        if (argc == 4) {
            /** Arbitrary, just for testing purpose */
            /** @todo: parse & use template & free */
            if (!strcmp(argv[3], "disk")) {
                crit = malloc(sizeof(struct dss_crit));
                crit->crit_name = DSS_DEV_family;
                crit->crit_cmp  = DSS_CMP_EQ;
                crit->crit_val.val_int = (int)PHO_DEV_DISK;
                crit_cnt = 1;
            } else if (!strcmp(argv[3], "tape")) {
                crit = malloc(sizeof(struct dss_crit));
                crit->crit_name = DSS_DEV_family;
                crit->crit_cmp  = DSS_CMP_EQ;
                crit->crit_val.val_int = (int)PHO_DEV_TAPE;
                crit_cnt = 1;
            } else if (!strcmp(argv[3], "used_space")) {
                crit = malloc(sizeof(struct dss_crit));
                crit->crit_name = DSS_MDA_vol_used;
                crit->crit_cmp  = DSS_CMP_GT;
                crit->crit_val.val_bigint = 16000000000;
                crit_cnt = 1;

            } else if (!strcmp(argv[3], "all")) {
                crit = NULL;
                crit_cnt = 0;
            } else {
                fprintf(stderr, "verb disk/tape expected at '%s'\n", argv[3]);
                exit(EINVAL);
            }

        } else {
            crit = NULL;
            crit_cnt = 0;
        }

        rc = dss_init("dbname=phobos host=localhost user=phobos password=phobos",
                      &dss_handle);
        if (rc) {
            fprintf(stderr, "dss_init failed: %s (%d)\n", strerror(-rc), -rc);
            exit(-rc);
        }

        rc = dss_get(dss_handle, type, crit, crit_cnt, &item_list, &item_cnt);
        if (rc) {
            fprintf(stderr, "dss_get failed: %s (%d)\n", strerror(-rc), -rc);
            exit(-rc);
        }

        switch (type) {
        case DSS_DEVICE:
            for (i = 0, dev = item_list; i < item_cnt; i++, dev++) {
                printf("Got device: %s %s %s %s %s %s\n",
                       dev_family2str(dev->family), dev->type,
                       dev->model, dev->path, dev->serial,
                       adm_status2str(dev->adm_status));
            }
            break;
        case DSS_MEDIA:
            for (i = 0, media = item_list; i < item_cnt; i++, media++) {
                printf("Got Media: %s\n",
                       media->media.id_u.label);
            }
            break;
        default:
            assert(false);
        }

        dss_res_free(item_list, item_cnt);
    } else {
        fprintf(stderr, "verb get expected at '%s'\n", argv[1]);
        rc = -EINVAL;
    }
    return rc;
}
