/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2023 CEA/DAM.
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
 * \brief  Daemon utilities (TLC and LRS could be launched as daemon)
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "pho_common.h"
#include "pho_cfg.h"
#include "pho_daemon.h"

/* Daemon running status */
bool running = true;

/**
 * SIGKILL and SIGTERM handler to set global running to false
 *
 * @param[in] signum    signal to manage by the handler
 */
static inline void sa_sigterm(int signum)
{
    running = false;
}

#define DAEMON_PARAMS_DEFAULT {PHO_LOG_INFO, true, false, NULL}

static void print_usage(const char *daemon_name)
{
    printf("usage: %s [-i/--interactive] [-c/--config cfg_file] "
               "[-v/--verbose] [-q/--quiet] [-s/--syslog]\n"
           "\nOptional arguments:\n"
           "    -i,--interactive        execute the daemon in foreground\n"
           "    -c,--config cfg_file    "
                "use cfg_file as the daemon configuration file\n"
           "    -v,--verbose            increase verbose level\n"
           "    -q,--quiet              decrease verbose level\n"
           "    -s,--syslog             print the daemon logs to syslog\n",
           daemon_name);
}

static struct daemon_params parse_args(int argc, char **argv,
                                       const char *daemon_name)
{
    static struct option long_options[] = {
        {"help",        no_argument,       0,  'h'},
        {"interactive", no_argument,       0,  'i'},
        {"config",      required_argument, 0,  'c'},
        {"verbose",     no_argument,       0,  'v'},
        {"quiet",       no_argument,       0,  'q'},
        {"syslog",      no_argument,       0,  's'},
        {0,             0,                 0,  0}
    };
    struct daemon_params param = DAEMON_PARAMS_DEFAULT;

    while (1) {
        int c;

        c = getopt_long(argc, argv, "hic:vqs", long_options, NULL);
        if (c == -1)
            break;

        switch (c) {
        case 'h':
            print_usage(daemon_name);
            exit(EXIT_SUCCESS);
        case 'i':
            param.is_daemon = false;
            break;
        case 'c':
            param.cfg_path = optarg;
            break;
        case 'v':
            ++param.log_level;
            break;
        case 'q':
            --param.log_level;
            break;
        case 's':
            param.use_syslog = true;
            break;
        default:
            print_usage(daemon_name);
            exit(EXIT_FAILURE);
        }
    }

    if (param.log_level < PHO_LOG_DISABLED)
        param.log_level = PHO_LOG_DISABLED;

    if (param.log_level > PHO_LOG_DEBUG)
        param.log_level = PHO_LOG_DEBUG;

    return param;
}

/* Father daemon initialization */
static int init_daemon(pid_t pid, int read_pipe_from_child_to_father)
{
    char *pid_filepath = NULL;
    ssize_t read_rc;
    char buf[16];
    int _errno;
    int fd;
    int rc;

    pid_filepath = getenv("DAEMON_PID_FILEPATH");
    if (!pid_filepath) {
        pho_error(-EINVAL,
                  "DAEMON_PID_FILEPATH env var must be set to init daemon");
        rc = EXIT_FAILURE;
        goto out;
    }

    rc = snprintf(buf, 16, "%d", pid);
    if (rc < 0 || rc >= 16) {
        pho_error(-EIO, "PID of the daemon could not be bufferized");
        rc = EXIT_FAILURE;
        goto out;
    }

    fd = open(pid_filepath, O_WRONLY | O_CREAT, 0666);
    if (fd == -1) {
        pho_error(errno, "cannot open the pid file at '%s'", pid_filepath);
        kill(pid, SIGKILL);
        rc = EXIT_FAILURE;
        goto out;
    }

    rc = write(fd, buf, strlen(buf));
    _errno = -errno;
    close(fd);
    if (rc == -1) {
        kill(pid, SIGKILL);
        pho_error(_errno, "cannot write the pid file at '%s'",
                  pid_filepath);
        rc = EXIT_FAILURE;
        goto out;
    }

    do {
        read_rc = read(read_pipe_from_child_to_father, &rc, sizeof(rc));
    } while (read_rc == -1 && errno == EAGAIN);

    if (read_rc != sizeof(rc))
        rc = EXIT_FAILURE;

out:
    close(read_pipe_from_child_to_father);

    return rc;
}

int daemon_creation(int argc, char **argv, struct daemon_params *param,
                    int *write_pipe_from_child_to_father,
                    const char *daemon_name)
{
    int pipefd[2] = {-1, -1};
    pid_t pid;
    int rc;

    rc = pho_context_init();
    if (rc)
        return rc;

    atexit(pho_context_fini);

    *param = parse_args(argc, argv, daemon_name);

    /* forking type daemon initialization */
    if (param->is_daemon) {
        rc = pipe(pipefd);
        if (rc == -1)
            LOG_RETURN(rc = -errno, "cannot init the communication pipe");

        pid = fork();
        if (pid < 0)
            LOG_RETURN(rc = -errno, "cannot create child process");

        if (pid) {
            close(pipefd[1]);
            exit(init_daemon(pid, pipefd[0]));
        }

        close(pipefd[0]);
        *write_pipe_from_child_to_father = pipefd[1];
    }

    return rc;
}

#define pholog2syslog(lvl) (lvl?2+lvl:lvl)

static void phobos_log_callback_def_with_sys(const struct pho_logrec *rec)
{
    struct tm time;

    localtime_r(&rec->plr_time.tv_sec, &time);

    if (rec->plr_err != 0)
        syslog(pholog2syslog(rec->plr_level),
               "<%s> [%u/%s:%s:%d] %s: %s (%d)",
               pho_log_level2str(rec->plr_level),
               rec->plr_tid, rec->plr_func, rec->plr_file, rec->plr_line,
               rstrip(rec->plr_msg), strerror(rec->plr_err), rec->plr_err);
    else
        syslog(pholog2syslog(rec->plr_level),
               "<%s> [%u/%s:%s:%d] %s",
               pho_log_level2str(rec->plr_level),
               rec->plr_tid, rec->plr_func, rec->plr_file, rec->plr_line,
               rstrip(rec->plr_msg));

}

int daemon_init(struct daemon_params param)
{
    struct sigaction sa;
    int rc;

    /* signal handler */
    sa.sa_handler = sa_sigterm;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    /* Load configuration */
    rc = pho_cfg_init_local(param.cfg_path);
    if (rc && rc != -EALREADY)
        return rc;

    atexit(pho_cfg_local_fini);
    pho_log_level_set(param.log_level);
    if (param.use_syslog)
        pho_log_callback_set(phobos_log_callback_def_with_sys);

    return rc;
}

void daemon_notify_init_done(int pipefd_to_close, int *rc)
{
    sighandler_t current_sigpipe_handler;

    /* disable SIGPIPE */
    current_sigpipe_handler = signal(SIGPIPE, SIG_IGN);
    if (current_sigpipe_handler == SIG_ERR) {
        *rc = -errno;
    } else {
        if (write(pipefd_to_close, rc, sizeof(*rc)) != sizeof(*rc))
            *rc = -errno;

        current_sigpipe_handler = signal(SIGPIPE, current_sigpipe_handler);
        if (current_sigpipe_handler == SIG_ERR)
            *rc = -errno;
    }

    close(pipefd_to_close);
}
