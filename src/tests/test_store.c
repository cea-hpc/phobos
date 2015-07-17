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
#include "pho_common.h"
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <libgen.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static void log_callback(const struct pho_logrec *rec)
{
    struct tm   time;

    localtime_r(&rec->plr_time.tv_sec, &time);
    printf("%04d.%02d.%02d %02d:%02d:%02d.%06ld %s:%s():%d <%s> %s",
           time.tm_year + 1900, time.tm_mon + 1, time.tm_mday,
           time.tm_hour, time.tm_min, time.tm_sec, rec->plr_time.tv_usec * 1000,
           rec->plr_file, rec->plr_func, rec->plr_line,
           pho_log_level2str(rec->plr_level), rec->plr_msg);
    if (rec->plr_err != 0)
        printf(": %s (%d)", strerror(abs(rec->plr_err)), rec->plr_err);
    putchar('\n');
}

int main(int argc, char **argv)
{
    int rc;

    pho_log_level_set(PHO_LOG_DEBUG);
    pho_log_callback_set(log_callback);

    if (argc != 3 && argc != 4) {
        fprintf(stderr, "usage: %s post|put <file>\n", argv[0]);
        fprintf(stderr, "       %s get <id> <dest>\n", argv[0]);
        exit(1);
    }

    if (!strcmp(argv[1], "post") || !strcmp(argv[1], "put")) {
        struct pho_attrs attrs = {0};
        char fullp[PATH_MAX];
        int flags = 0;

        if (!strcmp(argv[1], "put"))
            flags |= PHO_OBJ_REPLACE;

        rc = pho_attr_set(&attrs, "program", argv[0]);
        if (rc)
            return rc;
        rc = phobos_put(realpath(argv[2], fullp), argv[2], flags, &attrs);

        pho_attrs_free(&attrs);
    } else if (!strcmp(argv[1], "get")) {
        rc = phobos_get(argv[2], argv[3], 0);
    } else {
        fprintf(stderr, "verb put|post|get expected at '%s'\n", argv[1]);
        rc = -EINVAL;
    }
    return rc;
}
