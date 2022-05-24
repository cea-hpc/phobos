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
 * \brief  Phobos Local Resource Thread Management (LRS)
 */

#include "lrs_thread.h"

int thread_init(struct thread_info *thread, void *(*thread_routine)(void *),
                void *data)
{
    pthread_mutex_init(&thread->signal_mutex, NULL);
    pthread_cond_init(&thread->signal, NULL);

    thread->state = THREAD_RUNNING;
    thread->status = 0;

    return pthread_create(&thread->tid, NULL, thread_routine, data);
}

int thread_signal(struct thread_info *thread)
{
    int rc;

    MUTEX_LOCK(&thread->signal_mutex);

    rc = pthread_cond_signal(&thread->signal);
    if (rc)
        pho_error(-rc, "Unable to signal thread");

    MUTEX_UNLOCK(&thread->signal_mutex);

    return -rc;
}

int thread_signal_stop(struct thread_info *thread)
{
    thread->state = THREAD_STOPPING;
    return thread_signal(thread);
}

void thread_signal_stop_on_error(struct thread_info *thread, int error_code)
{
    thread->status = error_code;
    thread_signal_stop(thread);
}

int thread_signal_wait(struct thread_info *thread)
{
    int rc;

    MUTEX_LOCK(&thread->signal_mutex);
    rc = pthread_cond_wait(&thread->signal, &thread->signal_mutex);
    MUTEX_UNLOCK(&thread->signal_mutex);

    return rc;
}

int thread_signal_timed_wait(struct thread_info *thread, struct timespec *time)
{
    int rc;

    MUTEX_LOCK(&thread->signal_mutex);
    rc = pthread_cond_timedwait(&thread->signal, &thread->signal_mutex,
                                time);
    MUTEX_UNLOCK(&thread->signal_mutex);
    if (rc != ETIMEDOUT)
        rc = -rc;

    return rc;
}

int thread_wait_end(struct thread_info *thread)
{
    int *threadrc = NULL;
    int rc;

    rc = pthread_join(thread->tid, (void **)&threadrc);
    assert(rc == 0);

    return *threadrc;
}
