/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 * Copyright 2014-2015 CEA/DAM. All Rights Reserved.
 */
/**
 * \brief  Common tools.
 */
#ifndef _PHO_COMMON_H
#define _PHO_COMMON_H

#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <glib.h>
#include <stdio.h>

/* lighten the code by allowing to set rc and goto a label
 * in a single line of code */
#define GOTO(_label, _rc) \
do {                      \
    (void)(_rc);          \
    goto _label;          \
} while (0)

#define log(_fmt...) do {              \
    fprintf(stderr, "%s:%d: ", __func__, __LINE__); \
    fprintf(stderr, _fmt);             \
    fprintf(stderr, "\n");             \
} while (0)

#define LOG_GOTO(_label, _rc, _fmt...) \
do {                      \
    int code = (_rc);     \
    fprintf(stderr, "%s:%d: ", __func__, __LINE__); \
    fprintf(stderr, _fmt);    \
    fprintf(stderr, ": error %d: %s\n", code, \
            strerror(-code));   \
    goto _label;          \
} while (0)

#define LOG_RETURN(_rc, _fmt...)    \
    do {                            \
        int code = (_rc);           \
        fprintf(stderr, "%s:%d: ", __func__, __LINE__); \
        fprintf(stderr, _fmt);       \
        fprintf(stderr, ": error %d: %s\n", code, \
                strerror(-code));   \
        return code;                \
    } while (0)

static inline bool gstring_empty(const GString *s)
{
    return (s == NULL) || (s->len == 0) || (s->str == NULL);
}

#define min(_a, _b)   ((_a) < (_b) ? (_a) : (_b))
#define max(_a, _b)   ((_a) > (_b) ? (_a) : (_b))

/** Callback function to parse command output.
 * The function can freely modify line contents
 * without impacting program working.
 * \param cb_arg argument passed to command_call
 * \param line the line to be parsed
 * \param size size of the line buffer
 */
typedef int (*parse_cb_t)(void *cb_arg, char *line, size_t size);

/** call a command and call cb_func for each output line. */
int command_call(const char *cmd_line, parse_cb_t cb_func, void *cb_arg);

#endif
