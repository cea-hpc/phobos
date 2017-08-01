/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2017 CEA/DAM.
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

static int dss_generic_get(struct dss_handle *handle, enum dss_type type,
                           const struct dss_filter *filter, void **item_list,
                           int *n)
{
    switch (type) {
    case DSS_OBJECT:
        return dss_object_get(handle, filter,
                              (struct object_info **)item_list, n);
    case DSS_EXTENT:
        return dss_extent_get(handle, filter,
                              (struct layout_info **)item_list, n);
    case DSS_DEVICE:
        return dss_device_get(handle, filter,
                              (struct dev_info **)item_list, n);
    case DSS_MEDIA:
        return dss_media_get(handle, filter,
                             (struct media_info **)item_list, n);
    default:
        return -ENOTSUP;
    }
}

static int dss_generic_set(struct dss_handle *handle, enum dss_type type,
                           void *item_list, int n, enum dss_set_action action)
{
    switch (type) {
    case DSS_OBJECT:
        return dss_object_set(handle, (struct object_info *)item_list, n,
                              action);
    case DSS_EXTENT:
        return dss_extent_set(handle, (struct layout_info *)item_list, n,
                              action);
    case DSS_DEVICE:
        return dss_device_set(handle, (struct dev_info *)item_list, n,
                              action);
    case DSS_MEDIA:
        return dss_media_set(handle, (struct media_info *)item_list, n,
                             action);
    default:
        return -ENOTSUP;
    }
}

static int dss_generic_lock(struct dss_handle *handle, enum dss_type type,
                            void *item_list, int n)
{
    switch (type) {
    case DSS_DEVICE:
        return dss_device_lock(handle, (struct dev_info *)item_list, n);
    case DSS_MEDIA:
        return dss_media_lock(handle, (struct media_info *)item_list, n);
    default:
        return -ENOTSUP;
    }
}

static int dss_generic_unlock(struct dss_handle *handle, enum dss_type type,
                              void *item_list, int n)
{
    switch (type) {
    case DSS_DEVICE:
        return dss_device_unlock(handle, (struct dev_info *)item_list, n);
    case DSS_MEDIA:
        return dss_media_unlock(handle, (struct media_info *)item_list, n);
    default:
        return -ENOTSUP;
    }
}

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

        rc = dss_generic_get(&dss_handle, type, filter, &item_list, &item_cnt);
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
                          fs_type2str(media->fs.type),
                          fs_status2str(media->fs.status));
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
                pho_debug("Got layout: "
                          "oid:%s ext_count:%u state:%s desc:%s-%d.%d",
                          layout->oid, layout->ext_count,
                          extent_state2str(layout->state),
                          layout->layout_desc.mod_name,
                          layout->layout_desc.mod_major,
                          layout->layout_desc.mod_minor);
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

        rc = dss_generic_get(&dss_handle, type, NULL, &item_list, &item_cnt);
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

                    rc = asprintf(&s, "%sCOPY", layout->oid);
                    assert(rc > 0);
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

        rc = dss_generic_set(&dss_handle, type, item_list, item_cnt, action);
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

        rc = dss_generic_get(&dss_handle, type, NULL, &item_list, &item_cnt);
        if (rc) {
            pho_error(rc, "dss_get failed");
            exit(EXIT_FAILURE);
        }

        rc = dss_generic_lock(&dss_handle, type, item_list, item_cnt);
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

        rc = dss_generic_get(&dss_handle, type, NULL, &item_list, &item_cnt);
        if (rc) {
            pho_error(rc, "dss_get failed");
            exit(EXIT_FAILURE);
        }

        rc = dss_generic_unlock(&dss_handle, type, item_list, item_cnt);
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
