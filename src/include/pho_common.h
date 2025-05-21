/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2024 CEA/DAM.
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
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/time.h>
#include <stddef.h>
#include <time.h>

#include <glib.h>
#include <jansson.h>
#include "pho_types.h"

#define UUID_LEN 37   // 36 + 1 for the '\0'

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
    pid_t                plr_tid;   /**< Pid of the logging process. */
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
#define pho_assert(condition, fmt...) \
    do {                              \
        if (!(condition)) {           \
            pho_error(0, fmt);        \
            abort();                  \
        }                             \
    } while (0)


#define XWRAPPER(func, args...) ({                                  \
    void *ptr = func(args);                                         \
    if (!ptr) {                                                     \
        pho_error(-errno, #func " failed in '%s', line %d, abort.", \
                  __FILE__, __LINE__);                              \
        abort();                                                    \
    }                                                               \
    ptr; })

#define xmalloc(size)                   XWRAPPER(malloc, size)
#define xcalloc(nmemb, size)            XWRAPPER(calloc, nmemb, size)
#define xstrdup(str)                    XWRAPPER(strdup, str)
#define xstrndup(str, n)                XWRAPPER(strndup, str, n)
#define xrealloc(ptr, size)             XWRAPPER(realloc, ptr, size)
#define xstrdup_safe(str)               ({ (str) ? xstrdup(str) : NULL; })

/* This attribute allows functions redefined in tests to have higher priority
 * when the linker chooses which one to use in the final executable.
 *
 * Example:
 *
 * In lib.c:
 *
 * mockable
 * void myfunc(int a);
 *
 * In test.c
 *
 * void myfunc(int a)
 * {
 * }
 *
 * The function myfunc in test.c will be in the final executable test. Not the
 * one in lib.c
 */
#define mockable  __attribute__((weak))

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

#define MUTEX_LOCK(_mutex)                                                \
do {                                                                      \
    int _rc;                                                              \
    _rc = pthread_mutex_lock((_mutex));                                   \
    if (_rc) {                                                            \
        pho_error(_rc, "Unable to lock '%s'", (#_mutex));                 \
        abort();                                                          \
    }                                                                     \
} while (0)

#define MUTEX_UNLOCK(_mutex)                                              \
do {                                                                      \
    int _rc;                                                              \
    _rc = pthread_mutex_unlock((_mutex));                                 \
    if (_rc) {                                                            \
        pho_error(_rc, "Unable to unlock '%s'", (#_mutex));               \
        abort();                                                          \
    }                                                                     \
} while (0)

enum operation_type {
    PHO_OPERATION_INVALID = -1,
    PHO_LIBRARY_SCAN = 0,
    PHO_LIBRARY_OPEN,
    PHO_DEVICE_LOOKUP,
    PHO_MEDIUM_LOOKUP,
    PHO_DEVICE_LOAD,
    PHO_DEVICE_UNLOAD,
    PHO_LTFS_MOUNT,
    PHO_LTFS_UMOUNT,
    PHO_LTFS_FORMAT,
    PHO_LTFS_DF,
    PHO_LTFS_SYNC,
    PHO_LTFS_RELEASE,
    PHO_OPERATION_LAST,
};

static const char * const OPERATION_TYPE_NAMES[] = {
    [PHO_LIBRARY_SCAN]  = "Library scan",
    [PHO_LIBRARY_OPEN]  = "Library open",
    [PHO_DEVICE_LOOKUP] = "Device lookup",
    [PHO_MEDIUM_LOOKUP] = "Medium lookup",
    [PHO_DEVICE_LOAD]   = "Device load",
    [PHO_DEVICE_UNLOAD] = "Device unload",
    [PHO_LTFS_MOUNT]    = "LTFS mount",
    [PHO_LTFS_UMOUNT]   = "LTFS umount",
    [PHO_LTFS_FORMAT]   = "LTFS format",
    [PHO_LTFS_DF]       = "LTFS df",
    [PHO_LTFS_SYNC]     = "LTFS sync",
    [PHO_LTFS_RELEASE]  = "LTFS release",
};

static inline const char *operation_type2str(enum operation_type op)
{
    if (op >= PHO_OPERATION_LAST || op < 0)
        return NULL;

    return OPERATION_TYPE_NAMES[op];
}

static inline enum operation_type str2operation_type(const char *str)
{
    int i;

    for (i = 0; i < PHO_OPERATION_LAST; i++)
        if (!strcmp(str, OPERATION_TYPE_NAMES[i]))
            return i;

    return PHO_OPERATION_INVALID;
}

/**
 * The logging structure used to insert logs in the database, and retrieve them
 * from it.
 */
struct pho_log {
    struct pho_id device;      /** device the log pertain to */
    struct pho_id medium;      /** medium the log pertain to */
    int error_number;          /** error number in case the log is about a
                                 * failed operation, or 0 if the operation was
                                 * a success
                                 */
    enum operation_type cause; /** the operation that caused the log */
    json_t *message;           /** additional message about the operation */
    struct timeval time;       /** time of the log */
};

static inline void init_pho_log(struct pho_log *log,
                                struct pho_id *device,
                                struct pho_id *medium,
                                enum operation_type cause)
{
    pho_id_copy(&log->device, device);
    pho_id_copy(&log->medium, medium);
    log->cause = cause;
    log->error_number = -1;
    log->message = NULL;
}

static inline void json_insert_element(json_t *json, const char *key,
                                       json_t *value)
{
    if (!value) {
        pho_error(-ENOMEM, "Failed to set '%s' in json", key);
        return;
    }

    if (json_object_set_new(json, key, value) != 0) {
        json_decref(value);
        pho_error(-ENOMEM, "Failed to set '%s' in json", key);
    }
}

static inline bool should_log(struct pho_log *log, enum operation_type action)
{
    switch (action) {
    case PHO_LTFS_MOUNT:
    case PHO_LTFS_UMOUNT:
    case PHO_LTFS_FORMAT:
    case PHO_LTFS_DF:
    case PHO_LTFS_RELEASE:
        return log->message != NULL;
    case PHO_DEVICE_LOAD:
    case PHO_DEVICE_UNLOAD:
        return log->error_number == 0 || json_object_size(log->message) != 0;
    default:
        return json_object_size(log->message) != 0;
    }

    __builtin_unreachable();
}

static inline void destroy_log_message(struct pho_log *log)
{
    if (log->message)
        json_decref(log->message);

    log->message = NULL;
}

/**
 * Filter structure for logs dumping and clearing.
 */
struct pho_log_filter {
    struct pho_id device;
    struct pho_id medium;
    int *error_number;
    enum operation_type cause;
    struct timeval start;
    struct timeval end;
    bool errors;
};

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
#define clamp(_a, _min, _max) min(max((_a), (_min)), (_max))
#define swap(a, b)  \
    ({              \
     typeof(a) tmp; \
     tmp = a;       \
     a = b;         \
     b = tmp;       \
     })


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

/** Normalize a path by resolving all symbolic link */
int _normalize_path(char *path);

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

/**
 * Converts an unsigned char * to a string hex-encoded.
 *
 * @param[in]   str         unsigned char * to encode to hex.
 * @param[in]   str_size    size of the uchar * to encode.
 * @return hex encoded string on success, NULL on failure.
 *         errno is set on error.
 */
char *uchar2hex(const unsigned char *str, int str_size);

/**
 * Converts an hex-encoded string to an unsigned char *.
 *
 * @param[in]   hex         hex string to decode.
 * @param[in]   uchar_size  size of the uchar * to decode.
 * @return hex encoded string on success, NULL on failure.
 *         errno is set on error.
 */
unsigned char *hex2uchar(const char *hex, int uchar_size);


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
            (_rc) = (_call_func)((_udata), ##__VA_ARGS__);   \
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

static inline size_t gcd(size_t a, size_t b)
{
    size_t tmp;

    while (b != 0) {
        tmp = a % b;

        a = b;
        b = tmp;
    }

    return a;
}

static inline size_t lcm(size_t a, size_t b)
{
    return a / gcd(a, b) * b;
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

/**
 * Get allocated short host name and current pid (/!\ not thread-safe)
 *
 * (only the first local part of the FQDN is returned)
 *
 * @param[out] hostname Self hostname is returned (or NULL on failure)
 * @param[out] pid      Self pid is returned
 *
 * @return              0 on success,
 *                      -errno on failure.
 */
int fill_host_owner(const char **hostname, int *pid);

/** Compare two timespecs
 *
 * \return  0 iff a == b
 *         -1 iff a < b
 *          1 iff a > b
 */
int cmp_timespec(const struct timespec *a, const struct timespec *b);

/**
 * Return true if a is older or equal to b
 */
bool is_older_or_equal(struct timespec a, struct timespec b);

/**
 * Return true if t is older than now
 */
bool is_past(struct timespec t);

/** Compute the sum of \p a and \p b and make sure that the result's
 * tv_nsec is lower or equal to 10^9.
 */
struct timespec add_timespec(const struct timespec *a,
                             const struct timespec *b);

/** Compute \p a - \p b. This function assumes that \p a >= \p b */
struct timespec diff_timespec(const struct timespec *a,
                              const struct timespec *b);

struct collection_item;

/** global cached configuration */
struct config {
    const char *cfg_file;              /** pointer to the loaded config file */
    struct collection_item *cfg_items; /** pointer to the loaded configuration
                                         * structure
                                         */
    pthread_mutex_t lock;              /** lock to prevent concurrent load and
                                         * read.
                                         */
};

/* LTFS mocking structure */
struct mock_ltfs {
    /* Overrides default "mkdir" in tests. */
    int (*mock_mkdir)(const char *path, mode_t mode);
    /* Overrides default "command_call" in tests. */
    int (*mock_command_call)(const char *cmd_line, parse_cb_t cb_func,
                             void *cb_arg);
    /* Overrides default "statfs" in tests. */
    int (*mock_statfs)(const char *file, struct statfs *buf);
    /* Overrides default "getxattr" in tests. */
    ssize_t (*mock_getxattr)(const char *path, const char *name, void *value,
                             size_t size);
    /* Overrides default "setxattr" in tests. */
    int (*mock_setxattr)(const char *path, const char *name, const void *value,
                         size_t size, int flags);
};

/**
 * Callback function to mock an ioctl call as used in the SCSI library module
 */
typedef int (*mock_ioctl_t)(int fd, unsigned long request, ...);

/**
 * Structure containing functions mocked by the tests. For testing purposes
 * only.
 */
struct mocking_functions {
    /** LTFS mocking functions used by the module "ldm_fs_ltfs".  */
    struct mock_ltfs mock_ltfs;

    /** Callback to mock the ioctl call used by the ldm module "ldm_lib_scsi" to
     * interact with the tape library.
     */
    mock_ioctl_t mock_ioctl;

    /** Callback to generate an error in multi-put with partial releases
     * settings.
     */
    int (*mock_failure_after_second_partial_release)(void);
};

/**
 * Structure containing global information about Phobos. This structure is
 * shared between all threads and modules.
 *
 * /!\ It is not guaranteed that accessing elements of this structure will be
 * thread safe.
 */
struct phobos_global_context {
    /** Number of references of this global context */
    int pgc_refcount;

    /** Content of Phobos' configuration file */
    struct config config;
    /** Minimum level of logs to display */
    enum pho_log_level log_level;
    /** Callback used when writing logs */
    pho_log_callback_t log_callback;
    /** Whether to display additional information on each logs.  */
    bool log_dev_output;
    /** Mutex to serialize library SCSI requests */
    pthread_mutex_t ldm_lib_scsi_mutex;
    /** Media cache used by the LRS to share the media between threads and avoid
     * too many DSS requests.
     */
    struct pho_cache *lrs_media_cache[PHO_RSC_LAST];

    /* /!\ The following field is for testing purposes only /!\ */
    struct mocking_functions mocks;
};

/**
 * Initialize the phobos_global_context structure. Must be called before any
 * other phobos function or module loading routine.
 */
int pho_context_init(void);

/**
 * Release the phobos_global_context structure. Once called, no phobos function
 * or module loading routine should be called unless pho_context_init is called
 * again.
 */
void pho_context_fini(void);

/**
 * Return a pointer to the global context of Phobos. Shared between modules and
 * threads.
 *
 * /!\ It is not common with dynamically loaded modules and must
 * therefore be passed to load_module when loading a module.
 */
struct phobos_global_context *phobos_context(void);

/**
 * This function should be called from the code inside the module's library.
 *
 * \param[in] context  global context returned by phobos_context()
 *
 * /!\ the context argument must be retrieved by calling phobos_context() from a
 * function defined where the global context is already properly initialized.
 * In practice, the context will be retrieved from the main executable's source
 * code (e.g. Python binding code for the CLI, LRS' code, individual test
 * scripts, ...
 */
void phobos_module_context_set(struct phobos_global_context *context);

void pho_context_reset_mock_functions(void);

/**
 * Generate a unparsed UUID and return it as a string
 */
char *generate_uuid(void);

#endif
