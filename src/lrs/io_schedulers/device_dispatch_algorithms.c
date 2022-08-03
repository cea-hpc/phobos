#include "io_sched.h"

#include "schedulers.h"

static int request_handler_no_dispatch(struct pho_io_sched *io_sched,
                                       GPtrArray *devices,
                                       struct request_handler *handler)
{
    int i;

    for (i = 0; i < devices->len; i++) {
        struct lrs_dev *device;

        device = g_ptr_array_index(devices, i);

        handler->ops.add_device(handler, device);
    }

    return 0;
}

int no_dispatch(struct pho_io_sched *io_sched,
                GPtrArray *devices)
{
    int rc;

    rc = request_handler_no_dispatch(io_sched, devices, &io_sched->read);
    if (rc)
        return rc;

    rc = request_handler_no_dispatch(io_sched, devices, &io_sched->write);
    if (rc)
        return rc;

    rc = request_handler_no_dispatch(io_sched, devices, &io_sched->format);
    if (rc)
        return rc;

    return 0;
}
