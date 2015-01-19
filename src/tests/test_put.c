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

#include "phobos_store.h"
#include <stdlib.h>
#include <stdio.h>
#include <libgen.h>
#include <limits.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    int rc;
    struct pho_attrs attrs = {0};
    char fullp[PATH_MAX];

    if (argc != 2) {
        fprintf(stderr, "usage: %s <file>\n", argv[0]);
        exit(1);
    }

    rc = pho_attr_set(&attrs, "program", argv[0]);
    if (rc)
        return rc;
    rc = phobos_put(realpath(argv[1], fullp), argv[1], 0, &attrs);

    pho_attrs_free(&attrs);
    return rc;
}
