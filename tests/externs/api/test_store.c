/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2022 CEA/DAM.
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

#include "pho_test_utils.h"
#include "pho_test_xfer_utils.h"
#include "phobos_store.h"
#include "pho_common.h"
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <libgen.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static char *concat(char *path, char *suffix)
{
    char *tmp = xmalloc((strlen(path) + strlen(suffix) + 1) * sizeof(*tmp));

    strcpy(tmp, path);
    strcpy(tmp + strlen(path), suffix);

    return tmp;
}

static void cleanup(struct pho_xfer_desc *xfer, char *path)
{
    free(xfer->xd_targets->xt_objid);
    xfer_close_fd(xfer->xd_targets);
    pho_xfer_desc_clean(xfer);

    if (path != NULL)
        free(path);
}

static void free_tags(char **tags, int size)
{
    int i;

    for (i = 0; i < size; i++)
        free(tags[i]);

    free(tags);
}

static void duplicate_tags(char **argv, char ***tags, int size)
{
    char **tmp;
    int i;

    tmp = xmalloc(size * sizeof(*tags));

    for (i = 0; i < size; i++)
        tmp[i] = xstrdup(argv[i]);

    *tags = tmp;
}

static int try_parse_size(const char *arg, ssize_t *size)
{
    int64_t value;

    *size = -1;

    if (access(arg, R_OK) == 0)
        /* argument is a file, not a size */
        return 0;

    errno = 0;
    value = str2int64(arg);
    if (value < 0)
        return errno ? -errno : -EINVAL;

    *size = value;

    return 0;
}

int main(int argc, char **argv)
{
    struct pho_attrs attrs = {0};
    int rc;
    int i;

    test_env_initialize();

    if (argc < 3) {
        // FIXME I don't think put supports multiple files...
        fprintf(stderr, "usage: %s put [<size>] <file> <...>\n", argv[0]);
        fprintf(stderr, "       %s mput <file> <...>\n", argv[0]);
        fprintf(stderr, "       %s tag-put <file> <tag> <...>\n", argv[0]);
        fprintf(stderr, "       %s get <id> <dest>\n", argv[0]);
        fprintf(stderr, "       %s list <id>\n", argv[0]);
        exit(1);
    }

    pho_attr_set(&attrs, "program", argv[0]);

    if (!strcmp(argv[1], "put")) {
        struct pho_xfer_target target = {0};
        struct pho_xfer_desc xfer = {0};
        int first_file = 2;
        ssize_t size;
        char *path;
        int rc2;

        rc2 = try_parse_size(argv[2], &size);
        if (rc2 != 0)
            goto out_attrs;

        if (size != -1)
            first_file++;

        path = realpath(argv[first_file], NULL);
        if (path == NULL) {
            rc = errno;
            goto out_attrs;
        }

        xfer.xd_targets = &target;
        rc = xfer_desc_open_path(&xfer, argv[first_file], PHO_XFER_OP_PUT, 0);
        if (rc < 0) {
            free(path);
            goto out_attrs;
        }

        xfer.xd_params.put.family = PHO_RSC_INVAL;
        xfer.xd_targets->xt_objid = concat(path, "_put");
        xfer.xd_targets->xt_attrs = attrs;
        if (size != -1)
            xfer.xd_targets->xt_size = size;

        pho_log_level_set(PHO_LOG_DEBUG);
        rc = phobos_put(&xfer, 1, NULL, NULL);
        if (rc)
            pho_error(rc, "PUT '%s' failed", argv[first_file]);

        cleanup(&xfer, path);
        goto out;
    } else if (!strcmp(argv[1], "mput")) {
        struct pho_xfer_desc *xfer;
        int xfer_cnt = argc - 2;
        int j;

        xfer = xcalloc(xfer_cnt, sizeof(*xfer));

        for (i = 2, j = 0; i < argc; i++, j++) {
            char *path = realpath(argv[i], NULL);

            if (path == NULL) {
                rc = errno;
                goto out_free_mput;
            }
            xfer[j].xd_targets = xcalloc(1, sizeof(*xfer[j].xd_targets));
            rc = xfer_desc_open_path(xfer + j, argv[i], PHO_XFER_OP_PUT, 0);
            if (rc < 0)
                goto out_free_mput;

            xfer[j].xd_params.put.family = PHO_RSC_INVAL;
            xfer[j].xd_targets->xt_objid = concat(path, "_mput");
            xfer[j].xd_targets->xt_attrs = attrs;

            free(path);
        }

        rc = phobos_put(xfer, xfer_cnt, NULL, NULL);
        if (rc)
            pho_error(rc, "MPUT failed");

out_free_mput:
        for (j--; j >= 0; j--) {
            xfer_close_fd(xfer[j].xd_targets);
            free(xfer[j].xd_targets->xt_objid);
            free(xfer[j].xd_targets->xt_objuuid);
            free(xfer[j].xd_targets);
        }
        free(xfer);
        goto out_attrs;

    } else if (!strcmp(argv[1], "tag-put")) {
        struct pho_xfer_target target = {0};
        struct pho_xfer_desc xfer = {0};
        char **tags;
        char *path;

        duplicate_tags(argv + 3, &tags, argc - 3);

        path = realpath(argv[2], NULL);
        if (path == NULL) {
            rc = errno;
            free_tags(tags, argc - 3);
            goto out_attrs;
        }

        xfer.xd_targets = &target;
        rc = xfer_desc_open_path(&xfer, argv[2], PHO_XFER_OP_PUT, 0);
        if (rc < 0) {
            free(path);
            free_tags(tags, argc - 3);
            goto out_attrs;
        }

        xfer.xd_params.put.family = PHO_RSC_INVAL;
        xfer.xd_params.put.tags.strings = tags;
        xfer.xd_params.put.tags.count = argc - 3;
        xfer.xd_targets->xt_objid = concat(path, "_tag-put");
        xfer.xd_targets->xt_attrs = attrs;

        rc = phobos_put(&xfer, 1, NULL, NULL);
        if (rc)
            pho_error(rc, "TAG-PUT '%s' failed", argv[2]);

        cleanup(&xfer, path);
        goto out;
    } else if (!strcmp(argv[1], "get")) {
        struct pho_xfer_target target = {0};
        struct pho_xfer_desc xfer = {0};

        xfer.xd_targets = &target;
        rc = xfer_desc_open_path(&xfer, argv[3], PHO_XFER_OP_GET, 0);
        if (rc < 0)
            goto out_attrs;

        xfer.xd_targets->xt_objid = argv[2];

        rc = phobos_get(&xfer, 1, NULL, NULL);
        if (rc)
            pho_error(rc, "GET '%s' failed", argv[2]);

        xfer_close_fd(xfer.xd_targets);
    } else if (!strcmp(argv[1], "list")) {
        struct pho_list_filters filters = {0};
        struct object_info *objs;
        int n_objs;

        for (i = 3; i < argc; ++i) {
            filters.res = (const char **) &argv[i];
            filters.n_res = 1;
            filters.is_pattern = true;
            rc = phobos_store_object_list(&filters, DSS_OBJ_ALIVE, &objs,
                                          &n_objs, NULL);
            if (rc) {
                pho_error(rc, "LIST '%s' failed", argv[i]);
                exit(EXIT_FAILURE);
            }

            phobos_store_object_list_free(objs, n_objs);
            if (n_objs != 2 && n_objs != 3)
                pho_error(rc = -EINVAL,
                          "LIST '%s' failed: 2 or 3 results expected, "
                          "retrieved %d", argv[i], n_objs);
        }
    } else {
        rc = -EINVAL;
        pho_error(rc, "verb put|mput|get|list expected at '%s'\n", argv[1]);
    }

out_attrs:
    pho_attrs_free(&attrs);
out:
    exit(rc ? EXIT_FAILURE : EXIT_SUCCESS);
}
