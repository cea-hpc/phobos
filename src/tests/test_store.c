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

#include "pho_test_utils.h"
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

    pho_attrs_to_json(desc->xd_attrs, str, 0);
    printf("%s\n", str->str);
    g_string_free(str, TRUE);
}

int main(int argc, char **argv)
{
    int i;
    int rc;

    test_env_initialize();

    if (argc < 3) {
        fprintf(stderr, "usage: %s post|put <file> <...>\n", argv[0]);
        fprintf(stderr, "usage: %s mpost|mput <file> <...>\n", argv[0]);
        fprintf(stderr, "       %s get <id> <dest>\n", argv[0]);
        fprintf(stderr, "       %s getmd <id>\n", argv[0]);
        exit(1);
    }

    if (!strcmp(argv[1], "post") || !strcmp(argv[1], "put")) {
        struct pho_xfer_desc    xfer = {0};
        struct pho_attrs        attrs = {0};
        enum pho_xfer_flags     flags = 0;
        char                    fullp[PATH_MAX];

        if (!strcmp(argv[1], "put"))
            flags |= PHO_XFER_OBJ_REPLACE;

        rc = pho_attr_set(&attrs, "program", argv[0]);
        if (rc)
            exit(EXIT_FAILURE);

        for (i = 2; i < argc; i++) {
            xfer.xd_objid = realpath(argv[i], fullp);
            xfer.xd_fpath = argv[i];
            xfer.xd_attrs = &attrs;
            xfer.xd_flags = flags;

            rc = phobos_put(&xfer, 1, NULL, NULL);
            if (rc)
                pho_error(rc, "PUT '%s' failed", argv[i]);
        }

        pho_attrs_free(&attrs);
    } else if (!strcmp(argv[1], "mpost") || !strcmp(argv[1], "mput")) {
        struct pho_xfer_desc     *xfer;
        struct pho_attrs          attrs = {0};
        enum pho_xfer_flags       flags = 0;
        int                       xfer_cnt = argc - 2;

        if (!strcmp(argv[1], "mput"))
            flags |= PHO_XFER_OBJ_REPLACE;

        xfer = calloc(xfer_cnt, sizeof(*xfer));
        assert(xfer != NULL);

        rc = pho_attr_set(&attrs, "program", argv[0]);
        if (rc)
            exit(EXIT_FAILURE);

        argv += 2;
        argc -= 2;

        for (i = 0; i < argc; i++) {
            xfer[i].xd_objid = realpath(argv[i], NULL);
            xfer[i].xd_fpath = argv[i];
            xfer[i].xd_attrs = &attrs;
            xfer[i].xd_flags = flags;
        }

        rc = phobos_put(xfer, xfer_cnt, NULL, NULL);
        if (rc)
            pho_error(rc, "MPUT failed");

        pho_attrs_free(&attrs);
    } else if (!strcmp(argv[1], "get")) {
        struct pho_xfer_desc    xfer = {0};

        xfer.xd_objid = argv[2];
        xfer.xd_fpath = argv[3];

        rc = phobos_get(&xfer, 1, NULL, NULL);
    } else if (!strcmp(argv[1], "getmd")) {
        struct pho_xfer_desc    xfer = {0};

        xfer.xd_objid = argv[2];
        xfer.xd_flags = PHO_XFER_OBJ_GETATTR;

        rc = phobos_get(&xfer, 1, dump_md, NULL);
    } else {
        rc = -EINVAL;
        pho_error(rc, "verb put|post|get|getmd expected at '%s'\n", argv[1]);
    }

    exit(rc ? EXIT_FAILURE : EXIT_SUCCESS);
}
