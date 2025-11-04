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
 * \brief  LRS Thread management
 */
#ifndef _PHO_LRS_THREAD_H
#define _PHO_LRS_THREAD_H

#include <assert.h>
#include <pthread.h>

#include "pho_dss.h"

/**
 * Thread status.
 */
enum thread_state {
    THREAD_RUNNING = 0,                /**< Thread is currently running. */
    THREAD_STOPPING,                   /**< Thread end was requested. */
    THREAD_STOPPED,                    /**< Thread ended its execution. */
    THREAD_LAST
};

static const char * const thread_state_names[] = {
    [THREAD_RUNNING]  = "running",
    [THREAD_STOPPING] = "stopping",
    [THREAD_STOPPED]  = "stopped",
};

/**
 * Internal state of the thread.
 */
struct thread_info {
    pthread_t          tid;            /**< Thread ID */
    pthread_mutex_t    signal_mutex;   /**< Mutex to protect the signal
                                         *  access.
                                         */
    pthread_cond_t     signal;         /**< Used to signal the thread
                                         *  when new work is available.
                                         */
    _Atomic enum thread_state  state;  /**< Thread status. */
    int                status;         /**< Return status at the end of
                                         *  the execution.
                                         */
    struct dss_handle  dss;            /**< per thread DSS handle */
};

static inline bool thread_is_running(struct thread_info *thread)
{
    return thread->state == THREAD_RUNNING;
}

static inline bool thread_is_stopping(struct thread_info *thread)
{
    return thread->state == THREAD_STOPPING;
}

static inline bool thread_is_stopped(struct thread_info *thread)
{
    return thread->state == THREAD_STOPPED;
}

static inline const char *thread_state2str(struct thread_info *thread)
{
    if (thread->state >= THREAD_LAST || thread->state < 0)
        return "unknown";
    return thread_state_names[thread->state];
}

/**
 * Create and start a thread that will execute \p thread_routine with \p data
 *
 * \return 0 on success, negative error code on failure
 */
int thread_init(struct thread_info *thread, void *(*thread_routine)(void *),
                void *data);

/**
 * Signal the thread
 */
void thread_signal(struct thread_info *thread);

/**
 * Signal to the thread that it should stop working
 *
 * \param[in]  thread  the thread to signal
 */
void thread_signal_stop(struct thread_info *thread);

/**
 * Set the error status on thread and signal that it should stop working
 *
 * \param[in]   thread      thread to signal
 * \param[in]   error_code  error code
 */
void thread_signal_stop_on_error(struct thread_info *thread, int error_code);

/**
 * Make the thread wait for a signal indefinitely
 *
 * \return 0 on success, negative error code on failure
 */
int thread_signal_wait(struct thread_info *thread);

/* On success, it returns:
 * - ETIMEDOUT  the thread received no signal before the timeout
 * - 0          the thread received a signal
 *
 * Negative error codes reported by this function are fatal for the thread.
 */
int thread_signal_timed_wait(struct thread_info *thread, struct timespec *time);

/**
 * Wait for the thread termination
 *
 * thread_signal_stop must be called before this function as this one
 * is blocking.
 *
 * \param[in]  thread  the thread whose termination to wait for
 */
int thread_wait_end(struct thread_info *thread);

#endif
