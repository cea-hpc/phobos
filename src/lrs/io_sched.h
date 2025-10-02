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
 * \brief  LRS I/O Scheduler Abstraction
 */
#ifndef _PHO_IO_SCHED_H
#define _PHO_IO_SCHED_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <glib.h>
#include <pthread.h>
#include <stdbool.h>

#include "pho_cfg.h"
#include "pho_common.h"
#include "pho_types.h"
#include "pho_srl_lrs.h"
#include "lrs_device.h"

extern struct pho_config_item cfg_io_sched[];

/** List of I/O scheduler configuration parameters */
enum pho_cfg_params_io_sched {
    PHO_IO_SCHED_FIRST,

    /* lrs parameters */
    PHO_IO_SCHED_read_algo = PHO_IO_SCHED_FIRST,
    PHO_IO_SCHED_write_algo,
    PHO_IO_SCHED_format_algo,
    PHO_IO_SCHED_dispatch_algo,
    PHO_IO_SCHED_ordered_grouped_read,

    PHO_IO_SCHED_LAST
};

enum io_schedulers {
    IO_SCHED_INVAL = -1,
    IO_SCHED_FIFO,
    IO_SCHED_GROUPED_READ,
};

/**
 * Type of device demands made internally by the various I/O scheduling
 * algorithms passed as argument to io_sched_handle::claim_device.
 */
enum io_sched_claim_device_type {
    IO_SCHED_TAKE,     /** Take the device from the scheduler. Used by device
                         * dispatch algorithms.
                         */
    IO_SCHED_EXCHANGE, /** exchange between two devices, used by io_schedulers
                         * to ask for other schedulers' devices when needed.
                         * There is no reason to exchange a device shared
                         * between several schedulers because if a device is
                         * shared, it will be shared between every scheduler.
                         */
    IO_SCHED_BORROW,   /** Borrow a device from another scheduler. Used when a
                         * scheduler needs a device temporarily to perform an
                         * I/O.
                         */

};

union io_sched_claim_device_args {
    struct {
        struct lrs_dev *device;
        const char *technology;
    } take;
    struct {
        struct lrs_dev *desired_device;
        struct lrs_dev *unused_device;
    } exchange; /* exchange is a reserved keyword */
    struct {
        struct lrs_dev *dev;
    } borrow;
};

struct io_sched_handle;
struct io_scheduler;

struct io_scheduler_ops {
    int (*init)(struct io_scheduler *io_sched);

    void (*fini)(struct io_scheduler *io_sched);

    int (*push_request)(struct io_scheduler *io_sched,
                        struct req_container *reqc);

    int (*peek_request)(struct io_scheduler *io_sched,
                        struct req_container **reqc);

    int (*remove_request)(struct io_scheduler *io_sched,
                          struct req_container *reqc);

    int (*get_device_medium_pair)(struct io_scheduler *io_sched,
                                  struct req_container *reqc,
                                  struct lrs_dev **device,
                                  size_t *index);

    int (*retry)(struct io_scheduler *io_sched,
                 struct sub_request *sreq,
                 struct lrs_dev **device);

    int (*requeue)(struct io_scheduler *io_sched,
                   struct req_container *reqc);

    /* Add a device to this I/O scheduler. This device can already be in the
     * I/O scheduler, it is up to this callback to check this.
     *
     * \param[in]  io_sched a valid io_scheduler
     * \param[in]  device   the device to add
     */
    void (*add_device)(struct io_scheduler *io_sched, struct lrs_dev *device);

    /* Return the i-th element of io_scheduler::devices. This function does no
     * bound checking. It is undefined behavior to call this function with \p i
     * greater or equal to \p io_sched->devices->len.
     *
     * This function returns a pointer to a pointer so that the caller can use
     * container_of on the result to get to the outer structure if necessary.
     *
     *
     * \param[in]  io_sched  a valid io_scheduler
     * \param[in]  i         index to return
     *
     * \return               a pointer to a pointer of struct lrs_dev
     */
    struct lrs_dev **(*get_device)(struct io_scheduler *io_sched, size_t i);

    /* Remove a specific device from this I/O scheduler. The device may not be
     * in this I/O scheduler, it is up to this callback to check this.
     *
     * \param[in]  io_sched  a valid io_scheduler
     * \param[in]  device    the device to remove
     *
     * \return               0 on success, negative POSIX error code on failure
     */
    int (*remove_device)(struct io_scheduler *io_sched,
                         struct lrs_dev *device);

    /* Ask the I/O scheduler for a device to remove. The scheduler will choose
     * which device is removed depending on its internal state.
     *
     * TODO claim_device could return a NULL pointer or EBUSY with a device
     * to indicate to the caller that all the devices are in use but one device
     * will be freed later. This would allow the read scheduler to keep a device
     * until all the requests of the currently mounted tape are finished for
     * example.
     *
     * \param[in]     io_sched  a valid io_scheduler
     * \param[in]     type      which type of claim is done
     * \param[in/out] args      arguments depending on the \p type of claim (see
     *                          struct io_sched_claim_device_args)
     *
     * \return        0 on success, negative POSIX error code on failure
     */
    int (*claim_device)(struct io_scheduler *io_sched,
                        enum io_sched_claim_device_type type,
                        union io_sched_claim_device_args *args);
};

#define IO_REQ_ALL (IO_REQ_READ | IO_REQ_WRITE | IO_REQ_FORMAT)

enum io_request_type {
    IO_REQ_READ   = (1 << 0),
    IO_REQ_WRITE  = (1 << 1),
    IO_REQ_FORMAT = (1 << 2),
};

struct io_scheduler {
    struct io_sched_handle *io_sched_hdl; /* reference the I/O scheduler */
    GPtrArray *devices; /* Devices that this handle can use, it may
                         * be a subset of the devices available. Some
                         * or all the devices may be shared between
                         * schedulers.
                         */
    void *private_data;
    struct io_scheduler_ops ops;
    enum io_request_type type;
};

struct io_stats {
    size_t nb_reads;
    size_t nb_writes;
    size_t nb_formats;
};

struct io_sched_handle {
    /**
     * Decide which request should be considered next. This callback will decide
     * from which I/O scheduler the main scheduler should take its request.
     *
     * Each request \p read, \p write and \p format must be returned by
     * peek_request from the corresponding I/O scheduler and can be NULL if
     * there is no request of a given type.
     *
     * \param[in]  io_sched_hdl a valid io_sched_handle
     * \param[in]  read         a read request container
     * \param[in]  write        a write request container
     * \param[in]  format       a format request container
     *
     * \return                the next request to schedule. This function may
     *                        return NULL
     */
    struct req_container *(*next_request)(struct io_sched_handle *io_sched_hdl,
                                          struct req_container *read,
                                          struct req_container *write,
                                          struct req_container *format);

    /**
     * Dispatch devices to I/O schedulers by calling
     * io_scheduler_ops::add_device
     *
     * This function will be called at each iteration of the main scheduler
     * because the list of device can change dynamically but also because the
     * algorithm may dispatch devices differently depending on the system's
     * load.
     *
     * \param[in]  io_sched_hdl  a valid io_sched_handle
     * \param[in]  devices       the list of devices handled by the main
     *                           scheduler
     *
     * \return                   0 on success, negative POSIX error code on
     * failure
     */
    int (*dispatch_devices)(struct io_sched_handle *io_sched_hdl,
                            GPtrArray *devices);

    struct io_scheduler read;
    struct io_scheduler write;
    struct io_scheduler format;
    struct lock_handle *lock_handle;
    struct tsqueue     *response_queue; /* reference to the response queue */
    struct io_stats     io_stats;
    GPtrArray          *global_device_list; /* reference to
                                             * lrs_sched::devices::ldh_devices
                                             */
};

/* I/O Scheduler interface */

/**
 * Initialize the I/O schedulers from the configuration. This function will also
 * initialize the request handlers' internal data by calling
 * io_scheduler_ops::init.
 *
 * The name of each algorithm will be stored in the [io_sched] section from the
 * parameters: read_algo, write_algo and format_algo.
 *
 * \param[out]  io_sched_hdl io_sched_handle structure to initialize
 * \param[in]   family       which family will be handled by the schedulers
 *
 * \return                   0 on success, negative POSIX error code on failure
 */
int io_sched_handle_load_from_config(struct io_sched_handle *io_sched_hdl,
                                     enum rsc_family family);

/**
 * Cleanup the memory used by the io_sched_handle and the request handlers by
 * calling io_scheduler_ops::fini internally.
 *
 * \param[in]  io_sched_hdl  a valid io_sched_handle returned by
 *                           io_sched_handle_load_from_config
 */
void io_sched_fini(struct io_sched_handle *io_sched_hdl);

/**
 * This function will allow the main scheduler to add devices to the handlers.
 * The administrator will be able to choose from a set of heuristics but for now
 * the only one will be to give all the devices to all the schedulers.
 *
 * This function must be called regularly in order to update the status of the
 * devices but also because some heuristics may attribute devices to certain
 * requests dynamically depending on the system's load.
 *
 * \param[in]  io_sched_hdl  a valid I/O scheduler
 * \param[in]  devices       the list of devices owned by this LRS
 *
 * \return               0 on success, negative POSIX error on failure
 */
int io_sched_dispatch_devices(struct io_sched_handle *io_sched_hdl,
                              GPtrArray *devices);

/**
 * Push a new request to the scheduler, the request has to be of the correct
 * type.
 *
 * \param[in]  io_sched_hdl  a valid I/O scheduler
 * \param[in]  reqc          a request container to schedule
 *
 * \return                   0 on success, negative POSIX error on failure
 */
int io_sched_push_request(struct io_sched_handle *io_sched_hdl,
                          struct req_container *reqc);

/**
 * This function will return the next request to handle. This function allows
 * the main scheduler to know when there are no more requests to schedule but
 * also which type of request is to be scheduled next.
 *
 * \param[in]   io_sched_hdl  a valid io_sched_handle
 * \param[out]  reqc          the next request to handle, if NULL, the scheduler
 *                            has no more request to schedule.
 *
 */
int io_sched_peek_request(struct io_sched_handle *io_sched_hdl,
                          struct req_container **reqc);

/**
 * Remove a request from the scheduler.
 *
 * \param[in]  io_sched_hdl   a valid io_sched_handle
 * \param[in]  reqc           a request to remove that was just returned by
 *                            io_sched_peek_request
 *
 * \return    -EINVAL         the request was not found
 */
int io_sched_remove_request(struct io_sched_handle *io_sched_hdl,
                            struct req_container *reqc);

/**
 * Requeue a request. If a request cannot be scheduled immediatly, this function
 * will reschedule the request for later.
 *
 * \param[in]  io_sched_hdl   a valid io_sched_handle
 * \param[in]  reqc           a request to requeue that was just returned by
 *                            io_sched_peek_request
 *
 * \return    -EINVAL         the request was not found
 */
int io_sched_requeue(struct io_sched_handle *io_sched_hdl,
                     struct req_container *reqc);

/**
 * Given a request container as returned by io_sched_peek_request, this function
 * will return a device to use for this request \p dev.
 *
 * If \p is_error is true on read, valid media IDs are stored between index
 * reqc->req->ralloc->n_required and reqc->req->ralloc->n_med_ids - 1 inclusive.
 *
 * \param[in]   io_sched  a valid I/O scheduler
 * \param[in]   reqc      the request container used to generate sub-requests
 * \param[out]  dev       the device that must be used to handle \p sreq. It can
 *                        point to NULL indicating that no device can handle the
 *                        request currently.
 * \param[out]  index     index of the medium to consider in \p reqc, returned
 *                        by the I/O scheduler
 *
 * \return                0 on success, negative POSIX error on failure
 *             -EAGAIN    the scheduler doesn't have enough device to schedule
 *                        more requests.
 *             -ERANGE    this function was called too many times for a request
 *                        (e.g. more than req->ralloc->n_med_ids for a read).
 */
int io_sched_get_device_medium_pair(struct io_sched_handle *io_sched_hdl,
                                    struct req_container *reqc,
                                    struct lrs_dev **dev,
                                    size_t *index);

/*
 * This function is called when the request has already been sent to a device
 * thread but some error occured on the medium at sub_request::medium_index. The
 * caller is asking the scheduler to find a new medium, if possible, for this
 * request. It can also reuse the same medium if sub_request::failure_on_medium
 * is false. The implementer of the associated callback must keep in mind that
 * remove_request has already been called on sub_request::reqc.
 *
 * \param[in]     io_sched  a valid I/O scheduler
 * \param[in/out] sreq      the sub_request to reconsider
 * \param[out]    dev       the device that must be used to handle \p sreq. It
 *                          can point to NULL indicating that no device can
 *                          handle the request currently
 *
 * \return                  0 on success, negative POSIX error on failure
 */
int io_sched_retry(struct io_sched_handle *io_sched_hdl,
                   struct sub_request *sreq,
                   struct lrs_dev **dev);

/* Remove a specific device from the I/O schedulers that own it
 *
 * \param[in]  io_sched_hdl a valid io_sched_handle
 * \param[in]  device       a device to remove
 *
 * \return                  0 on success, negative POSIX error code on failure
 */
int io_sched_remove_device(struct io_sched_handle *io_sched_hdl,
                           struct lrs_dev *device);

/**
 * Claim a device from an io_scheduler. Depending on the type, this function
 * will perform various operations.
 *
 * - IO_SCHED_TAKE: take any device from \p io_sched. This is used by
 *   io_sched_handle::dispatch_devices to take devices back from schedulers.
 *
 * - IO_SCHED_BORROW: borrow a device from \p io_sched. This is useful when a
 *   scheduler needs a tape on a device owned by someone else but does not
 *   intend to keep this device.
 *
 * - IO_SCHED_EXCHANGE: exchange 2 devices between 2 schedulers. The goal of
 *   this feature is to take a new device without modifying the current share of
 *   devices that has been allocated to a scheduler.
 */
int io_sched_claim_device(struct io_scheduler *io_sched,
                          enum io_sched_claim_device_type type,
                          union io_sched_claim_device_args *args);

struct io_sched_weights {
    double read;
    double write;
    double format;
};

/* Compute the weight of each scheduler. This weight will be used by the device
 * dispatching algorithms to choose how many devices to allocate to each I/O
 * scheduler.
 *
 * For now, this function returns the percentage of each type of request. This
 * means that the number of devices allocated to each type of scheduler will
 * directly depend on the number and repartition of requests.
 */
int io_sched_compute_scheduler_weights(struct io_sched_handle *io_sched_hdl,
                                       struct io_sched_weights *weights);

/**
 * Count the number of devices of type \p techno in \p io_sched.
 *
 * \param[in]  io_sched  I/O scheduler whose device to count
 * \param[in]  techno    which technology we want to count
 *
 * \return               the number of devices of technology \p techno
 */
size_t io_sched_count_device_per_techno(struct io_scheduler *io_sched,
                                        const char *techno);

#define IO_SCHED_SECTION_TEMPLATE "io_sched_%s"

int io_sched_cfg_section_name(enum rsc_family family, char **section_name);

#endif
