# I/O Scheduler

## Goal

In order to try and compare several scheduling algorithms, it would be
interesting to have a generic API composed of a set of algorithms to pick from.
This API will be based around the concept of I/O scheduler. This will also
enable administrators to choose which algorithm fits their needs best.

An I/O Scheduler is a software abstraction layer inside the LRS' Scheduler
(`lrs_sched` referred to as *main scheduler* in the remainder of this document).

This component is focused on I/O related requests such as read, write and
format. The goal of this layer is to provide a generic scheduling API which will
make the development of new components easier and allow the administrators to
select easily from configuration which I/O scheduler should be used. The main
scheduler will manage 3 I/O schedulers, one per type of I/O request.

Each request is scheduled independently because they require different needs.
This API will also make it possible to schedule several types of requests in one
scheduler. So this should not be a limitation in practice if the need arises
someday.

The role of these I/O schedulers is to take requests in and add them into their
internal data structures (which will depend on the scheduling algorithm). Then
the main scheduler will ask for requests to the I/O scheduler and push them to
the right device.

Other parts of the scheduling are configurable. For instance, each I/O scheduler
will be given a set of devices on which they can schedule requests. Every
scheduler can have access to every device or only a subset of them. An I/O
scheduler may not assume that it is the only one using a device. Which device
gets allocated to which type of request can be chosen by some heuristic, for
example:

- every scheduler uses every device;
- the devices are spread between schedulers using some configurable percentage
  (i.e 40% read, 40% write and 20% format);
- chosen dynamically depending on the system's load, etc.

As each scheduler is independent, the main scheduler needs to know from which
I/O scheduler it should take requests. A configurable algorithm will also be
used internally by the I/O scheduling API to know which scheduler should be
used. This will therefore be transparent from the main scheduler's perspective.
This algorithm can also help set priorities between request types which is
useful when a device can handle several types of requests for example.

Possible algorithms include:
- FIFO: we take requests from the scheduler whose next request is the oldest.
  Note: this does not guarantee a FIFO behavior overall;
- using some form of fair share algorithm on the request's type (e.g. 40% of
  reads, 50% of writes and 10% of formats).

This API will therefore abstract and make configurable 5 different algorithms:
- 1 request scheduling algorithm for each type of request (so 3 in total) which
  will:
    - prioritize requests
    - associate requests to devices and media
- 1 device dispatching algorithm which will dispatch devices to I/O schedulers
- 1 request dispatching algorithm which will decide from which scheduler to pick
  the next request

For now, the device and request dispatching algorithms will not be configurable.
No dispatching will be done (every scheduler uses every device) and the
schedulers are used in a FIFO order.

### Configuration

A new section will be introduced in Phobos' configuration: `[io_sched]`.

This section should at least contain the name of each algorithm. Otherwise, a
default heuristic will be chosen.

Additionally, some of these algorithms may have parameters.

Here is an example of what this configuration can look like in practice:
```
[io_sched]
read_algo = grouped_read
write_algo = fifo
format_algo = fifo

# An example of a parameter named "param" for grouped_read
grouped_read_param = value
```

If we need the same algorithm for different kinds of requests with different
parameter values, we can consider adding an (optional) prefix to these
parameters:

```
# An example of 2 different values for the parameter "param" of the algorithms
# "A" for read and write requests
read_A_param = 1
write_A_param = 2
```

As this use case seems unlikely, this will not be implemented at first.

The device and request dispatching algorithms may also have parameters but for
now the basic algorithms wont so the configuration will not be introduced yet.

## API

A complete description of each function is provided in `src/lrs/io_sched.h`.

### Initialization

```
/* Initialize each I/O scheduler's memory */
io_sched_init
/* Cleanup each I/O scheduler's memory */
io_sched_fini

/* Load I/O schedulers at LRS startup, this function will also initialize the
 * I/O schedulers' internal data by calling io_sched_init
 */
load_io_schedulers_from_config

/* Call the device dispatch algorithm. This function will be called regularly
 * because devices may be added or removed during the LRS' lifetime and some
 * algorithm may have a dynamic behavior.
 */
io_sched_dispatch_devices
```

### Scheduling

```
/* Called to push a new request to the corresponding I/O scheduler */
io_sched_push_request

/* Called to know what is the next request to schedule. This is useful to know
 * which type of request will be scheduled next. This function should always
 * return the same request as long as no scheduling is done between two calls
 * (i.e. no call to io_sched_get_device_medium_pair, io_sched_requeue or
 * io_sched_remove_request).
 */
io_sched_peek_request

/* If a request returned by io_sched_peek_request cannot be scheduled at the
 * moment, this function will ask the I/O scheduler to reschedule it for later.
 */
io_sched_requeue

/* Remove a request returned by io_sched_peek_request from the scheduler when it
 * is pushed to a device or it cannot be allocated at all.
 */
io_sched_remove_request

/* Given a request returned by io_sched_peek_request, this function will tell
 * the caller the device and medium to use (cf. device and index parameters).
 * It should be called one time per medium required for the request.
 */
io_sched_get_device_medium_pair
```

### Device management

Since devices may be associated to a specific I/O scheduler, there is a need to
move them between schedulers such that each scheduler can update it's internal
data structures. These functions will be called by the device dispatch
algorithms and the main scheduler.

```
/* Add a device to the I/O scheduler */
add_device

/* Remove a device from the I/O scheduler */
remove_device

/* Ask the I/O scheduler for a device to take back and possibly allocate to
 * another I/O scheduler
 */
reclaim_device
```

## Algorithms

The following sections provide a very simplified example of the usage of this
scheduling API for each type of request.

### Format

```
reqc = io_sched_peek_request

/* index will always be 0 for a format since the request only contains one
 * medium
 */
device, index = io_sched_get_device_medium_pair(reqc)
if (!device)
    // no device available, continue

// try to schedule the request (cf. sched_handle_format)

rc = push_sub_request

if (rc == -EAGAIN)
    io_sched_requeue
else
    io_sched_remove_request
```

### Read

```
reqc = io_sched_peek_request

for i in 0..reqc->n_required:
    device, index = io_sched_get_device(reqc)

    // ... cf. sched_read_alloc_one_medium

rc = push_sub_request

if (rc == -EAGAIN)
    io_sched_requeue
else
    io_sched_remove_request
```

### Write

```
reqc = io_sched_peek_request

for i in 0..reqc->n_media:
    device, index = io_sched_get_device(reqc)

    // ... cf. sched_write_alloc_one_medium

rc = push_sub_request

if (rc == -EAGAIN)
    io_sched_requeue
else
    io_sched_remove_request
```
