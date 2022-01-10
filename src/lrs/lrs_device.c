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
 * \brief  Phobos Local Resource Scheduler (LRS)
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "lrs_device.h"

#include "pho_common.h"

/* XXX: this should probably be alligned with the synchronization timeout */
#define DEVICE_THREAD_WAIT_MS 4000

static inline long ms2sec(long ms)
{
    return ms / 1000;
}

static inline long ms2nsec(long ms)
{
    return (ms % 1000) * 1000000;
}

/* On success, it returns:
 * - ETIMEDOUT  the thread received no signal before the timeout
 * - 0          the thread received a signal
 *
 * Negative error codes reported by this function are fatal for the thread.
 */
static int wait_for_signal(struct lrs_dev *thread)
{
    struct timespec time;
    int rc;

    /* This should not fail */
    rc = clock_gettime(CLOCK_REALTIME, &time);
    if (rc)
        LOG_RETURN(-errno, "clock_gettime: unable to get CLOCK_REALTIME");

    time.tv_sec += ms2sec(DEVICE_THREAD_WAIT_MS);
    time.tv_nsec += ms2nsec(DEVICE_THREAD_WAIT_MS);

    pthread_mutex_lock(&thread->ld_signal_mutex);
    rc = pthread_cond_timedwait(&thread->ld_signal,
                                &thread->ld_signal_mutex,
                                &time);
    pthread_mutex_unlock(&thread->ld_signal_mutex);
    if (rc != ETIMEDOUT)
        rc = -rc;

    return rc;
}

/**
 * Main device thread loop.
 */
static void *lrs_dev_thread(void *tdata)
{
    struct dev_descr *device = (struct dev_descr *)tdata;
    struct lrs_dev *thread = &device->device_thread;

    while (thread->ld_running) {
        int rc;

        rc = wait_for_signal(thread);

        if (rc < 0)
            LOG_GOTO(end_thread, thread->ld_status = rc,
                     "device thread '%s': fatal error",
                     device->dss_dev_info->rsc.id.name);
    }

end_thread:
    pthread_exit(&thread->ld_status);
}

int lrs_dev_init(struct dev_descr *device)
{
    struct lrs_dev *thread = &device->device_thread;
    int rc;

    thread->ld_signal = (pthread_cond_t) PTHREAD_COND_INITIALIZER;
    thread->ld_signal_mutex = (pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER;
    thread->ld_running = true;
    thread->ld_status = 0;

    rc = pthread_create(&thread->ld_tid, NULL, lrs_dev_thread, device);
    if (rc)
        LOG_RETURN(rc, "Could not create device thread");

    return 0;
}

void lrs_dev_fini(struct dev_descr *device)
{
    struct lrs_dev *thread = &device->device_thread;
    int *threadrc = NULL;
    int rc;

    thread->ld_running = false;
    rc = pthread_join(thread->ld_tid, (void **)&threadrc);
    if (rc)
        pho_error(rc, "Error while waiting for device thread");

    if (*threadrc < 0)
        pho_error(*threadrc, "device thread '%s' terminated with error",
                  device->dss_dev_info->rsc.id.name);
}
