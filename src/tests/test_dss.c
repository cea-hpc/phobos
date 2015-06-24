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
    int item_cnt;
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

        rc = dss_init("dbname = phobos", &dss_handle);
        if (rc) {
            fprintf(stderr, "dss_init failed: %s (%d)\n", strerror(-rc), -rc);
            exit(-rc);
        }

        rc = dss_get(dss_handle, type, NULL, 0, &item_list, &item_cnt);
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
