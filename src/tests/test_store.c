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

static void dump_md(void *udata, const struct pho_xfer_desc *desc, int rc)
{
    GString *str = g_string_new("");

    pho_attrs_to_json(&desc->xd_attrs, str, 0);
    printf("%s\n", str->str);
    g_string_free(str, TRUE);
}

int main(int argc, char **argv)
{
    int i;
    int rc;

    test_env_initialize();

    if (argc < 3) {
        fprintf(stderr, "usage: %s put <file> <...>\n", argv[0]);
        fprintf(stderr, "       %s mput <file> <...>\n", argv[0]);
        fprintf(stderr, "       %s tag-put <file> <tag> <...>\n", argv[0]);
        fprintf(stderr, "       %s get <id> <dest>\n", argv[0]);
        fprintf(stderr, "       %s getmd <id>\n", argv[0]);
        exit(1);
    }

    if (!strcmp(argv[1], "put")) {
        struct pho_xfer_desc    xfer = {0};
        struct pho_attrs        attrs = {0};

        rc = pho_attr_set(&attrs, "program", argv[0]);
        if (rc)
            exit(EXIT_FAILURE);

        for (i = 2; i < argc; i++) {
            xfer_desc_open_path(&xfer, argv[i], PHO_XFER_OP_PUT, 0);
            xfer.xd_objid = realpath(argv[i], NULL);
            xfer.xd_family = PHO_RSC_INVAL;
            xfer.xd_attrs = attrs;

            rc = phobos_put(&xfer, 1, NULL, NULL);
            xfer_desc_close_fd(&xfer);
            if (rc)
                pho_error(rc, "PUT '%s' failed", argv[i]);
        }

        pho_attrs_free(&attrs);
    } else if (!strcmp(argv[1], "mput")) {
        struct pho_xfer_desc     *xfer;
        struct pho_attrs          attrs = {0};
        int                       xfer_cnt = argc - 2;

        xfer = calloc(xfer_cnt, sizeof(*xfer));
        assert(xfer != NULL);

        /* Oh, let's add a tag for the mput case */
        rc = pho_attr_set(&attrs, "program", argv[0]);
        if (rc) {
            free(xfer);
            exit(EXIT_FAILURE);
        }

        argv += 2;
        argc -= 2;

        for (i = 0; i < argc; i++) {
            xfer_desc_open_path(xfer + i, argv[i], PHO_XFER_OP_PUT, 0);
            xfer[i].xd_objid = realpath(argv[i], NULL);
            xfer[i].xd_family = PHO_RSC_INVAL;
            xfer[i].xd_attrs = attrs;
        }

        rc = phobos_put(xfer, xfer_cnt, NULL, NULL);

        for (i = 0; i < argc; i++) {
            xfer_desc_close_fd(xfer + i);
            free(xfer[i].xd_objid);
        }

        if (rc)
            pho_error(rc, "MPUT failed");

        pho_attrs_free(&attrs);
        free(xfer);
    } else if (!strcmp(argv[1], "tag-put")) {
        struct pho_xfer_desc    xfer = {0};
        struct pho_attrs        attrs = {0};

        rc = pho_attr_set(&attrs, "program", argv[0]);
        if (rc)
            exit(EXIT_FAILURE);

        xfer_desc_open_path(&xfer, argv[2], PHO_XFER_OP_PUT, 0);
        xfer.xd_objid = realpath(argv[2], NULL);
        xfer.xd_family = PHO_RSC_INVAL;
        xfer.xd_attrs = attrs;
        xfer.xd_tags.tags = &argv[3];
        xfer.xd_tags.n_tags = argc - 3;

        rc = phobos_put(&xfer, 1, NULL, NULL);
        xfer_desc_close_fd(&xfer);
        if (rc)
            pho_error(rc, "TAG-PUT '%s' failed", argv[2]);

        pho_attrs_free(&attrs);
    } else if (!strcmp(argv[1], "get")) {
        struct pho_xfer_desc    xfer = {0};

        xfer_desc_open_path(&xfer, argv[3], PHO_XFER_OP_GET, 0);
        xfer.xd_objid = argv[2];

        rc = phobos_get(&xfer, 1, NULL, NULL);
        xfer_desc_close_fd(&xfer);
    } else if (!strcmp(argv[1], "getmd")) {
        struct pho_xfer_desc    xfer = {0};

        xfer_desc_open_path(&xfer, NULL, PHO_XFER_OP_GETMD, 0);
        xfer.xd_objid = argv[2];

        rc = phobos_get(&xfer, 1, dump_md, NULL);
        xfer_desc_close_fd(&xfer);
    } else {
        rc = -EINVAL;
        pho_error(rc, "verb put|mput|get|getmd expected at '%s'\n", argv[1]);
    }

    exit(rc ? EXIT_FAILURE : EXIT_SUCCESS);
}
