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

#include <glib.h>


enum pho_log_level {
    PHO_LOG_DISABLED = 0,
    PHO_LOG_ERROR    = 1,
    PHO_LOG_INFO     = 2,
    PHO_LOG_VERB     = 3,
    PHO_LOG_DEBUG    = 4,
    PHO_LOG_DEFAULT  = PHO_LOG_INFO
};

/**
 * Log record description, as passed to the log handlers. It contains several
 * indications about where and when the message was generated. Note that the
 * plr_message will be free'd after the callback returns.
 */
struct pho_logrec {
    enum pho_log_level   plr_level;
    const char          *plr_file;
    const char          *plr_func;
    int                  plr_line;
    int                  plr_err;
    struct timeval       plr_time;
    char                *plr_msg;
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
#define pho_dbg(_fmt...)        _PHO_LOG_INTERNAL(PHO_LOG_DEBUG, 0, _fmt)
#define pho_vrb(_fmt...)        _PHO_LOG_INTERNAL(PHO_LOG_VERB, 0, _fmt)
#define pho_nfo(_fmt...)        _PHO_LOG_INTERNAL(PHO_LOG_INFO, 0, _fmt)
#define pho_err(_rc, _fmt...)   _PHO_LOG_INTERNAL(PHO_LOG_ERROR, (_rc), _fmt)


static inline const char *pho_log_level2str(enum pho_log_level level)
{
    switch (level) {
    case PHO_LOG_DEBUG: return "DEBUG";
    case PHO_LOG_VERB:  return "VERBOSE";
    case PHO_LOG_INFO:  return "INFO";
    case PHO_LOG_ERROR: return "ERROR";
    case PHO_LOG_DISABLED:  return "DISABLED";
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
do {                      \
    int _code = (_rc);    \
    pho_err(_code, _fmt); \
    goto _label;          \
} while (0)

#define LOG_RETURN(_rc, _fmt...)   \
    do {                           \
        int _code = (_rc);         \
        pho_err(_code, _fmt);      \
        return _code;              \
    } while (0)


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

/** call a command and call cb_func for each output line. */
int command_call(const char *cmd_line, parse_cb_t cb_func, void *cb_arg);

#endif
