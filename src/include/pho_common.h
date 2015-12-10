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
               const char *func, int errcode, const char *fmt, ...);


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
do {                        \
    int _code = (_rc);      \
    pho_error(_code, _fmt); \
    goto _label;            \
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

/** Callback function to parse command output.
 * The function can freely modify line contents
 * without impacting program working.
 * \param cb_arg argument passed to command_call
 * \param line the line to be parsed
 * \param size size of the line buffer
 */
typedef int (*parse_cb_t)(void *cb_arg, char *line, size_t size);

/** Common callback for command_call: concatenate a command output. */
int collect_output(void *cb_arg, char *line, size_t size);

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

#endif
