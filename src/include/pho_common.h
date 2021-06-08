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
 * \brief  Common tools.
 */
#ifndef _PHO_COMMON_H
#define _PHO_COMMON_H

#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/time.h>
#include <stddef.h>

#include <glib.h>
#include <jansson.h>
#include "pho_types.h"

enum pho_log_level {
    PHO_LOG_DISABLED = 0,
    PHO_LOG_ERROR    = 1,
    PHO_LOG_WARN     = 2,
    PHO_LOG_INFO     = 3,
    PHO_LOG_VERB     = 4,
    PHO_LOG_DEBUG    = 5,
    PHO_LOG_DEFAULT  = PHO_LOG_INFO
};

/**
 * Log record description, as passed to the log handlers. It contains several
 * indications about where and when the message was generated. Note that the
 * plr_message will be free'd after the callback returns.
 *
 * The internal log framework will make sure that positive error codes are
 * delivered in plr_err.
 */
struct pho_logrec {
    enum pho_log_level   plr_level; /**< Level of the log record */
    pid_t                plr_pid;   /**< Pid of the logging process. */
    const char          *plr_file;  /**< Source file where this was emitted */
    const char          *plr_func;  /**< Function name where this was emitted */
    int                  plr_line;  /**< Line number in source code */
    int                  plr_err;   /**< Positive errno code */
    struct timeval       plr_time;  /**< Timestamp */
    char                *plr_msg;   /**< Null terminated log message */
};

/**
 * Receive log messages corresponding to the current log level.
 */
typedef void (*pho_log_callback_t)(const struct pho_logrec *);


/**
 * Update log level. \a level must be any of PHO_LOG_* values or the
 * current level will be reset to PHO_LOG_DEFAULT.
 */
void pho_log_level_set(enum pho_log_level level);

/**
 * Get current log level.
 */
enum pho_log_level pho_log_level_get(void);

/**
 * Register a custom log handler. This will replace the current one, or reset
 * it to its default value if cb is NULL.
 */
void pho_log_callback_set(pho_log_callback_t cb);

/**
 * Internal wrapper, do not call directly!
 * Use the pho_{dbg, msg, err} wrappers below instead.
 *
 * This function will fill a log message structure and pass it down the
 * registered logging callback.
 */
void _log_emit(enum pho_log_level level, const char *file, int line,
               const char *func, int errcode, const char *fmt, ...)
               __attribute__((format(printf, 6, 7)));


#define _PHO_LOG_INTERNAL(_level, _rc, _fmt...)  \
do {                                                                    \
    if ((_level) <= pho_log_level_get())                                \
        _log_emit((_level), __FILE__, __LINE__, __func__, (_rc), _fmt); \
} while (0)

/**
 * Actually exposed logging API for use by the rest of the code. They preserve
 * errno.
 */
#define pho_error(_rc, _fmt...) _PHO_LOG_INTERNAL(PHO_LOG_ERROR, (_rc), _fmt)
#define pho_warn(_fmt...)       _PHO_LOG_INTERNAL(PHO_LOG_WARN, 0, _fmt)
#define pho_info(_fmt...)       _PHO_LOG_INTERNAL(PHO_LOG_INFO, 0, _fmt)
#define pho_verb(_fmt...)       _PHO_LOG_INTERNAL(PHO_LOG_VERB, 0, _fmt)
#define pho_debug(_fmt...)      _PHO_LOG_INTERNAL(PHO_LOG_DEBUG, 0, _fmt)


static inline const char *pho_log_level2str(enum pho_log_level level)
{
    switch (level) {
    case PHO_LOG_DISABLED:  return "DISABLED";
    case PHO_LOG_ERROR:     return "ERROR";
    case PHO_LOG_WARN:      return "WARNING";
    case PHO_LOG_INFO:      return "INFO";
    case PHO_LOG_VERB:      return "VERBOSE";
    case PHO_LOG_DEBUG:     return "DEBUG";
    default: return "???";
    }
}

/**
 * Lighten the code by allowing to set rc and goto a label or return
 * in a single line of code.
 */
#define GOTO(_label, _rc) \
do {                      \
    (void)(_rc);          \
    goto _label;          \
} while (0)

#define LOG_GOTO(_label, _rc, _fmt...) \
do {                                   \
    int _code = (_rc);                 \
    pho_error(_code, _fmt);            \
    goto _label;                       \
} while (0)

#define LOG_RETURN(_rc, _fmt...)   \
    do {                           \
        int _code = (_rc);         \
        pho_error(_code, _fmt);    \
        return _code;              \
    } while (0)

#define ENTRY   pho_debug("ENTERING %s()", __func__)


static inline bool gstring_empty(const GString *s)
{
    return (s == NULL) || (s->len == 0) || (s->str == NULL);
}

#define min(_a, _b) ((_a) < (_b) ? (_a) : (_b))
#define max(_a, _b) ((_a) > (_b) ? (_a) : (_b))

#define abs(_a)     ((_a) < 0 ? -(_a) : (_a))

/**
 * Callback function to parse command output.
 * The function can freely modify line contents
 * without impacting program working.
 *
 * \param[in,out] cb_arg    argument passed to command_call
 * \param[in]     line      the line to be parsed
 * \param[in]     size      size of the line buffer
 * \param[in]     stream    fileno of the stream the line comes from
 */
typedef int (*parse_cb_t)(void *cb_arg, char *line, size_t size, int stream);

/** call a command and call cb_func for each output line. */
int command_call(const char *cmd_line, parse_cb_t cb_func, void *cb_arg);

#define container_of(addr, type, member) ({         \
    const typeof(((type *) 0)->member) * __mptr = (addr);   \
    (type *)((char *) __mptr - offsetof(type, member)); })

/** convert to upper case (in place) */
void upperstr(char *str);

/** convert to lower case (in place) */
void lowerstr(char *str);

/** Return a pointer to the final '\0' character of a string */
static inline char *end_of_string(char *str)
{
    return str + strlen(str);
}

/** remove spaces at end of string */
char *rstrip(char *msg);

/**
 * Converts a string to an int64 with error check.
 * @return value on success, INT64_MIN on error.
 */
int64_t str2int64(const char *str);

/* Number of items in a fixed-size array */
#define ARRAY_SIZE(a)   (sizeof(a) / sizeof((a)[0]))

/**
 * GCC hint for unreachable code
 * See: https://gcc.gnu.org/onlinedocs/gcc/Other-Builtins.html
 */
#define UNREACHED       __builtin_unreachable

/**
 * Type of function for handling retry loops.
 * @param[in]     fnname     Name of the called function.
 * @param[in]     rc         Call status.
 * @param[in,out] retry_cnt  Current retry credit.
 *                           Set to negative value to exit the retry loop.
 * @param[in,out] context    Custom context for retry function.
 */
typedef void(*retry_func_t)(const char *fnname, int rc, int *retry_cnt,
                            void *context);

/** Manage retry loops */
#define PHO_RETRY_LOOP(_rc, _retry_func, _udata, _retry_cnt, _call_func, ...) \
    do {                                         \
        int retry = (_retry_cnt);                \
        do {                                     \
            (_rc) = (_call_func)(__VA_ARGS__);   \
            (_retry_func)(#_call_func, (_rc), &retry, (_udata)); \
        } while (retry >= 0);                    \
    } while (0)


/**
 * Phobos-specific type to iterate over a GLib hashtable and stop on error.
 * Propagate the error back.
 */
typedef int (*pho_ht_iter_cb_t)(const void *, void *, void *);

int pho_ht_foreach(GHashTable *ht, pho_ht_iter_cb_t cb, void *data);

/**
 * Handy macro to quickly replicate a structure
 */
#define MEMDUP(_x)  g_memdup((_x), sizeof(*(_x)))

/**
 * Identify medium-global error codes.
 * Typically useful to trigger custom procedures when a medium becomes
 * read-only.
 */
static inline bool is_medium_global_error(int errcode)
{
    return errcode == -ENOSPC || errcode == -EROFS || errcode == -EDQUOT;
}

/**
 * Get short host name once (/!\ not thread-safe).
 *
 * (only the first local part of the FQDN is returned)
 */
const char *get_hostname(void);

/**
 * Get allocated short host name (/!\ not thread-safe)
 *
 * (only the first local part of the FQDN is returned)
 *
 * @param[out] hostname Self hostname is returned (or NULL on failure)
 *
 * @return              0 on success,
 *                      -errno on failure.
 */
int get_allocated_hostname(char **hostname);

/**
 * Compare trimmed strings
 */
int cmp_trimmed_strings(const char *first, const char *second);

#endif
