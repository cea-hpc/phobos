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
#include <unistd.h>
#include <assert.h>


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
    GMainLoop   *loop;
    int          ref;
};

static inline void ctx_incref(struct exec_ctx *ctx)
{
    ENTRY;

    assert(ctx->ref >= 0);
    ctx->ref++;
}

static inline void ctx_decref(struct exec_ctx *ctx)
{
    ENTRY;

    assert(ctx->ref > 0);
    if (--ctx->ref == 0)
        g_main_loop_quit(ctx->loop);
}

/**
 * External process termination handler.
 */
static void watch_child_cb(GPid pid, gint status, gpointer data)
{
    struct exec_ctx *ctx = data;
    ENTRY;

    pho_debug("Child %d terminated with %d", pid, status);
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
    struct io_chan_arg  *args = ud;
    GError              *error = NULL;
    gchar               *line;
    gsize                size;
    ENTRY;

    /* The channel is closed, no more data to read */
    if (cond == G_IO_HUP) {
        g_io_channel_unref(channel);
        ctx_decref(args->exec_ctx);
        return false;
    }

    g_io_channel_read_line(channel, &line, &size, NULL, &error);
    if (error != NULL) {
        pho_error(error->code, "Cannot read from child: %s", error->message);
        g_error_free(error);
    } else {
        args->cb(args->udata, line, size, args->ident);
        g_free(line);
    }
    return true;
}

/**
 * Execute synchronously an external command, read its output and invoke
 * a user-provided filter function on every line of it.
 */
int command_call(const char *cmd_line, parse_cb_t cb_func, void *cb_arg)
{
    struct exec_ctx   ctx = { 0 };
    GPid              pid;
    gint              ac;
    gchar           **av = NULL;
    GError           *err_desc = NULL;
    GSpawnFlags       flags = G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD;
    GIOChannel       *out_chan;
    GIOChannel       *err_chan;
    int               p_stdout;
    int               p_stderr;
    bool              success;
    int               rc = 0;

    success = g_shell_parse_argv(cmd_line, &ac, &av, &err_desc);
    if (!success)
        LOG_GOTO(out_err_free, rc = -err_desc->code, "Cannot parse '%s': %s",
                 cmd_line, err_desc->message);

    ctx.loop = g_main_loop_new(NULL, false);
    ctx.ref  = 0;

    pho_debug("Spawning external command '%s'", cmd_line);

    success = g_spawn_async_with_pipes(NULL,   /* Working dir */
                                       av,     /* Parameters */
                                       NULL,   /* Environment */
                                       flags,  /* Execution directives */
                                       NULL,   /* Child setup function */
                                       NULL,   /* Child setup arg */
                                       &pid,   /* Child PID */
                                       NULL,      /* STDIN (unused) */
                                       &p_stdout, /* W STDOUT file desc */
                                       &p_stderr, /* W STDERR file desc */
                                       &err_desc);
    if (!success)
        LOG_GOTO(out_free, rc = -err_desc->code, "Failed to execute '%s': %s",
                 cmd_line, err_desc->message);

    /* register a watcher in the loop, thus increase refcount of our exec_ctx */
    ctx_incref(&ctx);
    g_child_watch_add(pid, watch_child_cb, &ctx);

    if (cb_func != NULL) {
        struct io_chan_arg  out_args = {
            .ident    = STDIN_FILENO,
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

    return rc;
}

int collect_output(void *cb_arg, char *line, size_t size, int stream)
{
    GString **output = cb_arg;
    int       rc = 0;

    switch (stream) {
    case STDIN_FILENO:
        g_string_append_len(output[0], line, size);
        break;
    case STDERR_FILENO:
        g_string_append_len(output[1], line, size);
        break;
    default:
        rc = -EINVAL;
        pho_error(rc, "Non-supported stream index %d", stream);
    }

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
