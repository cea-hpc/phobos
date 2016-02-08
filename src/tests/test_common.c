/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 * Copyright 2014-2015 CEA/DAM. All Rights Reserved.
 */
/**
 * \brief  Test common tools
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "pho_test_utils.h"
#include "pho_common.h"
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* callback function for parsing */
static int parse_line(void *arg, char *line, size_t size, int stream)
{
    GList   **ctx = (GList **)arg;
    int       len;

    if (line == NULL)
        return -EINVAL;

    len = strnlen(line, size);
    /* terminate the string */
    if (len >= size)
        line[len - 1] = '\0';

    /* remove '\n' */
    if ((len > 0) && (line[len - 1] == '\n'))
        line[len - 1] = '\0';

    *ctx = g_list_append(*ctx, strdup(line));
    return 0;
}

static void print_lines(GList *lines)
{
    GList *l;
    int i = 0;

    /* print the list */
    for (l = lines; l != NULL; l = l->next) {
        i++;
        pho_info("%d: <%s>", i, (char *)l->data);
    }
}


int main(int argc, char **argv)
{
    GList   *lines = NULL;
    int      rc = 0;

    test_env_initialize();

    if (argc != 2) {
        fprintf(stderr, "usage: %s <cmd>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    /** call a command and call cb_func for each output line */
    rc = command_call(argv[1], parse_line, &lines);
    if (rc) {
        fprintf(stderr, "command '%s' return with status %d\n", argv[1], rc);
        exit(EXIT_FAILURE);
    }

    print_lines(lines);
    exit(EXIT_SUCCESS);
}
