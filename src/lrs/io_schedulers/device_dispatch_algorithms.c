#include "io_sched.h"

#include "schedulers.h"

#include <math.h>

static int io_scheduler_no_dispatch(struct io_scheduler *io_sched,
                                    GPtrArray *devices)
{
    int i;

    for (i = 0; i < devices->len; i++) {
        struct lrs_dev *device;

        device = g_ptr_array_index(devices, i);

        io_sched->ops.add_device(io_sched, device);
    }

    return 0;
}

int no_dispatch(struct io_sched_handle *io_sched_hdl,
                GPtrArray *devices)
{
    int rc;

    rc = io_scheduler_no_dispatch(&io_sched_hdl->read, devices);
    if (rc)
        return rc;

    rc = io_scheduler_no_dispatch(&io_sched_hdl->write, devices);
    if (rc)
        return rc;

    rc = io_scheduler_no_dispatch(&io_sched_hdl->format, devices);
    if (rc)
        return rc;

    return 0;
}

/* Take devices from \p io_sched by calling io_scheduler_ops::reclaim_device
 * until it has \p nb_devices. Does nothing if the I/O scheduler already has
 * \p nb_devices or less.
 */
static int take_devices(struct io_scheduler *io_sched, GPtrArray *devices,
                        size_t nb_devices)
{
    // TODO need the model argument (fixed in a later patch)
    if (io_sched->devices->len <= nb_devices)
        /* io_sched has no device to give */
        return 0;

    while (io_sched->devices->len > nb_devices) {
        struct lrs_dev *device;
        int rc;

        rc = io_sched->ops.reclaim_device(io_sched, &device);
        if (rc)
            return rc;

        g_ptr_array_add(devices, device);
    }

    return 0;
}

static int give_devices(struct io_scheduler *io_sched, GPtrArray *devices,
                        size_t nb_devices)
{
    if (io_sched->devices->len >= nb_devices)
        /* no device to take */
        return 0;

    while (io_sched->devices->len < nb_devices) {
        struct lrs_dev *device;
        int rc;

        device = g_ptr_array_index(devices, 0);
        g_ptr_array_remove_index(devices, 0);
        rc = io_sched->ops.add_device(io_sched, device);
        if (rc)
            /* Only fatal errors are reported here. The scheduler thread will be
             * stopped, no need to give the devices back to their original
             * schedulers.
             */
            return rc;
    }

    return 0;
}

static size_t count_total_devices(struct io_sched_handle *io_sched_hdl)
{
    return io_sched_hdl->read.devices->len +
           io_sched_hdl->write.devices->len +
           io_sched_hdl->format.devices->len;
}

struct device_repartition {
    size_t nb_reads;
    size_t nb_writes;
    size_t nb_formats;
};

/* Fetch the devices that will be given to other schedulers:
 * - the new devices, they are added at the end of \p new_devices by the upper
 *   layer when a notify add is received
 * - the devices that belong to schedulers which exceed the new repartition
 *   \p repartition
 *
 * \param[in/out]  io_sched_hdl     Handler of the I/O schedulers, some devices
 *                                  may be taken from each I/O scheduler if they
 *                                  have more than the new repartition
 * \param[in]      new_devices      new global list of devices to dispatch
 * \param[in]      repartition      new repartition of devices to follow
 * \param[out]     devices_to_give  list of struct lrs_dev of devices that will
 *                                  be redistributed to schedulers that need
 *                                  them to follow the new repartition
 */
static int fetch_devices_to_give(struct io_sched_handle *io_sched_hdl,
                                 GPtrArray *new_devices,
                                 struct device_repartition *repartition,
                                 GPtrArray *devices_to_give)
{
    size_t i;
    int rc;

    /* Insert new devices into the list. New devices are added at the end of the
     * list.
     */
    /* FIXME this does not work when devices are shared between schedulers */
    for (i = count_total_devices(io_sched_hdl); i < new_devices->len; i++)
        g_ptr_array_add(devices_to_give,
                        g_ptr_array_index(new_devices, i));

    /* Take in excess devices from each scheduler */
    rc = take_devices(&io_sched_hdl->read, devices_to_give,
                      repartition->nb_reads);
    if (rc)
        return rc;

    rc = take_devices(&io_sched_hdl->write, devices_to_give,
                      repartition->nb_writes);
    if (rc)
        return rc;

    rc = take_devices(&io_sched_hdl->format, devices_to_give,
                      repartition->nb_formats);
    if (rc)
        return rc;

    return 0;
}

static int dispatch_devices(struct io_sched_handle *io_sched_hdl,
                            GPtrArray *devices_to_give,
                            struct device_repartition *repartition)
{
    int rc;

    rc = give_devices(&io_sched_hdl->read, devices_to_give,
                      repartition->nb_reads);
    if (rc)
        return rc;

    rc = give_devices(&io_sched_hdl->write, devices_to_give,
                      repartition->nb_writes);
    if (rc)
        return rc;

    rc = give_devices(&io_sched_hdl->format, devices_to_give,
                      repartition->nb_formats);
    if (rc)
        return rc;

    return 0;
}

/* The total number of devices in \p total should not be 0 */
static double compute_weight_diff(double original_weight,
                                  size_t new_number_of_devices,
                                  size_t total_devices)
{
    assert(total_devices != 0);

    return ((double) new_number_of_devices / (double) total_devices) -
        original_weight;
}

/* This function can only be called if we have two devices to allocate. */
static void
set_heaviest_scheduler_devices_to_2(struct io_sched_weights *weights,
                                    struct device_repartition *repartition)
{
    if (weights->read > weights->write) {
        if (weights->read > weights->format)
            repartition->nb_reads = 2;
        else
            repartition->nb_formats = 2;
    } else {
        if (weights->write > weights->format)
            repartition->nb_writes = 2;
        else
            repartition->nb_formats = 2;
    }
}

/* Compute the difference between the weight of each I/O scheduler and their
 * current device repartition. Then, increase by one the number of devices of
 * the scheduler with the lowest negative difference.
 *
 * This function is called until either the total number of allocated devices
 * has reached the number of available devices or every scheduler has reached
 * their max of devices. We want to be sure that this function will always
 * increment the number of allocated devices, otherwise we will loop forever.
 *
 * Let:
 * - N be the total number of devices (\p nb_devices), assume that N > 0;
 * - Nr, Nw and Nf be the current number of devices associated to the read,
 *   write and format schedulers respectively;
 * - Wr, Ww and Wf be the weights of the read, write and format schedulers
 *   respectively (Wr + Ww + Wf = 1).
 * - Δr, Δw and Δf be the weight difference as returned by
 *   compute_weight_diff e.g. Δr = Nr/N - Wr
 *
 * At the point where this function is called, we have:
 *
 *                  N > Nr + Nw + Nf
 *        =>  N/N - 1 > (Nr + Nw + Nf)/N - 1 with Wr + Wf + Ww = 1
 *        =>  N/N - 1 > (Nr/N - Wr) + (Nw/N - Ww) + (Nf/N - Wf)
 *        =>        0 > Δr + Δw + Δf
 *
 * This means that as long as we do not have allocated all the devices, at least
 * one of the weight difference is strictly less than 0 and they can't all be 0.
 * Therefore, this function will increase one of the scheduler's number of
 * allocated devices as long as it is called when N > Nr + Nw + Nf.
 *
 * \param[in]      weights      relative weights of the I/O schedulers
 * \param[in/out]  repartition  current repartition of devices to update
 * \param[in]      nb_devices   total number of devices to distribute
 */
static void
increment_least_favored_scheduler(struct io_sched_weights *weights,
                                  struct device_repartition *repartition,
                                  size_t nb_devices)
{
    double read_diff = compute_weight_diff(weights->read,
                                           repartition->nb_reads,
                                           nb_devices);
    double write_diff = compute_weight_diff(weights->write,
                                            repartition->nb_writes,
                                            nb_devices);
    double format_diff = compute_weight_diff(weights->format,
                                             repartition->nb_formats,
                                             nb_devices);

    if (read_diff < 0 && read_diff < write_diff) {
        if (read_diff < format_diff)
            repartition->nb_reads++;
        else
            repartition->nb_formats++;
    } else if (write_diff < 0) {
        if (write_diff < format_diff)
            repartition->nb_writes++;
        else
            repartition->nb_formats++;
    } else if (format_diff < 0) {
        repartition->nb_formats++;
    } else {
        /* Can they all be positive? */
        assert(read_diff == 0 && write_diff == 0 && format_diff == 0);
    }
}

/* Allocate devices to schedulers according to their weights.
 *
 * - a scheduler without request is given no device.
 * - a scheduler with at least one request has at least one device
 * - the rest of the devices are distributed depending on the weights
 *
 * \param[out] repartition    repartition to compute
 * \param[in]  weights        relative weights of the schedulers between 0 and 1
 * \param[in]  stats          current I/O statistics (only the number of
 *                            requests is used)
 * \param[in]  total_devices  total number of devices. If 0, each scheduler gets
 *                            one device.
 */
static void compute_device_repartition(struct device_repartition *repartition,
                                       struct io_sched_weights *weights,
                                       struct io_stats *stats,
                                       size_t total_devices)
{
    repartition->nb_reads = stats->nb_reads > 0 ?
        floor(1 + weights->read * total_devices) :
        0;
    repartition->nb_writes = stats->nb_writes > 0 ?
        floor(1 + weights->write * total_devices) :
        0;
    repartition->nb_formats = stats->nb_formats > 0 ?
        floor(1 + weights->format * total_devices) :
        0;
}

/* Compute the repartition of devices to I/O schedulers depending on the number
 * of devices, the number of schedulers with at least one request and the
 * relative weights of the I/O schedulers.
 *
 * \param[in]  io_stats       current I/O statistics, only the number of
 *                            requests is used
 * \param[in]  weights        relative weights of the I/O schedulers
 * \param[out] repartition    new repartition of the devices to I/O schedulers
 * \param[in]  nb_devices     total number of devices to allocate
 * \param[in]  nb_schedulers  number of schedulers with at least one request
 */
static void compute_number_of_devices(struct io_stats *io_stats,
                                      struct io_sched_weights *weights,
                                      struct device_repartition *repartition,
                                      size_t nb_devices, size_t nb_schedulers)
{
    /* Each I/O scheduler with at least one request will have 1 device.
     * nb_extra_devices is the number of remaining devices to dispatch.
     */
    size_t nb_extra_devices;

    if (nb_devices == 0) {
        repartition->nb_reads = 0;
        repartition->nb_writes = 0;
        repartition->nb_formats = 0;

        return;
    }

    if (nb_devices < nb_schedulers)
        /* We don't have enough devices, allocate one device to each scheduler
         * which has requests.
         */
        nb_extra_devices = 0;
    else
        nb_extra_devices = nb_devices - nb_schedulers;

    compute_device_repartition(repartition, weights, io_stats,
                               nb_extra_devices);
    /* At this point, each scheduler with at least one request will have at
     * least one device.
     */

    if (nb_devices == 1) {
        /* Each scheduler with at least one request will have the only device
         * available.
         */
        assert(repartition->nb_reads + repartition->nb_writes +
               repartition->nb_formats == nb_schedulers);
    } else if (nb_devices == 2) {
        /* In this case, we want to give two devices to the I/O scheduler with
         * the most requests.
         */
        set_heaviest_scheduler_devices_to_2(weights, repartition);
    } else {
        /* We have at least 3 devices, no scheduler should share devices */
        while (repartition->nb_reads +
               repartition->nb_writes +
               repartition->nb_formats < nb_devices)
            increment_least_favored_scheduler(weights, repartition, nb_devices);
    }
}

struct device_list {
    const char *model;
    GPtrArray  *devices;
};

static int sort_devices_by_model_cmp(const void *lhs, const void *rhs)
{
    struct lrs_dev **deva = (struct lrs_dev **)lhs;
    struct lrs_dev **devb = (struct lrs_dev **)rhs;

    return strcmp((*deva)->ld_dss_dev_info->rsc.model,
                  (*devb)->ld_dss_dev_info->rsc.model);
}

static int
fair_share_number_of_requests_one_model(struct io_sched_handle *io_sched_hdl,
                                        GPtrArray *devices);

int fair_share_number_of_requests(struct io_sched_handle *io_sched_hdl,
                                  GPtrArray *devices)
{
    GArray *device_lists = g_array_new(FALSE, FALSE,
                                       sizeof(struct device_list));
    const char *model_of_previous_device = NULL;
    int rc = 0;
    int i;

    // TODO handle new devices before sort (fixed in a later patch)

    /* When a device is removed, it is directly removed from the corresponding
     * scheduler. We can only have the same amount of devices plus new ones when
     * devices are added to the LRS.
     */
    assert(io_sched_hdl->read.devices->len +
           io_sched_hdl->write.devices->len +
           io_sched_hdl->format.devices->len <= devices->len);

    /* sort devices by model before creating sublists */
    qsort(devices->pdata, devices->len, sizeof(struct lrs_dev *),
          sort_devices_by_model_cmp);

    for (i = 0; i < devices->len; i++) {
        struct lrs_dev *dev = g_ptr_array_index(devices, i);
        struct device_list *current_dev_list;

        if (!model_of_previous_device ||
            strcmp(model_of_previous_device, dev->ld_dss_dev_info->rsc.model)) {
            struct device_list device_list;

            device_list.model = dev->ld_dss_dev_info->rsc.model;
            model_of_previous_device = device_list.model;
            device_list.devices = g_ptr_array_new();
            g_ptr_array_add(device_list.devices, dev);

            g_array_append_val(device_lists, device_list);
            continue;
        }

        current_dev_list = &g_array_index(device_lists,
                                          struct device_list,
                                          device_lists->len - 1);
        g_ptr_array_add(current_dev_list->devices, dev);
    }

    for (i = 0; i < device_lists->len; i++) {
        struct device_list *devs = &g_array_index(device_lists,
                                                  struct device_list,
                                                  i);

        if (!rc)
            rc = fair_share_number_of_requests_one_model(io_sched_hdl,
                                                         devs->devices);

        g_ptr_array_free(devs->devices, TRUE);
    }
    g_array_free(device_lists, TRUE);

    return rc;
}

/* Callback for io_sched_handle::dispatch_devices. This algorithm will compute
 * the relative weight of the I/O schedulers and dispatch devices according to
 * them.
 */
static int
fair_share_number_of_requests_one_model(struct io_sched_handle *io_sched_hdl,
                                        GPtrArray *devices)
{
    struct device_repartition repartition;
    struct io_sched_weights weights;
    GPtrArray *devices_to_give;
    int nb_used_sched = 0;
    int rc;

    devices_to_give = g_ptr_array_new();
    if (!devices_to_give)
        return -ENOMEM;

    nb_used_sched += io_sched_hdl->io_stats.nb_reads > 0 ? 1 : 0;
    nb_used_sched += io_sched_hdl->io_stats.nb_writes > 0 ? 1 : 0;
    nb_used_sched += io_sched_hdl->io_stats.nb_formats > 0 ?  1 : 0;

    if (nb_used_sched == 0)
        /* nothing to do */
        /* We could take all the devices from all the schedulers */
        GOTO(free_devices, rc = 0);

    rc = io_sched_compute_scheduler_weights(io_sched_hdl, &weights);
    if (rc)
        GOTO(free_devices, rc);

    compute_number_of_devices(&io_sched_hdl->io_stats, &weights, &repartition,
                              devices->len, nb_used_sched);

    rc = fetch_devices_to_give(io_sched_hdl, devices, &repartition,
                               devices_to_give);
    if (rc)
        /* fetch_devices_to_give is not expected to fail. If rc is not 0, a
         * system error ocurred (e.g. an allocation failure). Nothing much,
         * can be done in this case so just return the error to the caller.
         */
        GOTO(free_devices, rc);

    if (devices_to_give->len == 0)
        /* nothing to distribute */
        GOTO(free_devices, rc = 0);

    if (devices->len == 1 || devices->len == 2) {
        struct lrs_dev *dev = g_ptr_array_index(devices_to_give, 0);

        if (repartition.nb_reads > 0) {
            rc = io_sched_hdl->read.ops.add_device(&io_sched_hdl->read, dev);
            if (rc)
                GOTO(free_devices, rc);
        }

        if (repartition.nb_writes > 0) {
            rc = io_sched_hdl->write.ops.add_device(&io_sched_hdl->write, dev);
            if (rc)
                GOTO(free_devices, rc);
        }

        if (repartition.nb_formats > 0) {
            rc = io_sched_hdl->format.ops.add_device(&io_sched_hdl->format,
                                                     dev);
            if (rc)
                GOTO(free_devices, rc);
        }

        if (devices->len == 2) {
            dev = g_ptr_array_index(devices_to_give, 1);

            if (repartition.nb_reads == 2) {
                rc = io_sched_hdl->read.ops.add_device(&io_sched_hdl->read,
                                                         dev);
                if (rc)
                    GOTO(free_devices, rc);
            }

            if (repartition.nb_writes == 2) {
                rc = io_sched_hdl->write.ops.add_device(&io_sched_hdl->write,
                                                         dev);
                if (rc)
                    GOTO(free_devices, rc);
            }

            if (repartition.nb_formats == 2) {
                rc = io_sched_hdl->format.ops.add_device(&io_sched_hdl->format,
                                                         dev);
                if (rc)
                    GOTO(free_devices, rc);
            }
        }

        GOTO(free_devices, rc = 0);
    }

    rc = dispatch_devices(io_sched_hdl, devices_to_give, &repartition);
    if (rc)
        GOTO(free_devices, rc);

free_devices:
    g_ptr_array_free(devices_to_give, TRUE);

    return rc;
}
