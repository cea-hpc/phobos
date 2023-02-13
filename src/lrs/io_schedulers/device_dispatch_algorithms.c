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
        device->ld_io_request_type = (IO_REQ_READ |
                                      IO_REQ_WRITE |
                                      IO_REQ_FORMAT);

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

static bool gptr_array_contains(GPtrArray *array, gconstpointer target)
{
    int i;

    for (i = 0; i < array->len; i++)
        if (g_ptr_array_index(array, i) == target)
            return true;

    return false;
}

/* Take devices from \p io_sched by calling io_scheduler_ops::reclaim_device
 * until it has \p target_nb_devices. Does nothing if the I/O scheduler already
 * has \p target_nb_devices or less.
 */
static int take_devices(struct io_scheduler *io_sched,
                        enum io_request_type type,
                        GPtrArray *devices,
                        size_t target_nb_devices,
                        const char *technology)
{
    size_t current_nb_devices;

    current_nb_devices = io_sched_count_device_per_techno(io_sched, technology);

    if (current_nb_devices <= target_nb_devices)
        /* io_sched has no device to give */
        return 0;

    while (current_nb_devices > target_nb_devices) {
        struct lrs_dev *device;
        bool is_shared;
        int rc;

        rc = io_sched->ops.reclaim_device(io_sched, technology, &device);
        if (rc == -ENODEV)
            /* the scheduler may not have a device of this technology to return
             */
            break;

        if (rc)
            return rc;

        is_shared = is_device_shared_between_schedulers(device);
        if (!is_shared ||
            (is_shared && !gptr_array_contains(devices, device))) {
            /* the device is shared, only add it if this is the first time we
             * see it.
             */
            g_ptr_array_add(devices, device);
            device->ld_io_request_type &= ~type;
        }

        current_nb_devices--;
    }

    return 0;
}

/* Give devices from \p devices to \p io_sched until it has \p nb_devices. */
static int give_devices(struct io_scheduler *io_sched,
                        enum io_request_type type,
                        GPtrArray *devices,
                        size_t nb_devices, const char *technology)
{
    size_t current_nb_devices;
    size_t target;

    current_nb_devices = io_sched_count_device_per_techno(io_sched, technology);

    if (current_nb_devices >= nb_devices)
        /* no device to take */
        return 0;

    /* we need nb_devices - current_nb_devices more devices */
    target = io_sched->devices->len + (nb_devices - current_nb_devices);

    if (devices->len < nb_devices - current_nb_devices) {
        pho_error(0,
                  "Not enough devices for repartition. Expected: %lu, got: %d. "
                  "Unexpected state, will abort.",
                  nb_devices - current_nb_devices, devices->len);
        abort();
    }

    while (io_sched->devices->len < target) {
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

        device->ld_io_request_type |= type;
    }

    return 0;
}

struct device_repartition {
    size_t nb_reads;
    size_t nb_writes;
    size_t nb_formats;
};

struct range {
    int min;
    int max;
};

struct device_list {
    const char *technology; /* tape technology (e.g. LTO5, LTO6, ...) */
    GPtrArray  *devices;
    struct range read;
    struct range write;
    struct range format;
};

static size_t device_repartition_total(struct device_repartition *repartition)
{
    return repartition->nb_reads + repartition->nb_writes +
        repartition->nb_formats;
}

/* Fetch the devices that will be given to other schedulers:
 * - the new devices, they are added at the end of \p new_devices by the upper
 *   layer when a notify add is received
 * - the devices that belong to schedulers which exceed the new repartition
 *   \p repartition
 *
 * \param[in/out] io_sched_hdl        Handler of the I/O schedulers, some
 *                                    devices may be taken from each I/O
 *                                    scheduler if they have more than the new
 *                                    repartition
 * \param[in]     devices_to_dispatch new global list of devices to dispatch
 * \param[in]     repartition         new repartition of devices to follow
 * \param[out]    devices_to_give     list of struct lrs_dev of devices that
 *                                    will be redistributed to schedulers that
 *                                    need them to follow the new repartition
 */
static int fetch_devices_to_give(struct io_sched_handle *io_sched_hdl,
                                 GPtrArray *devices_to_dispatch,
                                 struct device_repartition *repartition,
                                 const char *technology,
                                 GPtrArray *devices_to_give)
{
    size_t i;
    int rc;

    /* Insert new devices into the list. New devices are added at the end of the
     * list.
     */
    for (i = 0; i < devices_to_dispatch->len; i++) {
        struct lrs_dev *dev;

        dev = g_ptr_array_index(devices_to_dispatch, i);
        if (!(dev->ld_io_request_type &
            (IO_REQ_READ | IO_REQ_WRITE | IO_REQ_FORMAT)))
            /* device does not belong to any scheduler */
            g_ptr_array_add(devices_to_give, dev);
    }

    /* Take in excess devices from each scheduler */
    rc = take_devices(&io_sched_hdl->read, IO_REQ_READ, devices_to_give,
                      repartition->nb_reads, technology);
    if (rc)
        return rc;

    rc = take_devices(&io_sched_hdl->write, IO_REQ_WRITE, devices_to_give,
                      repartition->nb_writes, technology);
    if (rc)
        return rc;

    rc = take_devices(&io_sched_hdl->format, IO_REQ_FORMAT, devices_to_give,
                      repartition->nb_formats, technology);
    if (rc)
        return rc;

    return 0;
}

static int dispatch_devices(struct io_sched_handle *io_sched_hdl,
                            GPtrArray *devices_to_give,
                            struct device_repartition *repartition,
                            const char *technology)
{
    int rc;

    rc = give_devices(&io_sched_hdl->read, IO_REQ_READ, devices_to_give,
                      repartition->nb_reads, technology);
    if (rc)
        return rc;

    rc = give_devices(&io_sched_hdl->write, IO_REQ_WRITE, devices_to_give,
                      repartition->nb_writes, technology);
    if (rc)
        return rc;

    rc = give_devices(&io_sched_hdl->format, IO_REQ_FORMAT, devices_to_give,
                      repartition->nb_formats, technology);
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

static int max_reached(struct io_stats *stats,
                       struct device_repartition *repartition,
                       struct device_list *device_list)
{
    int res = 0;

    /* we consider that a scheduler without requests is maxed */
    if (repartition->nb_reads == device_list->read.max ||
        stats->nb_reads == 0)
        res |= IO_REQ_READ;
    if (repartition->nb_writes == device_list->write.max ||
        stats->nb_writes == 0)
        res |= IO_REQ_WRITE;
    if (repartition->nb_formats == device_list->format.max ||
        stats->nb_formats == 0)
        res |= IO_REQ_FORMAT;

    return res;
}

/* This function can only be called if we have two devices to allocate. */
static void
set_heaviest_scheduler_devices_to_2(struct device_list *device_list,
                                    struct io_stats *stats,
                                    struct io_sched_weights *weights,
                                    struct device_repartition *repartition)
{
    int sched_max_reached = max_reached(stats, repartition, device_list);

    if (weights->read > weights->write) {
        if (!(sched_max_reached & IO_REQ_READ) &&
            weights->read > weights->format)
            repartition->nb_reads = 2;
        else if (!(sched_max_reached & IO_REQ_FORMAT) &&
                 weights->format > weights->write)
            repartition->nb_formats = 2;
        else if (!(sched_max_reached & IO_REQ_WRITE))
            repartition->nb_writes = 2;
    } else {
        if (!(sched_max_reached & IO_REQ_WRITE) &&
            weights->write > weights->format)
            repartition->nb_writes = 2;
        else if (!(sched_max_reached & IO_REQ_FORMAT) &&
                 weights->format > weights->write)
            repartition->nb_formats = 2;
        else if (!(sched_max_reached & IO_REQ_READ))
            repartition->nb_reads = 2;
    }
}

/* Count the number of devices which reached the maximum of devices that can be
 * allocated to them.
 *
 * \param[in] maxed_scheduler_fields  a bit field of OR-ed enum io_request_type
 *                                    as returned by max_reached
 *
 * \return                            the number of max'ed schedulers: 0, 1, 2
 *                                    or 3
 */
static int count_not_maxed_schedulers(int maxed_scheduler_fields)
{
    return __builtin_popcount((~maxed_scheduler_fields) & IO_REQ_ALL);
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
increment_least_favored_scheduler(struct device_list *device_list,
                                  struct io_sched_weights *weights,
                                  struct device_repartition *repartition,
                                  struct io_stats *stats,
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
    int sched_max_reached = max_reached(stats, repartition, device_list);

    /* Do not consider schedulers which reached their maximum of devices. */
    if (sched_max_reached & IO_REQ_READ && stats->nb_reads > 0) {
        if (read_diff < 0) {
            /* redistribute the weight remaining from read to write and format
             */
            int n = count_not_maxed_schedulers(sched_max_reached);
            /* n will either be 0, 1 or 2. If 0, both if will evaluate to false
             * and we won't divide by 0.
             */
            if (!(sched_max_reached & IO_REQ_WRITE) && stats->nb_writes > 0)
                write_diff += read_diff / n;
            if (!(sched_max_reached & IO_REQ_FORMAT) && stats->nb_formats > 0)
                format_diff += read_diff / n;
        }
        /* put it to 0 to make the assert bellow pass */
        read_diff = 0;
    }
    if (sched_max_reached & IO_REQ_WRITE && stats->nb_writes > 0) {
        if (write_diff < 0) {
            int n = count_not_maxed_schedulers(sched_max_reached);

            if (!(sched_max_reached & IO_REQ_READ) && stats->nb_reads > 0)
                read_diff += write_diff / n;
            if (!(sched_max_reached & IO_REQ_FORMAT) && stats->nb_formats > 0)
                format_diff += write_diff / n;
        }
        write_diff = 0;
    }
    if (sched_max_reached & IO_REQ_FORMAT && stats->nb_formats > 0) {
        if (format_diff < 0) {
            int n = count_not_maxed_schedulers(sched_max_reached);

            if (!(sched_max_reached & IO_REQ_READ) && stats->nb_reads > 0)
                read_diff += format_diff / n;
            if (!(sched_max_reached & IO_REQ_WRITE) && stats->nb_writes > 0)
                write_diff += format_diff / n;
        }
        format_diff = 0;
    }

    if (read_diff >= 0 && write_diff >= 0 && format_diff >= 0) {
        double max_rw = max(read_diff, write_diff);
        /* We add one to max in case the max is 0 to make sure that one
         * scheduler will be negative.
         */
        double max_rwf = max(max_rw, format_diff) + 1;

        /* This can happen when one or two schedulers have reached their maximum
         * number of devices or don't have any requests to handle. If the
         * remaining schedulers have enough devices to fulfill the weight they
         * where associated, we have to give the remaining devices to them until
         * they've reached their max, or we don't have any more devices to give.
         *
         * We remove the max of {read,write,format}_diff so that the smallest
         * positive number becomes the smallest negative number and will be
         * picked by the ifs bellow.
         */
        if (!(sched_max_reached & IO_REQ_READ) && stats->nb_reads > 0)
            read_diff -= max_rwf;
        if (!(sched_max_reached & IO_REQ_WRITE) && stats->nb_writes > 0)
            write_diff -= max_rwf;
        if (!(sched_max_reached & IO_REQ_FORMAT) && stats->nb_formats > 0)
            format_diff -= max_rwf;
    }

    /* increase the repartition of lowest negative weigth */
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
 * \param[in]  total_devices  total number of devices
 */
static void compute_device_repartition(struct device_repartition *repartition,
                                       struct io_sched_weights *weights,
                                       struct io_stats *stats,
                                       struct device_list *device_list,
                                       size_t total_devices)
{
    repartition->nb_reads = stats->nb_reads > 0 ?
        clamp(floor(weights->read * total_devices),
              device_list->read.min,
              device_list->read.max) :
        0;
    repartition->nb_writes = stats->nb_writes > 0 ?
        clamp(floor(weights->write * total_devices),
              device_list->write.min,
              device_list->write.max) :
        0;
    repartition->nb_formats = stats->nb_formats > 0 ?
        clamp(floor(weights->format * total_devices),
              device_list->format.min,
              device_list->format.max) :
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
 * \param[in]  min_devices    number of schedulers with at least one request
 */
static void compute_number_of_devices(struct io_stats *io_stats,
                                      struct io_sched_weights *weights,
                                      struct device_repartition *repartition,
                                      struct device_list *device_list,
                                      size_t min_devices)
{
    size_t nb_devices = device_list->devices->len;

    if (nb_devices == 0) {
        repartition->nb_reads = 0;
        repartition->nb_writes = 0;
        repartition->nb_formats = 0;

        return;
    }

    if (nb_devices < min_devices && nb_devices > 2) {
        /* not enough devices to fulfill the minimal constraints, we will act as
         * if we allocate from 1 to min unless the min is 0 or 1. If it is 0, no
         * device will be allocated. If it is 1, only 1 will.
         */
        device_list->read.max = device_list->read.min;
        /* we will give at least one device if the min is > 0 */
        device_list->read.min = min(1, device_list->read.min);
        device_list->write.max = device_list->write.min;
        device_list->write.min = min(1, device_list->write.min);
        device_list->format.max = device_list->format.min;
        device_list->format.min = min(1, device_list->format.min);
        /* XXX this needs to be restored if we want to cache this information */
    }

    if (nb_devices == 1 || nb_devices == 2) {
        repartition->nb_reads = io_stats->nb_reads > 0 ?
            clamp(1, device_list->read.min, device_list->read.max) : 0;
        repartition->nb_writes = io_stats->nb_writes > 0 ?
            clamp(1, device_list->write.min, device_list->write.max) : 0;
        repartition->nb_formats = io_stats->nb_formats > 0 ?
            clamp(1, device_list->format.min, device_list->format.max) : 0;

        if (nb_devices == 2)
            /* In this case, we want to give two devices to the I/O scheduler
             * with the most requests.
             */
            set_heaviest_scheduler_devices_to_2(device_list, io_stats, weights,
                                                repartition);
    } else {
        compute_device_repartition(repartition, weights, io_stats, device_list,
                                   nb_devices);
        if (device_repartition_total(repartition) > nb_devices) {
            /* we can be greater than nb_devices if the mins are big enough */
            while (device_repartition_total(repartition) > nb_devices) {
                if (repartition->nb_reads > device_list->read.min)
                    repartition->nb_reads--;
                else if (repartition->nb_writes > device_list->write.min)
                    repartition->nb_writes--;
                else if (repartition->nb_formats > device_list->format.min)
                    repartition->nb_formats--;
            }
        }

        /* We have at least 3 devices, no scheduler should share devices */
        while (repartition->nb_reads +
               repartition->nb_writes +
               repartition->nb_formats < nb_devices &&
               max_reached(io_stats, repartition, device_list) !=
               (IO_REQ_READ | IO_REQ_WRITE | IO_REQ_FORMAT))
            increment_least_favored_scheduler(device_list, weights, repartition,
                                              io_stats, nb_devices);
    }
}

static int cfg_technology_get_range(enum rsc_family family,
                                    const char *technology,
                                    const char *minmax,
                                    const char **value)
{
    char *section;
    char *key;
    int rc;

    rc = io_sched_cfg_section_name(family, &section);
    if (rc)
        return rc;

    rc = asprintf(&key, "fair_share_%s_%s", technology, minmax);
    if (rc == -1) {
        rc = -ENOMEM;
        goto free_section;
    }

    rc = pho_cfg_get_val(section, key, value);
    if (rc)
        goto free_key;

free_key:
    free(key);
free_section:
    free(section);

    return rc;
}

static int csv2ints(const char *_input, int values[3])
{
    char *saveptr;
    char *input;
    int rc = 0;
    int i;

    input = strdup(_input);

    for (i = 0; i < 3; i++) {
        char *token = strtok_r(i == 0 ? input : NULL, ",", &saveptr);
        int64_t value;

        if (!token)
            LOG_GOTO(free_input, rc = -EINVAL,
                     "'%s' is not a valid value for fair_share min/max "
                     "parameter", _input);

        value = str2int64(token);
        if (value == INT64_MIN)
            GOTO(free_input, rc = -EINVAL);
        else if (value < 0 || value > INT_MAX)
            GOTO(free_input, rc = -ERANGE);

        values[i] = value;
    }

free_input:
    free(input);

    return rc;
}

static int device_list_init(struct device_list *device_list,
                            struct lrs_dev *dev)
{
    enum rsc_family family = dev->ld_sys_dev_state.lds_family;
    const char *cfg_min;
    const char *cfg_max;
    int min[3] = {0};
    int max[3] = {0};
    int rc;

    device_list->technology = dev->ld_technology;
    device_list->devices = g_ptr_array_new();

    rc = cfg_technology_get_range(family, device_list->technology, "min",
                                  &cfg_min);
    if (rc)
        return rc;

    rc = csv2ints(cfg_min, min);
    if (rc)
        return rc;

    rc = cfg_technology_get_range(family, device_list->technology, "max",
                                  &cfg_max);
    if (rc)
        return rc;

    rc = csv2ints(cfg_max, max);
    if (rc)
        return rc;

    device_list->read.min = min[0];
    device_list->read.max = max[0];
    device_list->write.min = min[1];
    device_list->write.max = max[1];
    device_list->format.min = min[2];
    device_list->format.max = max[2];

    return 0;
}

static void device_list_fini(struct device_list *device_list)
{
    g_ptr_array_free(device_list->devices, TRUE);
}

static gint sort_devices_by_technology_cmp(gconstpointer a, gconstpointer b)
{
    struct lrs_dev **deva = (struct lrs_dev **)a;
    struct lrs_dev **devb = (struct lrs_dev **)b;

    return strcmp((*deva)->ld_technology, (*devb)->ld_technology);
}

/* g_ptr_array_copy is not available in the current version of glib2 */
static GPtrArray *gptr_array_copy(GPtrArray *input)
{
    GPtrArray *copy = g_ptr_array_new();
    int i;

    if (!copy)
        return NULL;

    for (i = 0; i < input->len; i++)
        g_ptr_array_add(copy, g_ptr_array_index(input, i));

    return copy;
}

static int
fair_share_number_of_requests_one_techno(struct io_sched_handle *io_sched_hdl,
                                         struct device_list *device_list);

static size_t count_devices_shared_with(struct io_scheduler *io_sched,
                                        enum io_request_type type)
{
    size_t count = 0;
    int i = 0;

    for (i = 0; i < io_sched->devices->len; i++) {
        struct lrs_dev **dev;

        dev = io_sched->ops.get_device(io_sched, i);
        assert(dev);

        if ((*dev)->ld_io_request_type & type)
            count++;
    }

    return count;
}

static size_t count_distinct_devices(struct io_sched_handle *io_sched_hdl)
{
    size_t count = io_sched_hdl->read.devices->len;

    count += io_sched_hdl->write.devices->len;
    count -= count_devices_shared_with(&io_sched_hdl->write, IO_REQ_READ);

    count += io_sched_hdl->format.devices->len;
    count -= count_devices_shared_with(&io_sched_hdl->format,
                                       IO_REQ_READ | IO_REQ_WRITE);

    return count;
}

/* Callback for io_sched_handle::dispatch_devices. This algorithm will compute
 * the relative weight of the I/O schedulers and dispatch devices according to
 * them.
 */
int fair_share_number_of_requests(struct io_sched_handle *io_sched_hdl,
                                  GPtrArray *_devices)
{
    const char *techno_of_previous_device = NULL;
    GArray *device_lists;
    GPtrArray *devices;
    size_t nb_devs;
    int rc = 0;
    int i;

    if (io_sched_hdl->io_stats.nb_reads +
        io_sched_hdl->io_stats.nb_writes +
        io_sched_hdl->io_stats.nb_formats == 0)
        /* nothing to do */
        return 0;

    device_lists = g_array_new(FALSE, FALSE, sizeof(struct device_list));
    /* Do not modify the callers' list of devices. */
    devices = gptr_array_copy(_devices);

    /* When a device is removed, it is directly removed from the corresponding
     * scheduler. We can only have the same amount of devices plus new ones when
     * devices are added to the LRS.
     */
    nb_devs = count_distinct_devices(io_sched_hdl);
    assert(nb_devs <= devices->len);

    /* sort devices by technology before creating sublists */
    g_ptr_array_sort(devices, sort_devices_by_technology_cmp);

    for (i = 0; i < devices->len; i++) {
        struct lrs_dev *dev = g_ptr_array_index(devices, i);
        struct device_list *current_dev_list;

        if (!techno_of_previous_device ||
            strcmp(techno_of_previous_device, dev->ld_technology)) {
            struct device_list device_list;

            rc = device_list_init(&device_list, dev);
            if (rc)
                goto free_device_list;

            techno_of_previous_device = device_list.technology;
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
            rc = fair_share_number_of_requests_one_techno(io_sched_hdl, devs);

        device_list_fini(devs);
    }

free_device_list:
    g_array_free(device_lists, TRUE);

    return rc;
}

static int dispatch_shared_devices(struct io_scheduler *io_sched,
                                   GPtrArray *devices_to_give,
                                   size_t target,
                                   enum io_request_type type)
{
    int i = 0;

    while (io_sched->devices->len < target && i < devices_to_give->len) {
        struct lrs_dev *dev = g_ptr_array_index(devices_to_give, i++);
        int rc;

        rc = io_sched->ops.add_device(io_sched, dev);
        if (rc)
            return rc;

        dev->ld_io_request_type |= type;
    }

    return 0;
}

static int
fair_share_number_of_requests_one_techno(struct io_sched_handle *io_sched_hdl,
                                        struct device_list *device_list)
{
    GPtrArray *devices = device_list->devices;
    struct device_repartition repartition;
    struct io_sched_weights weights;
    GPtrArray *devices_to_give;
    int min_devices = 0;
    int rc;

    if (io_sched_hdl->io_stats.nb_reads +
        io_sched_hdl->io_stats.nb_writes +
        io_sched_hdl->io_stats.nb_formats == 0)
        /* nothing to do */
        /* We could take all the devices from all the schedulers */
        return 0;

    devices_to_give = g_ptr_array_new();
    if (!devices_to_give)
        return -ENOMEM;

    /* we have to allocate at least the sum of the mins */
    min_devices += io_sched_hdl->io_stats.nb_reads > 0 ?
        device_list->read.min : 0;
    min_devices += io_sched_hdl->io_stats.nb_writes > 0 ?
        device_list->write.min : 0;
    min_devices += io_sched_hdl->io_stats.nb_formats > 0 ?
        device_list->format.min : 0;

    rc = io_sched_compute_scheduler_weights(io_sched_hdl, &weights);
    if (rc)
        GOTO(free_devices, rc);

    compute_number_of_devices(&io_sched_hdl->io_stats, &weights, &repartition,
                              device_list, min_devices);

    rc = fetch_devices_to_give(io_sched_hdl, devices, &repartition,
                               device_list->technology, devices_to_give);
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
        rc = dispatch_shared_devices(&io_sched_hdl->read, devices_to_give,
                                     repartition.nb_reads, IO_REQ_READ);
        if (rc)
            GOTO(free_devices, rc);

        rc = dispatch_shared_devices(&io_sched_hdl->write, devices_to_give,
                                     repartition.nb_writes, IO_REQ_WRITE);
        if (rc)
            GOTO(free_devices, rc);

        rc = dispatch_shared_devices(&io_sched_hdl->format, devices_to_give,
                                     repartition.nb_formats, IO_REQ_FORMAT);
        if (rc)
            GOTO(free_devices, rc);

    } else {
        rc = dispatch_devices(io_sched_hdl, devices_to_give, &repartition,
                              device_list->technology);
        if (rc)
            GOTO(free_devices, rc);
    }

free_devices:

    g_ptr_array_free(devices_to_give, TRUE);

    return rc;
}
