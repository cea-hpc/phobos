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
#include <inttypes.h>


int main(int argc, char **argv)
{
    int     rc;
    void                *dss_handle;
    enum dss_type        type;
    enum dss_set_action  action;
    struct  dev_info    *dev;
    struct  media_info  *media;
    struct  object_info *object;
    struct  layout_info *layout;
    struct  extent      *extents;
    void                *item_list;
    int                  item_cnt;
    struct  dss_crit    *crit;
    int                  crit_cnt;
    int                  i, j;
    char                *parser;


    pho_log_level_set(PHO_LOG_VERB);

    if (argc < 3 || argc > 5) {
        fprintf(stderr, "Usage: %s ACTION TYPE [ \"CRIT\" ]\n", argv[0]);
        fprintf(stderr, "where  ACTION := { get | set }\n");
        fprintf(stderr, "       TYPE := "
                        "{ device | media | object | extent }\n");
        fprintf(stderr, "       [ \"CRIT\" ] := \"field cmp value\"\n");
        exit(1);
    }
    if (!strcmp(argv[1], "get")) {
        type = str2dss_type(argv[2]);
        if (type == DSS_INVAL) {
            fprintf(stderr, "verb device|media|object|extend "
                            "expected at '%s'\n", argv[2]);
            exit(EINVAL);
        }
        if (argc == 4) {
            printf("Crit Filter: %s\n", argv[3]);
            parser = strtok(argv[3], " ");
            if (!parser)
                return -EINVAL;

            if (!strcmp(parser, "all")) {
                crit_cnt = 0;
                crit = NULL;
            } else {
                crit = malloc(sizeof(struct dss_crit));
                crit->crit_name = str2dss_fields(parser);
                parser = strtok(NULL, " ");
                if (!parser)
                    return -EINVAL;
                crit->crit_cmp = str2dss_cmp(parser);
                parser = strtok(NULL, " ");
                if (!parser)
                    return -EINVAL;
                str2dss_val_fill(crit->crit_name, parser, &crit->crit_val);
                crit_cnt = 1;
            }
        } else {
            crit = NULL;
            crit_cnt = 0;
        }

        rc = dss_init("dbname=phobos"
                      " host=localhost"
                      " user=phobos"
                      " password=phobos", &dss_handle);
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
                printf("Got device: family:%s host:%s model:%s path:%s "
                       "serial:%s adm_st:%s changer_idx:%d\n",
                       dev_family2str(dev->family),
                       dev->host, dev->model, dev->path, dev->serial,
                       adm_status2str(dev->adm_status),
                       dev->changer_idx);
            }
            break;
        case DSS_MEDIA:
            for (i = 0, media = item_list; i < item_cnt; i++, media++) {
                printf("Got Media: label:%s model:%s adm_st:%s address_type:%s"
                       " fs_type:%s fs_status:%s\n",
                       media_id_get(&media->id),
                       media->model,
                       media_adm_status2str(media->adm_status),
                       address_type2str(media->addr_type),
                       fs_type2str(media->fs_type),
                       fs_status2str(media->fs_status));
                printf("Got Media Stats: nb_obj:%" PRIu64 " logc_spc_used:%zu"
                       " phys_spc_used:%zu phys_spc_free:%zu\n",
                       media->stats.nb_obj,
                       media->stats.logc_spc_used,
                       media->stats.phys_spc_used,
                       media->stats.phys_spc_free
                      );
            }
            break;
        case DSS_OBJECT:
            for (i = 0, object = item_list; i < item_cnt; i++, object++) {
                printf("Got object: oid:%s\n",
                       object->oid);
            }
            break;
        case DSS_EXTENT:
            for (i = 0,  layout = item_list; i < item_cnt; i++, layout++) {
                printf("Got layout: oid:%s ext_count:%u"
                       " state:%s type:%s\n",
                       layout->oid, layout->ext_count,
                       extent_state2str(layout->state),
                       layout_type2str(layout->type));
                extents = layout->extents;
                for (j = 0; j < layout->ext_count; j++) {
                    printf("->Got extent: layout_idx:%d, size:%zu,"
                           " address:%s,media type:%s, media:%s\n",
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
            fprintf(stderr, "verb dev|media|object|extent"
                            "expected at '%s'\n", argv[2]);
            exit(EINVAL);
        }
        action = str2dss_set_action(argv[3]);

        rc = dss_init("dbname=phobos"
                      " host=localhost"
                      " user=phobos"
                      " password=phobos", &dss_handle);
        if (rc) {
            fprintf(stderr, "dss_init failed: %s (%d)\n",
                    strerror(-rc), -rc);
            exit(-rc);
         }
        crit_cnt = 0;
        crit = NULL;

        rc = dss_get(dss_handle, type, crit, crit_cnt, &item_list, &item_cnt);
        if (rc) {
            fprintf(stderr, "dss_get failed: %s (%d)\n", strerror(-rc), -rc);
            exit(-rc);
        }

        switch (type) {
        case DSS_DEVICE:
            for (i = 0, dev = item_list; i < item_cnt; i++, dev++) {
                if (action == DSS_SET_INSERT)
                    asprintf(&dev->serial, "%sCOPY", dev->serial);
                if (action == DSS_SET_UPDATE)
                    asprintf(&dev->host, "%sUPDATE", dev->host);
            }
            break;
        case DSS_MEDIA:
            for (i = 0, media = item_list; i < item_cnt; i++, media++) {
                const char *id;
                char *s;

                if (action == DSS_SET_INSERT) {
                    id = media_id_get(&media->id);
                    asprintf(&s, "%sCOPY", id);
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

                    asprintf(&s, "%sCOPY", object->oid);
                    object->oid = s;
                }
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
            }
            break;
        default:
            abort();
        }

        rc = dss_set(dss_handle, type, item_list, item_cnt, action);
        if (rc) {
            fprintf(stderr, "dss_set failed: %s (%d)\n", strerror(-rc), -rc);
            exit(-rc);
        }

    } else {
        fprintf(stderr, "verb get | set expected at '%s'\n", argv[1]);
        rc = -EINVAL;
    }
    return rc;
}
