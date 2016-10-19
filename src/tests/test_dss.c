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
#include "pho_test_utils.h"
#include <errno.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <libgen.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>


int main(int argc, char **argv)
{
    struct dss_handle    dss_handle;
    enum dss_type        type;
    enum dss_set_action  action;
    struct  dev_info    *dev;
    struct  media_info  *media;
    struct  object_info *object;
    struct  layout_info *layout;
    struct  extent      *extents;
    struct dss_filter   *filter = NULL;
    void                *item_list;
    int                  item_cnt;
    int                  i, j;
    int                  rc;
    bool                 oidtest = false;

    test_env_initialize();

    if (argc < 3 || argc > 5) {
        fprintf(stderr, "Usage: %s ACTION TYPE [ \"CRIT\" ]\n", argv[0]);
        fprintf(stderr, "where  ACTION := { get | set | lock | unlock }\n");
        fprintf(stderr, "       TYPE := "
                        "{ device | media | object | extent }\n");
        fprintf(stderr, "       [ \"CRIT\" ] := \"field cmp value\"\n");
        fprintf(stderr, "Optional for set:\n");
        fprintf(stderr, "       oidtest set oid to NULL");
        exit(1);
    }

    setenv("PHOBOS_DSS_connect_string", "dbname=phobos host=localhost "
                                        "user=phobos password=phobos", 1);
    rc = dss_init(&dss_handle);

    if (rc) {
        pho_error(rc, "dss_init failed");
        exit(EXIT_FAILURE);
    }

    if (!strcmp(argv[1], "get")) {
        type = str2dss_type(argv[2]);
        if (type == DSS_INVAL) {
            pho_error(EINVAL,
                      "verb device|media|object|extend expected instead of %s",
                      argv[2]);
            exit(EXIT_FAILURE);
        }

        if (argc == 4) {
            pho_info("Crit Filter: %s", argv[3]);
            if (strcmp(argv[3], "all") != 0) {
                filter = malloc(sizeof(*filter));
                if (!filter) {
                    pho_error(ENOMEM, "Cannot allocate DSS filter");
                    exit(EXIT_FAILURE);
                }
                rc = dss_filter_build(filter, "%s", argv[3]);
                if (rc) {
                    pho_error(rc, "Cannot build DSS filter");
                    exit(EXIT_FAILURE);
                }
            }
        }

        rc = dss_get(&dss_handle, type, filter, &item_list, &item_cnt);
        if (rc) {
            pho_error(rc, "dss_get failed");
            exit(EXIT_FAILURE);
        }

        switch (type) {
        case DSS_DEVICE:
            for (i = 0, dev = item_list; i < item_cnt; i++, dev++) {
                pho_debug("Got device: family:%s host:%s model:%s path:%s "
                          "serial:%s adm_st:%s",
                          dev_family2str(dev->family),
                          dev->host, dev->model, dev->path, dev->serial,
                          adm_status2str(dev->adm_status));
            }
            break;
        case DSS_MEDIA:
            for (i = 0, media = item_list; i < item_cnt; i++, media++) {
                pho_debug("Got Media: label:%s model:%s adm_st:%s "
                          "address_type:%s fs_type:%s fs_status:%s",
                          media_id_get(&media->id),
                          media->model,
                          media_adm_status2str(media->adm_status),
                          address_type2str(media->addr_type),
                          fs_type2str(media->fs_type),
                          fs_status2str(media->fs_status));
                pho_debug("Got Media Stats: nb_obj:%lld logc_spc_used:%zd"
                          " phys_spc_used:%zd phys_spc_free:%zd:nb_errors:%ld:"
                          "last_load:%ld",
                          media->stats.nb_obj,
                          media->stats.logc_spc_used,
                          media->stats.phys_spc_used,
                          media->stats.phys_spc_free,
                          media->stats.nb_errors,
                          (long)media->stats.last_load);
            }
            break;
        case DSS_OBJECT:
            for (i = 0, object = item_list; i < item_cnt; i++, object++)
                pho_debug("Got object: oid:%s", object->oid);
            break;
        case DSS_EXTENT:
            for (i = 0,  layout = item_list; i < item_cnt; i++, layout++) {
                pho_debug("Got layout: oid:%s ext_count:%u state:%s type:%s",
                          layout->oid, layout->ext_count,
                          extent_state2str(layout->state),
                          layout_type2str(layout->type));
                extents = layout->extents;
                for (j = 0; j < layout->ext_count; j++) {
                    pho_debug("->Got extent: layout_idx:%d, size:%zu,"
                             " address:%s,media type:%s, media:%s",
                             extents->layout_idx, extents->size,
                             extents->address.buff,
                             dev_family2str(extents->media.type),
                             media_id_get(&extents->media));
                    extents++;
                }
            }
            break;
        default:
            abort();
        }

        dss_res_free(item_list, item_cnt);

    } else if (!strcmp(argv[1], "set")) {
        type = str2dss_type(argv[2]);

        if (type == DSS_INVAL) {
            pho_error(EINVAL,
                      "verb dev|media|object|extent expected instead of %s",
                      argv[2]);
            exit(EXIT_FAILURE);
        }

        action = str2dss_set_action(argv[3]);
        if (argc == 5)
                if (strcmp(argv[4], "oidtest") == 0) {
                        oidtest = true;
                        pho_debug("Switch to oidtest mode (test null oid)");
                }

        rc = dss_get(&dss_handle, type, NULL, &item_list, &item_cnt);
        if (rc) {
            pho_error(rc, "dss_get failed");
            exit(EXIT_FAILURE);
        }

        switch (type) {
        case DSS_DEVICE:
            for (i = 0, dev = item_list; i < item_cnt; i++, dev++) {
                if (action == DSS_SET_INSERT) {
                    rc = asprintf(&dev->serial, "%sCOPY", dev->serial);
                    assert(rc > 0);
                }
                if (action == DSS_SET_UPDATE) {
                    rc = asprintf(&dev->host, "%sUPDATE", dev->host);
                    assert(rc > 0);
                }
            }
            break;
        case DSS_MEDIA:
            for (i = 0, media = item_list; i < item_cnt; i++, media++) {
                const char *id;
                char *s;

                if (action == DSS_SET_INSERT) {
                    id = media_id_get(&media->id);
                    rc = asprintf(&s, "%sCOPY", id);
                    assert(rc > 0);
                    media_id_set(&media->id, s);
                } else if (action == DSS_SET_UPDATE) {
                    media->stats.nb_obj += 1000;
                }
            }
            break;
        case DSS_OBJECT:
            for (i = 0, object = item_list; i < item_cnt; i++, object++) {
                if (action == DSS_SET_INSERT) {
                    char *s;

                    rc = asprintf(&s, "%sCOPY", object->oid);
                    assert(rc > 0);
                    object->oid = s;
                }
                if (oidtest)
                    object->oid = NULL;
            }
            break;
        case DSS_EXTENT:
            for (i = 0,  layout = item_list; i < item_cnt; i++, layout++) {
                if (action == DSS_SET_INSERT) {
                    char *s;

                    asprintf(&s, "%sCOPY", layout->oid);
                    layout->oid = s;
                }
                else if (action == DSS_SET_UPDATE)
                    layout->extents[0].size = 0;
                if (oidtest)
                    layout->oid = NULL;
            }
            break;
        default:
            abort();
        }

        rc = dss_set(&dss_handle, type, item_list, item_cnt, action);
        if (rc) {
            pho_error(rc, "dss_set failed");
            exit(EXIT_FAILURE);
        }
    } else if (!strcmp(argv[1], "lock")) {
        type = str2dss_type(argv[2]);

        if (type != DSS_DEVICE && type != DSS_MEDIA) {
            pho_error(EINVAL, "verb dev expected instead of %s", argv[2]);
            exit(EXIT_FAILURE);
        }

        rc = dss_get(&dss_handle, type, NULL, &item_list, &item_cnt);
        if (rc) {
            pho_error(rc, "dss_get failed");
            exit(EXIT_FAILURE);
        }

        rc = dss_lock(&dss_handle, item_list, item_cnt, type);
        if (rc) {
            pho_error(rc, "dss_lock failed");
            exit(EXIT_FAILURE);
        }

    } else if (!strcmp(argv[1], "unlock")) {
        type = str2dss_type(argv[2]);

        if (type != DSS_DEVICE && type != DSS_MEDIA) {
            pho_error(EINVAL, "verb dev expected instead of %s", argv[2]);
            exit(EXIT_FAILURE);
        }

        rc = dss_get(&dss_handle, type, NULL, &item_list, &item_cnt);
        if (rc) {
            pho_error(rc, "dss_get failed");
            exit(EXIT_FAILURE);
        }

        rc = dss_unlock(&dss_handle, item_list, item_cnt, type);
        if (rc) {
            pho_error(rc, "dss_unlock failed");
            exit(EXIT_FAILURE);
        }
    } else {
        pho_error(EINVAL, "verb get|set expected instead of %s", argv[2]);
        exit(EXIT_FAILURE);
    }

    exit(EXIT_SUCCESS);
}
