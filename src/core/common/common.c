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
 * \brief  Common tools
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "pho_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/utsname.h>
#include <ctype.h>
#include <unistd.h>
#include <assert.h>
#include <uuid/uuid.h>

/**
 * When executing an external processes, two I/O channels are open on its
 * stdout / stderr streams.  Everytime a line is read from these channels
 * we call a user-provided function back.
 */
struct io_chan_arg {
    int              ident;
    parse_cb_t       cb;
    void            *udata;
    struct exec_ctx *exec_ctx;
};

/**
 * GMainLoop exposes a refcount but it is not related to running and stopping
 * the loop. Because we can have several users of the loop (child process
 * termination watcher, stdout watcher, stderr watcher), we need to wait for
 * all of them to complete before calling g_main_loop_quit(). Use custom
 * reference counting for this purpose.
 */
struct exec_ctx {
    GMainLoop   *loop;  /* GMainLoop for the current context */
    int          ref;   /* Pending operations in the loop */
    int          rc;    /* Subprocess termination code (as an errno) */
};


/**
 * Increment reference count (in term of pending operations on the loop)
 */
static inline void ctx_incref(struct exec_ctx *ctx)
{
    ENTRY;

    assert(ctx->ref >= 0);
    ctx->ref++;
}

/**
 * Decrement reference count (in term of pending operations on the loop).
 * Quit the loop but without freeing it if count reaches zero.
 * Allocated will be released by the caller.
 */
static inline void ctx_decref(struct exec_ctx *ctx)
{
    ENTRY;

    assert(ctx->ref > 0);
    if (--ctx->ref == 0)
        g_main_loop_quit(ctx->loop);
}

#ifndef g_spawn_check_wait_status
#define g_spawn_check_wait_status g_spawn_check_exit_status
#endif

/**
 * External process termination handler.
 */
static void watch_child_cb(GPid pid, gint status, gpointer data)
{
    struct exec_ctx *ctx = data;
    const char *error_string;

    pho_debug("Child %d terminated with %d", pid, status);

    if (status != 0) {
        GError *error = NULL;

        g_spawn_check_wait_status(status, &error);
        error_string = error->message;
        ctx->rc = error->code;

        pho_debug("External command failed: %s", error_string);
    }

    g_spawn_close_pid(pid);
    ctx_decref(ctx);
}

/**
 * IO channel watcher.
 * Read one line from the current channel and forward it to the user function.
 *
 * Return true as long as the channel has to stay registered, false otherwise.
 */
static gboolean readline_cb(GIOChannel *channel, GIOCondition cond, gpointer ud)
{
    struct io_chan_arg  *args  = ud;
    GError              *error = NULL;
    gchar               *line;
    gsize                size;
    GIOStatus            res;

    /* The channel is closed, no more data to read */
    if (cond == G_IO_HUP) {
        g_io_channel_unref(channel);
        ctx_decref(args->exec_ctx);
        return false;
    }

    res = g_io_channel_read_line(channel, &line, &size, NULL, &error);
    switch (res) {
    case G_IO_STATUS_EOF:
        g_io_channel_shutdown(channel, true, &error);
        g_error_free(error);
        g_io_channel_unref(channel);
        ctx_decref(args->exec_ctx);

        return false;
    case G_IO_STATUS_ERROR:
        if (error) {
            pho_error(EIO, "Cannot read from child: %s", error->message);
            g_error_free(error);
        } else {
            pho_error(0, "Cannot read from child, but no error was given");
        }

        return false;
    case G_IO_STATUS_NORMAL:
        args->cb(args->udata, line, size, args->ident);
        g_free(line);
    case G_IO_STATUS_AGAIN:
        return true;
    }

     __builtin_unreachable();
    return true;
}

/**
 * Execute synchronously an external command, read its output and invoke
 * a user-provided filter function on every line of it.
 */
int command_call(const char *cmd_line, parse_cb_t cb_func, void *cb_arg)
{
    GSpawnFlags flags = G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD;
    struct exec_ctx ctx = { 0 };
    GError *err_desc = NULL;
    GIOChannel *out_chan;
    GIOChannel *err_chan;
    gchar **av = NULL;
    int p_stdout;
    int p_stderr;
    bool success;
    int rc = 0;
    GPid pid;
    gint ac;

    success = g_shell_parse_argv(cmd_line, &ac, &av, &err_desc);
    if (!success)
        LOG_GOTO(out_err_free, rc = -EINVAL, "Cannot parse '%s': %s",
                 cmd_line, err_desc->message);

    ctx.loop = g_main_loop_new(NULL, false);
    ctx.ref = 0;
    ctx.rc = 0;

    pho_debug("Spawning external command '%s'", cmd_line);

    success = g_spawn_async_with_pipes(NULL,   /* Working dir */
                                       av,     /* Parameters */
                                       NULL,   /* Environment */
                                       flags,  /* Execution directives */
                                       NULL,   /* Child setup function */
                                       NULL,   /* Child setup arg */
                                       &pid,   /* Child PID */
                                       NULL,      /* STDIN (unused) */
                                       cb_func ? &p_stdout : NULL, /* STDOUT */
                                       cb_func ? &p_stderr : NULL, /* STDERR */
                                       &err_desc);
    if (!success)
        LOG_GOTO(out_free, rc = -ECHILD, "Failed to execute '%s': %s",
                 cmd_line, err_desc->message);

    /* register a watcher in the loop, thus increase refcount of our exec_ctx */
    ctx_incref(&ctx);
    g_child_watch_add(pid, watch_child_cb, &ctx);

    if (cb_func != NULL) {
        struct io_chan_arg  out_args = {
            .ident    = STDOUT_FILENO,
            .cb       = cb_func,
            .udata    = cb_arg,
            .exec_ctx = &ctx
        };
        struct io_chan_arg  err_args = {
            .ident    = STDERR_FILENO,
            .cb       = cb_func,
            .udata    = cb_arg,
            .exec_ctx = &ctx
        };

        out_chan = g_io_channel_unix_new(p_stdout);
        err_chan = g_io_channel_unix_new(p_stderr);

        /* instruct the refcount system to close the channels when unused */
        g_io_channel_set_close_on_unref(out_chan, true);
        g_io_channel_set_close_on_unref(err_chan, true);

        /* update refcount for the two watchers */
        ctx_incref(&ctx);
        ctx_incref(&ctx);

        g_io_add_watch(out_chan, G_IO_IN | G_IO_HUP, readline_cb, &out_args);
        g_io_add_watch(err_chan, G_IO_IN | G_IO_HUP, readline_cb, &err_args);
    }

    g_main_loop_run(ctx.loop);

out_free:
    g_main_loop_unref(ctx.loop);
    g_strfreev(av);

out_err_free:
    if (err_desc)
        g_error_free(err_desc);

    return rc ? rc : ctx.rc;
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

int _normalize_path(char *path)
{
    char norm_path[PATH_MAX];
    size_t len_path;
    char *norm_ptr;

    norm_ptr = realpath(path, norm_path);
    if (norm_ptr == NULL)
        LOG_RETURN(-errno, "Failed to normalize path '%s'", path);

    norm_path[PATH_MAX - 1] = '\0';
    len_path = strlen(norm_path);
    if (len_path >= PHO_URI_MAX)
        LOG_RETURN(-EINVAL, "Normalized path is too large '%s'", norm_path);

    memcpy(path, norm_path, len_path);
    path[len_path] = '\0';

    return 0;
}

int64_t str2int64(const char *str)
{
    char     *endptr;
    int64_t   val;

    errno = 0; /* to distinguish success/failure after call */
    val = strtol(str, &endptr, 10);

    /* check various strtoll error cases */
    if ((errno == ERANGE && (val == LLONG_MAX || val == LLONG_MIN))
           || (errno != 0 && val == 0))
        return INT64_MIN;

    /* nothing was read or there are characters after numeric input */
    if (endptr == str || *endptr != '\0') {
        errno = EINVAL;
        return INT64_MIN;
    }

    return val;
}

char *uchar2hex(const unsigned char *buf, int buf_size)
{
    char *hex = xcalloc(buf_size * 2 + 1, sizeof(char));
    int j;
    int k;

    for (j = 0, k = 0; j < buf_size; j++, k += 2) {
        int rc = 0;

        rc = sprintf(hex + k, "%02x", buf[j]);
        if (rc < 0) {
            free(hex);
            pho_error(-EINVAL, "Failed to write in hex buffer");
            errno = EINVAL;
            return NULL;
        }
    }

    return hex;
}

unsigned char *hex2uchar(const char *hex, int uchar_size)
{
    unsigned char *buff = xcalloc(uchar_size + 1, sizeof(char));
    int i;
    int j;

    for (i = 0, j = 0; i < uchar_size; i++, j += 2) {
        unsigned int temp = 0;
        int rc = 0;

        rc = sscanf(hex + j, "%02x", &temp);
        if (rc < 0) {
            free(buff);
            errno = -EINVAL;
            return NULL;
        }
        buff[i] = (unsigned char)temp;
    }
    buff[uchar_size] = '\0';

    return buff;
}

/**
 * GLib HashTable iteration methods lack the ability for the callback to return
 * an error code, to stop the iteration on error and return it to the caller.
 *
 * Implement it once for all here to avoid having atrocious workaround
 * everywhere in the code base.
 */
struct pho_ht_data {
    pho_ht_iter_cb_t     cb;    /**< Callback (supplied by caller) */
    void                *ud;    /**< Callback user data */
    int                  rc;    /**< Return code to propagate first error */
};

static gboolean pho_ht_iter_cb_wrapper(gpointer key, gpointer val, gpointer ud)
{
    struct pho_ht_data  *phd = ud;

    phd->rc = phd->cb(key, val, phd->ud);

    /* return true on error to simulate "finding" and stop iteration */
    return phd->rc ? true : false;
}

int pho_ht_foreach(GHashTable *ht, pho_ht_iter_cb_t cb, void *data)
{
    struct pho_ht_data phd = {.cb = cb, .ud = data, .rc = 0};

    g_hash_table_find(ht, pho_ht_iter_cb_wrapper, &phd);
    return phd.rc;
}

const char *get_hostname()
{
    static struct utsname host_info;
    char *dot;

    if (host_info.nodename[0] != '\0')
        return host_info.nodename;

    if (uname(&host_info) != 0) {
        pho_error(errno, "Failed to get host name");
        return NULL;
    }

    dot = strchr(host_info.nodename, '.');
    if (dot)
        *dot = '\0';

    return host_info.nodename;
}

int get_allocated_hostname(char **hostname)
{
    const char *self_hostname = get_hostname();

    *hostname = NULL;

    if (!self_hostname)
        LOG_RETURN(-EADDRNOTAVAIL, "Unable to get self hostname");

    *hostname = xstrdup(self_hostname);

    return 0;
}

/* Returned pointer is part of the given string, so it shouldn't be freed */
static const char *get_trimmed_string(const char *str, size_t *length)
{
    while (isspace(*str))
        str++;
    if (*str == '\0')
        return NULL;

    *length = strlen(str);
    while (isspace(str[*length - 1]))
        (*length)--;

    return str;
}

int cmp_trimmed_strings(const char *first, const char *second)
{
    size_t second_len;
    size_t first_len;

    first = get_trimmed_string(first, &first_len);
    second = get_trimmed_string(second, &second_len);

    if (first == NULL || second == NULL)
        return -EINVAL;

    if (first_len != second_len)
        return -EINVAL;

    return strncmp(first, second, first_len);
}

int fill_host_owner(const char **hostname, int *pid)
{
    *hostname = get_hostname();
    if (!*hostname)
        return -errno;

    *pid = getpid();
    return 0;
}

int cmp_timespec(const struct timespec *a, const struct timespec *b)
{
    if (a->tv_sec == b->tv_sec) {
        if (a->tv_nsec < b->tv_nsec)
            return -1;
        else if (a->tv_nsec > b->tv_nsec)
            return 1;
        else
            return 0;
    } else if (a->tv_sec < b->tv_sec) {
        return -1;
    } else {
        return 1;
    }
}

bool is_older_or_equal(struct timespec a, struct timespec b)
{
    if (a.tv_sec > b.tv_sec ||
        (a.tv_sec == b.tv_sec && a.tv_nsec > b.tv_nsec))
        return false;

    return true;
}

bool is_past(struct timespec t)
{
    struct timespec now;
    int rc;

    rc = clock_gettime(CLOCK_REALTIME, &now);
    if (rc) {
        pho_error(-errno, "Unable to get CLOCK_REALTIME to check delay");
        return true;
    }

    return is_older_or_equal(t, now);
}

struct timespec add_timespec(const struct timespec *a,
                             const struct timespec *b)
{
    return (struct timespec) {
        .tv_sec = a->tv_sec + b->tv_sec
            + (a->tv_nsec + b->tv_nsec) / 1000000000,
        .tv_nsec = (a->tv_nsec + b->tv_nsec) % 1000000000,
    };
}

struct timespec diff_timespec(const struct timespec *a,
                              const struct timespec *b)
{
    if (a->tv_nsec >= b->tv_nsec)
        return (struct timespec) {
            .tv_sec = a->tv_sec - b->tv_sec,
            .tv_nsec = a->tv_nsec - b->tv_nsec,
        };
    else
        return (struct timespec) {
            .tv_sec = a->tv_sec - (b->tv_sec + 1),
            .tv_nsec = (a->tv_nsec + 1000000000) - b->tv_nsec,
        };
}

char *generate_uuid(void)
{
    uuid_t binary_uuid;
    char *uuid;

    uuid = xmalloc(UUID_LEN);
    uuid_generate_random(binary_uuid);
    uuid_unparse(binary_uuid, uuid);

    return uuid;
}

void pho_buff_alloc(struct pho_buff *buffer, size_t size)
{
    buffer->buff = xmalloc(size);
    buffer->size = size;
}

void pho_buff_realloc(struct pho_buff *buffer, size_t size)
{
    buffer->buff = xrealloc(buffer->buff, size);
    buffer->size = size;
}

void pho_buff_free(struct pho_buff *buffer)
{
    free(buffer->buff);
    buffer->buff = NULL;
    buffer->size = 0;
}
