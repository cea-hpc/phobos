#include "io_sched.h"

#include "schedulers.h"

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
