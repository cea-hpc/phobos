/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 * Copyright 2014-2015 CEA/DAM. All Rights Reserved.
 */
/**
 * \brief  Logging facility.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "pho_common.h"
#include <ctype.h>
#include <stdarg.h>


static void phobos_log_callback_default(const struct pho_logrec *rec);


static enum pho_log_level   phobos_log_level = PHO_LOG_DEFAULT;
static pho_log_callback_t   phobos_log_callback = phobos_log_callback_default;

static bool                 phobos_dev_output;


char *rstrip(char *msg)
{
    int i;

    for (i = strlen(msg) - 1; i >= 0 && isspace(msg[i]); i--)
        msg[i] = '\0';

    return msg;
}

void phobos_log_callback_default(const struct pho_logrec *rec)
{
    struct tm   time;

    localtime_r(&rec->plr_time.tv_sec, &time);

    printf("%04d-%02d-%02d %02d:%02d:%02d.%06ld <%s>",
           time.tm_year + 1900, time.tm_mon + 1, time.tm_mday,
           time.tm_hour, time.tm_min, time.tm_sec,
           rec->plr_time.tv_usec * 1000, pho_log_level2str(rec->plr_level));

    /* Running with dev mode adds filename and line number to the output */
    if (phobos_dev_output)
        printf(" [%s:%s:%d]", rec->plr_func, rec->plr_file, rec->plr_line);

    printf(" %s", rstrip(rec->plr_msg));

    if (rec->plr_err != 0)
        printf(": %s (%d)", strerror(rec->plr_err), rec->plr_err);

    putchar('\n');
}

void pho_log_level_set(enum pho_log_level level)
{
    switch (level) {
    case PHO_LOG_DEBUG:
    case PHO_LOG_VERB:
    case PHO_LOG_INFO:
    case PHO_LOG_WARN:
    case PHO_LOG_ERROR:
    case PHO_LOG_DISABLED:
        phobos_log_level = level;
        break;

    default:
        phobos_log_level = PHO_LOG_DEFAULT;
        break;
    }

    phobos_dev_output = (level == PHO_LOG_DEBUG);
}

enum pho_log_level pho_log_level_get(void)
{
    return phobos_log_level;
}

void pho_log_callback_set(pho_log_callback_t cb)
{
    if (cb == NULL)
        phobos_log_callback = phobos_log_callback_default;
    else
        phobos_log_callback = cb;
}

void _log_emit(enum pho_log_level level, const char *file, int line,
               const char *func, int errcode, const char *fmt, ...)
{
    struct pho_logrec   rec;
    va_list             args;
    int                 save_errno = errno;
    int                 rc;

    va_start(args, fmt);

    rec.plr_level = level;
    rec.plr_file  = file;
    rec.plr_func  = func;
    rec.plr_line  = line;
    rec.plr_err   = abs(errcode);
    gettimeofday(&rec.plr_time, NULL);

    rc = vasprintf(&rec.plr_msg, fmt, args);
    if (rc < 0)
        rec.plr_msg = NULL;

    phobos_log_callback(&rec);
    free(rec.plr_msg);
    va_end(args);

    /* Make sure errno is preserved throughout the call */
    errno = save_errno;
}
