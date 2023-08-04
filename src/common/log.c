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
 * \brief  Logging facility.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "pho_common.h"
#include <ctype.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/syscall.h>


static void phobos_log_callback_default(const struct pho_logrec *rec);

char *rstrip(char *msg)
{
    int i;

    for (i = strlen(msg) - 1; i >= 0 && isspace(msg[i]); i--)
        msg[i] = '\0';

    return msg;
}

void phobos_log_callback_default(const struct pho_logrec *rec)
{
    char *error_buffer = NULL;
    char *dev_buffer = NULL;
    struct tm time;

    localtime_r(&rec->plr_time.tv_sec, &time);

    /* Running with dev mode adds filename and line number to the output */
    if (phobos_context()->log_dev_output) {
        int rc;

        rc = asprintf(&dev_buffer, " [%u/%s:%s:%d]", rec->plr_tid,
                      rec->plr_func, rec->plr_file, rec->plr_line);
        if (rc < 0)
            dev_buffer = NULL;
    }

    if (rec->plr_err != 0) {
        int rc;

        rc = asprintf(&error_buffer, ": %s (%d)",
                      strerror(rec->plr_err), rec->plr_err);
        if (rc < 0) {
            free(dev_buffer);
            error_buffer = NULL;
        }
    }

    fprintf(stderr, "%04d-%02d-%02d %02d:%02d:%02d.%09ld <%s>%s %s%s\n",
            time.tm_year + 1900, time.tm_mon + 1, time.tm_mday,
            time.tm_hour, time.tm_min, time.tm_sec,
            rec->plr_time.tv_usec * 1000, pho_log_level2str(rec->plr_level),
            dev_buffer ? : "",
            rstrip(rec->plr_msg),
            error_buffer ? : "");

    free(error_buffer);
    free(dev_buffer);
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
        phobos_context()->log_level = level;
        break;

    default:
        phobos_context()->log_level = PHO_LOG_DEFAULT;
        break;
    }

    phobos_context()->log_dev_output = (level == PHO_LOG_DEBUG);
}

enum pho_log_level pho_log_level_get(void)
{
    return phobos_context()->log_level;
}

void pho_log_callback_set(pho_log_callback_t cb)
{
    if (cb == NULL)
        phobos_context()->log_callback = phobos_log_callback_default;
    else
        phobos_context()->log_callback = cb;
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
    rec.plr_tid   = syscall(SYS_gettid);
    rec.plr_file  = file;
    rec.plr_func  = func;
    rec.plr_line  = line;
    rec.plr_err   = abs(errcode);
    gettimeofday(&rec.plr_time, NULL);

    rc = vasprintf(&rec.plr_msg, fmt, args);
    if (rc < 0)
        rec.plr_msg = NULL;

    phobos_context()->log_callback(&rec);
    free(rec.plr_msg);
    va_end(args);

    /* Make sure errno is preserved throughout the call */
    errno = save_errno;
}
