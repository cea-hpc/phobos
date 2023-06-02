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
 * \brief  Daemon tools.
 */

#ifndef _PHO_DAEMON_H
#define _PHO_DAEMON_H

/**
 * This boolean is initially set to true and only set to false when the process
 * received a SIGINT or SIGTERM
 */
extern bool running;

/* Daemon command line argument parsing */
struct daemon_params {
    int log_level;                      /*!< Logging level. */
    bool is_daemon;                     /*!< True if executed as a daemon. */
    bool use_syslog;                    /*!< True if syslog is requested. */
    char *cfg_path;                     /*!< Configuration file path. */
};

/**
 * Process initialization routine
 *
 * This function parses input args, init global phobos config and starts
 * the daemon fork (if asked).
 *
 * @param[in]       argc            Number of command line arguments
 * @param[in]       argv            Command line arguments
 * @param[out]      param           Processed command line arguments
 * @param[out]      write_pipe_from_child_to_father
 *                                  Pipe descriptor used by daemon mode
 *                                  from the created child daemon to the father
 *                                  process
 * @param[in]       daemon_name     Daemon name used for printing help usage
 *
 * @return 0 if success, else a negative error code
 */
int daemon_creation(int argc, char **argv, struct daemon_params *param,
                    int *write_pipe_from_child_to_father,
                    const char *daemon_name);

/**
 * Init the daemon
 *
 * Signal handler is set. Configuration is loaded. Log level is set.
 *
 * @param[in]   param   Daemon parsed parameters
 *
 * @return 0 if success, else a negative error code
 */
int daemon_init(struct daemon_params param);

/**
 * Finished the daemon initialization
 *
 * Must be called only if we are in daemon mode at the end of the daemon init
 * phase.
 *
 * @param[in] pipefd_to_close   pipe linked with the father process of the
 *                              daemon (used to transfer the initialization
 *                              phase status code that the father process is
 *                              waiting to end)
 * @param[in, out] rc           Status code of the initialization phase.
 *                              Must be checked at output if this function
 *                              fails itself.
 */
void daemon_notify_init_done(int pipefd_to_close, int *rc);


#endif /* _PHO_DAEMON_H */
