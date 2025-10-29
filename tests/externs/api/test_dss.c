/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2025 CEA/DAM.
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
#include "test_setup.h"
#include "dss_lock.h"
#include <errno.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <libgen.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#define LOCK_HOSTNAME "generic_lock_hostname"
#define LOCK_OWNER "0"

static int dss_generic_get(struct dss_handle *handle, enum dss_type type,
                           const struct dss_filter *inner_filter,
                           const struct dss_filter *outer_filter,
                           void **item_list, int *n)
{
    switch (type) {
    case DSS_OBJECT:
        return dss_object_get(handle, inner_filter,
                              (struct object_info **)item_list, n, NULL);
    case DSS_DEPREC:
        return dss_deprecated_object_get(handle, inner_filter,
                                     (struct object_info **)item_list, n, NULL);
    case DSS_FULL_LAYOUT:
        return dss_full_layout_get(handle, inner_filter, outer_filter,
                                   (struct layout_info **)item_list, n, NULL);
    case DSS_DEVICE:
        return dss_device_get(handle, inner_filter,
                              (struct dev_info **)item_list, n, NULL);
    case DSS_MEDIA:
        return dss_media_get(handle, inner_filter,
                             (struct media_info **)item_list, n, NULL);
    default:
        return -ENOTSUP;
    }
}

static int dss_generic_set(struct dss_handle *handle, enum dss_type type,
                           void *item_list, int n, enum dss_set_action action,
                           uint64_t fields)
{
    switch (type) {
    case DSS_OBJECT:
        switch (action) {
        case DSS_SET_INSERT:
            return dss_object_insert(handle, (struct object_info *)item_list, n,
                                     DSS_SET_INSERT);
        case DSS_SET_UPDATE:
            return dss_object_update(handle, (struct object_info *)item_list,
                                     (struct object_info *) item_list, n,
                                     DSS_OBJECT_UPDATE_USER_MD);
        case DSS_SET_DELETE:
        default:
            return dss_object_delete(handle, (struct object_info *)item_list,
                                     n);
        }
    case DSS_DEPREC:
        switch (action) {
        case DSS_SET_INSERT:
        return dss_deprecated_object_insert(handle,
                                            (struct object_info *)item_list, n);
        case DSS_SET_DELETE:
            return dss_deprecated_object_delete(handle,
                                                (struct object_info *)item_list,
                                                n);
        default:
            return -ENOTSUP;
        }
    case DSS_LAYOUT:
        return dss_layout_insert(handle, (struct layout_info *)item_list, n);
    case DSS_DEVICE:
        switch (action) {
        case DSS_SET_UPDATE:
            return dss_device_update(handle, (struct dev_info *)item_list,
                                     (struct dev_info *) item_list, n, fields);
        case DSS_SET_INSERT:
            return dss_device_insert(handle, (struct dev_info *)item_list, n);
        case DSS_SET_DELETE:
            return dss_device_delete(handle, (struct dev_info *)item_list, n);
        default:
            return -ENOTSUP;
        }
    case DSS_MEDIA:
        switch (action) {
        case DSS_SET_UPDATE:
            return dss_media_update(handle, (struct media_info *)item_list,
                                    (struct media_info *) item_list, n, fields);
        case DSS_SET_INSERT:
            return dss_media_insert(handle, (struct media_info *)item_list, n);
        case DSS_SET_DELETE:
            return dss_media_delete(handle, (struct media_info *)item_list, n);
        default:
            return -ENOTSUP;
        }
    default:
        return -ENOTSUP;
    }
}

static int convert_pid(const char *pid)
{
    int lock_owner = (int) strtoll(pid, NULL, 10);

    if (errno == EINVAL || errno == ERANGE) {
        pho_error(-errno, "Pid couldn't be converted: %s, err = %d",
                  pid, -errno);
        exit(EXIT_FAILURE);
    }

    return lock_owner;
}

int main(int argc, char **argv)
{
    bool with_outer_filter = false;
    struct dss_filter outer_filter;
    struct dss_handle *dss_handle;
    const char *connect_string;
    enum dss_set_action action;
    struct object_info *object;
    struct layout_info *layout;
    bool with_filter = false;
    struct dss_filter filter;
    struct media_info *media;
    struct extent *extents;
    struct dev_info *dev;
    bool oidtest = false;
    enum dss_type type;
    void *item_list;
    int item_cnt;
    int rc;
    int i;
    int j;

    test_env_initialize();

    if (argc < 3 || argc > 5) {
        fprintf(stderr, "Usage: %s ACTION TYPE [ \"CRIT\" ]\n", argv[0]);
        fprintf(stderr, "where  ACTION := { get | set | lock | unlock }\n");
        fprintf(stderr, "       TYPE := "
                        "{ device | media | object | deprec | layout }\n");
        fprintf(stderr, "       [ \"CRIT\" ] := \"field cmp value\"\n");
        fprintf(stderr, "         \"CRIT\" may start by '+' to specify an "
                        "outer filter for layout get\n");
        fprintf(stderr, "Optional for get:\n");
        fprintf(stderr, "       nb item found\n");
        fprintf(stderr, "Optional for set:\n");
        fprintf(stderr, "       oidtest set oid to NULL\n");
        fprintf(stderr, "Optional for lock and unlock:\n");
        fprintf(stderr, "       name of the lock to acquire or release\n");
        exit(1);
    }

    rc = global_setup_dss((void **)&dss_handle);
    if (rc) {
        pho_error(rc, "dss setup failed");
        exit(EXIT_FAILURE);
    }

    if (!strcmp(argv[1], "get")) {
        type = str2dss_type(argv[2]);
        if (type == DSS_INVAL) {
            pho_error(EINVAL,
                      "verb device|media|object|deprec|layout "
                      "expected instead of %s",
                      argv[2]);
            exit(EXIT_FAILURE);
        }

        if (argc >= 4) {
            pho_info("Criteria Filter: %s", argv[3]);
            if (strcmp(argv[3], "all") != 0) {
                if (type == DSS_FULL_LAYOUT &&
                    strstr(argv[3], "DSS::EXT") != NULL) {
                    with_outer_filter = true;
                    rc = dss_filter_build(&outer_filter, "%s", argv[3]);
                    if (rc) {
                        pho_error(rc, "Cannot build DSS filter");
                        exit(EXIT_FAILURE);
                    }
                } else {
                    with_filter = true;
                    rc = dss_filter_build(&filter, "%s", argv[3]);
                    if (rc) {
                        pho_error(rc, "Cannot build DSS filter");
                        exit(EXIT_FAILURE);
                    }
                }
            }
        }

        rc = dss_generic_get(dss_handle, type, with_filter ? &filter : NULL,
                             with_outer_filter ? &outer_filter : NULL,
                             &item_list, &item_cnt);
        if (with_filter)
            dss_filter_free(&filter);
        if (with_outer_filter)
            dss_filter_free(&outer_filter);

        if (rc) {
            pho_error(rc, "dss_get failed");
            exit(EXIT_FAILURE);
        }

        switch (type) {
        case DSS_DEVICE:
            for (i = 0, dev = item_list; i < item_cnt; i++, dev++) {
                pho_debug("Got device: family:%s host:%s model:%s path:%s "
                          "serial:%s adm_st:%s library:%s",
                          rsc_family2str(dev->rsc.id.family), dev->host,
                          dev->rsc.model, dev->path, dev->rsc.id.name,
                          rsc_adm_status2str(dev->rsc.adm_status),
                          dev->rsc.id.library);
            }
            break;
        case DSS_MEDIA:
            for (i = 0, media = item_list; i < item_cnt; i++, media++) {
                pho_debug("Got Media: name:%s model:%s adm_st:%s "
                          "address_type:%s fs_type:%s fs_status:%s library:%s",
                          media->rsc.id.name,
                          media->rsc.model,
                          rsc_adm_status2str(media->rsc.adm_status),
                          address_type2str(media->addr_type),
                          fs_type2str(media->fs.type),
                          fs_status2str(media->fs.status),
                          media->rsc.id.library);
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
        case DSS_DEPREC:
            for (i = 0, object = item_list; i < item_cnt; i++, object++)
                pho_debug("Got object: oid:%s", object->oid);
            break;
        case DSS_FULL_LAYOUT:
            for (i = 0,  layout = item_list; i < item_cnt; i++, layout++) {
                pho_debug("Got layout: "
                          "oid:%s ext_count:%u desc:%s-%d.%d",
                          layout->oid, layout->ext_count,
                          layout->layout_desc.mod_name,
                          layout->layout_desc.mod_major,
                          layout->layout_desc.mod_minor);
                extents = layout->extents;
                for (j = 0; j < layout->ext_count; j++) {
                    pho_debug("->Got extent: layout_idx:%d, state:%s size:%zu,"
                             " address:%s,media type:%s, name:%s, library:%s",
                             extents->layout_idx,
                             extent_state2str(extents->state),
                             extents->size,
                             extents->address.buff,
                             rsc_family2str(extents->media.family),
                             extents->media.name, extents->media.library);
                    extents++;
                }
            }
            break;
        default:
            abort();
        }

        dss_res_free(item_list, item_cnt);

        if (argc >= 5) { /* check item_cnt */
            int target_item_cnt = atoi(argv[4]);

            if (target_item_cnt != item_cnt) {
                pho_error(-EBADMSG,
                          "dss_object_get %s returns %d item(s) whereas %d "
                          "where expected.\n",
                          argv[3], item_cnt, target_item_cnt);

                exit(EXIT_FAILURE);
            }
        }
    } else if (!strcmp(argv[1], "set")) {
        uint64_t fields = 0;

        type = str2dss_type(argv[2]);

        if (type == DSS_INVAL) {
            pho_error(EINVAL,
                      "verb dev|media|object|deprec|layout expected "
                      "instead of %s",
                      argv[2]);
            exit(EXIT_FAILURE);
        }

        action = str2dss_set_action(argv[3]);
        if (argc == 5) {
            if (type == DSS_DEVICE && action == DSS_SET_UPDATE) {
                if (strcmp(argv[4], "adm_status") == 0) {
                    fields = DSS_DEVICE_UPDATE_ADM_STATUS;
                } else if (strcmp(argv[4], "host") == 0) {
                    fields = DSS_DEVICE_UPDATE_HOST;
                } else {
                    pho_error(EINVAL,
                              "invalid update of a device requested, expected adm_status|host, got '%s'",
                              argv[4]);
                    exit(EXIT_FAILURE);
                }
            } else if (strcmp(argv[4], "oidtest") == 0) {
                oidtest = true;
                pho_debug("Switch to oidtest mode (test null oid)");
            }
        }

        rc = dss_generic_get(dss_handle,
                             type == DSS_LAYOUT ? DSS_FULL_LAYOUT : type,
                             NULL, NULL, &item_list,
                             &item_cnt);

        if (rc) {
            pho_error(rc, "dss_get failed");
            exit(EXIT_FAILURE);
        }

        pho_info("Retrieved %d items", item_cnt);

        switch (type) {
        case DSS_DEVICE:
            for (i = 0, dev = item_list; i < item_cnt; i++, dev++) {
                if (action == DSS_SET_INSERT) {
                    assert(strlen(dev->rsc.id.name) + strlen("COPY") <
                           PHO_URI_MAX);
                    strcat(dev->rsc.id.name, "COPY");
                }
                if (fields == DSS_DEVICE_UPDATE_ADM_STATUS)
                    dev->rsc.adm_status = PHO_RSC_ADM_ST_FAILED;
                if (fields == DSS_DEVICE_UPDATE_HOST)
                    dev->host = xstrdup("h0st");
            }
            break;
        case DSS_MEDIA:
            for (i = 0, media = item_list; i < item_cnt; i++, media++) {
                const char *id;
                char *s;

                if (action == DSS_SET_INSERT) {
                    id = media->rsc.id.name;
                    rc = asprintf(&s, "%sCOPY", id);
                    assert(rc > 0);
                    pho_id_name_set(&media->rsc.id, s, "legacy");
                    free(s);
                } else if (action == DSS_SET_UPDATE) {
                    media->stats.nb_obj = 1000;
                    fields |= NB_OBJ_ADD;
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
        case DSS_DEPREC:
            for (i = 0, object = item_list; i < item_cnt; i++, object++) {
                if (action == DSS_SET_INSERT)
                    ++object->version;
            }
            break;
        case DSS_LAYOUT:
            for (i = 0,  layout = item_list; i < item_cnt; i++, layout++) {
                if (action == DSS_SET_INSERT) {
                    /* Only the item from the object table is considered as the
                     * insert request in DSS assumes a layout can be added only
                     * if it comes from a living object
                     */
                    char *s;

                    if (strcmp(layout->oid, "01230123ABC"))
                        continue;

                    rc = asprintf(&s, "%sCOPY", layout->oid);
                    assert(rc > 0);
                    layout->oid = s;
                    item_list = layout;
                    item_cnt = 1;
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

        rc = dss_generic_set(dss_handle, type, item_list, item_cnt, action,
                             fields);
        if (rc) {
            pho_error(rc, "dss_set failed");
            exit(EXIT_FAILURE);
        }
    } else if (!strcmp(argv[1], "lock")) {
        const char *lock_hostname = argc > 3 ? argv[3] : LOCK_HOSTNAME;
        int lock_owner = (argc > 4) ? convert_pid(argv[4]) :
                                      convert_pid(LOCK_OWNER);

        type = str2dss_type(argv[2]);

        if (type != DSS_DEVICE && type != DSS_MEDIA) {
            pho_error(EINVAL, "verb dev expected instead of %s", argv[2]);
            exit(EXIT_FAILURE);
        }

        rc = dss_generic_get(dss_handle, type, NULL, NULL, &item_list,
                             &item_cnt);
        if (rc) {
            pho_error(rc, "dss_get failed");
            exit(EXIT_FAILURE);
        }

        rc = _dss_lock(dss_handle, type, item_list, item_cnt, lock_hostname,
                       lock_owner, false, NULL);
        if (rc) {
            pho_error(rc, "_dss_lock failed");
            exit(EXIT_FAILURE);
        }

    } else if (!strcmp(argv[1], "unlock")) {
        const char *lock_hostname = argc > 3 ? argv[3] : NULL;
        int lock_owner = (argc > 4) ? convert_pid(argv[4]) : 0;

        type = str2dss_type(argv[2]);

        if (type != DSS_DEVICE && type != DSS_MEDIA) {
            pho_error(EINVAL, "verb dev expected instead of %s", argv[2]);
            exit(EXIT_FAILURE);
        }

        rc = dss_generic_get(dss_handle, type, NULL, NULL, &item_list,
                             &item_cnt);
        if (rc) {
            pho_error(rc, "dss_get failed");
            exit(EXIT_FAILURE);
        }

        rc = _dss_unlock(dss_handle, type, item_list, item_cnt, lock_hostname,
                         lock_owner);
        if (rc) {
            pho_error(rc, "_dss_unlock failed");
            exit(EXIT_FAILURE);
        }
    } else {
        pho_error(EINVAL, "verb get|set expected instead of %s", argv[2]);
        exit(EXIT_FAILURE);
    }

    rc = global_teardown_dss((void **)&dss_handle);
    if (rc)
        pho_error(rc, "teardown failed, will not fail the test");

    exit(EXIT_SUCCESS);
}
