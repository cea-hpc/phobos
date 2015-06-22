/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 * Copyright 2014-2015 CEA/DAM. All Rights Reserved.
 */
/**
 * \brief  Common tools
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "pho_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#define PHO_LINE_MAX 4096

/** call a command and call cb_func for each output line */
int command_call(const char *cmd_line, parse_cb_t cb_func, void *cb_arg)
{
    FILE *stream;
    char *buff = NULL;
    int   rc = 0;

    stream = popen(cmd_line, "r");
    if (stream == NULL)
        return -errno;

    buff = malloc(PHO_LINE_MAX);
    if (buff == NULL)
        GOTO(close_stream, rc = -errno);

    /* read the next line */
    while (fgets(buff, PHO_LINE_MAX, stream) != NULL) {
        if (cb_func == NULL)
            continue;
        rc = cb_func(cb_arg, buff, PHO_LINE_MAX);
        if (rc)
            goto free_buff;
    }

free_buff:
    free(buff);
close_stream:
    pclose(stream);
    return rc;
}

void upperstr(char *str)
{
    int i = 0;

    for (i = 0; str[i]; i++)
       str[i] = toupper(str[i]);
}

void lowerstr(char *str)
{
    int i = 0;

    for (i = 0; str[i]; i++)
       str[i] = tolower(str[i]);
}
