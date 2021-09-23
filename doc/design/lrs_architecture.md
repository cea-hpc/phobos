# Local Resource Scheduler Architecture

The LRS is Phobos' "brain". Its scheduling policies will drive Phobos
performance.

## LRS architecture summary

### Queues and Set

- Listening Socket: one
- Client Sockets: one per client
- Input Requests Queues: one per resource family
- Scheduled Queues: one per device
- Running Sets: one per device
- To-Sync Sets: one per device
- Responses Queue: one

### Threads
- CT, Communication Thread: one, actually the "main" thread
- STs, Scheduling Threads: one per device family
- DTs, Device Threads: one per device

## Receiving requests

### Listening socket, client sockets, Communication thread

The listening socket waits for new client connections. It is an
AF_UNIX/SOCK_STREAM socket. Each time a new client connects, the LRS
Communication Thread creates a new accepted client socket. This thread is in
charge of polling:
- the listening socket to accept new client connections and create new client
  sockets,
- the clients sockets to receive incoming requests from clients.

Ping, notify, release, cancel and monitor, requests are immediately processed by
the Communication thread.

Format, write and read allocation requests are instead enqueued in a "per
family" Input Request Queue.

In the future, if needed, the Communication Thread could be parallelized by
creating a Client Thread in charge of receiving incoming requests per client
socket and sending responses to each client. The Response Queue could also
be parallelized with one Response Queue per Client Thread.

### Requests id

- Clients fill an id into each allocation request (format/write/read) :
  "from_client_request_id".
- On the LRS side,
  - each client is linked to one socket and identified by an "lrs_client_id".
    This "lrs_client_id" doesn't exist from the client's point of view.
  - each request is identified by the couple "lrs_client_id" and
    "from_client_request_id".
  - some read and write allocation requests lead to booking several media with
    a sub-request per medium. Each sub-request has its own "medium_id".
  - on the LRS, each sub-request is identified by the triplet: "lrs_client_id",
    "from_client_request_id" and "medium_id". (The first two form the request
    id on the lrs side.)

From the client point of view and in all lrs<->client communications,
there are only "from_client_request_id" and "medium_id".

{[(from_client_request_id + lrs_client_id) -> lrs_request_id] + medium_id}
    -> lrs_sub-request_id

## Scheduling requests

### Ordering Format/Write/Read requests

There is one Input Request Queue per media family because allocations of
media from different families are not concurrent.

In the future, if we add an "any family" allocation request (ie : allocation for
which the lrs could chose the "best" family to execute it), the LRS will have to
optimize the dispatch between different family resources. We can imagine
different priority rules, like fair-share or fix priority for instance.

One Scheduling Thread per family is in charge of picking requests from each "per
family" Input Request Queue and reordering them based on a multicriteria
prioritization rule:

- fix weight per type of request,
- fair share per type of request per number of requests,
- fix weight per tag,
- fair share per tag per number of requests,
- fair share per tag per size of requests.

To achieve and store the pending requests in the new order, the Scheduling
Thread could use internal Ordered Queues.

### Drive set

Each LRS has its own set of devices which are locked for its sole use. This set
can be modified by using notify requests.

#### Medium status and id per device

The medium status per device could be either empty, loaded or mounted.

If the medium status is loaded or mounted, there is a medium id attached to the
holding device. Any medium loaded or mounted into a device must be locked by the
LRS which holds the device.

For read and write allocation requests, the loaded status is a transient one
between empty and mounted. For format operation, the medium status should be
loaded.

#### Scheduled queues

Each device has its own scheduled list of requests and operations.

The Scheduling Thread per resource family is in charge of filling each Scheduled
queue per device.

It takes into account an estimation of the duration of each request and
operation to optimize the scheduling. Each write, read and operation has a
latency parameter per device/medium type and a bandwith parameter per
device/medium type. (ie : bandwith * size -> duration)

As parameter, a device/medium pair has the targeted number of read and write
allocation requests running in parallel. In addition each device/medium type has
two parameters to define the targeted size of the scheduled list: number of
requests and the global size of the scheduled requests.

We could imagine having differentiated read and write parameters to
use, for example, for an RAO (Recommended Access Order) feature.

The scheduler could also take into account parameters setting the minimum number
of devices that should be available or used at any time per type of request
(ie: "3 devices should always be available or used to read")

Media are locked by this LRS into the DSS as soon as they are scheduled on a
device/medium pair.

The scheduler schedules the first request of the requests lists after running a
priority algorithm to order them. But it can bypass this order to optimize its
scheduling:

- finding a medium mandatory to a read allocation,
- finding a request that fills in the schedule windows (adding consecutive
  writes for tapes is a good example),
- backfilling small requests before interdependent grouped requests,
- grouping requests per tag on the same media (in addition
  of "on same media" grouping per tag, the scheduler can also apply a default
  grouping policy per client if enabled from the conf)...

Many parameters are defined into the configuration to set the scheduling
algorithm:

- grouping, bypassing and backfilling windows sizes,
- tags to be taken into account for grouping,
- activating grouping policy per client.

## Executing Requests

#### Media operations and format request

Media operations are sync, load, mount, umount and unload. These operations are
needed to get available media for devices and ready to be allocated for I/O
requests like format, read and write allocations. Whereas several read and write
allocation requests could run in parallel depending on device parameters, all
operations and the format request must be run alone on the device by the running
thread.

Each Device Thread is in charge of executing umount, unload, load, mount, format
when the next scheduled request from the Scheduled Queue needs it. It is also in
charge of executing sync operations when the state of the To-Sync Set needs
it.

#### Running set per device

A running request is a format, read or write allocation request on one
medium/device pair for which the device is ready and the lrs could have
already answered the corresponding allocation to the requesting client and has
not received yet the release request including this request.

As soon as a slot is free in its Running Set, the Device Thread takes, if any,
the first scheduled request and manages it. A medium operation should be
completed by this device thread. Allocation responses are triggered by the
device thread when its medium was the last one of the request awaiting to be run
or when an error occurs. The Device Threads push responses to the response
queue.

#### To-Sync Set per device

When the LRS receives a need-to-sync release request, all the requests that are
included in this release request are moved from the Running Set to the To-Sync
Set.

In order to execute efficient gathered synchronizations, a device/medium pair
has the three following parameters:

 - the to-sync targeted number of requests for a gathered synchronization,
 - the targeted to-sync global size before executing a synchronization,
 - the maximum time a request can stay released without being secured by a
   synchronization.

When a request is moved from "running" to "to-sync", the current time is
recorded. Date is registered to allow checking later if the maximum waiting
time is exceeded.

Reaching one of the three limits is enough to trigger the synchronization
operation.

Each Device Thread is in charge of triggering gathered synchronization of
to-sync requests. When a synchronization is triggered by the Device Thread, it
will execute this top priority sync operation as an exclusive running operation
when all current running operations will be released.

Umount operations contain an implicit synchronization. So, when the running
threads manages a umount operation, it must also manages the to-sync set by
mananging pending synchronization as done.

When the need-to-sync request is synchronized and the release response
concerning this request has been pushed into the Response Queue, the request
can be wiped from the LRS.

Only "need-to-sync" release requests need a release response from the LRS to the
client.

The "no-need-to-sync" flag should be automatic and meaningless for read release
request. For a write request, a "no-need-to-sync" flag in the corresponding
release request could corresponds to a write operation that failed or was
canceled by the client.

## Sending Responses

The Communication Thread takes responses from the Response Queue and sends them
to the corresponding clients.

## Monitoring

Input Requests Queues, Scheduled Queues, Running Sets and To-Sync Sets, can be
monitored with a monitor request. A json syntax allows to query and receive
selected pieces of data.

DUMP:
 {families=[
 { family="dir",
   pending_requests=[
     {client_req_id=xxx, req_type=READ_ALLOC, ...},
     {...}
   ],
   drives=[
      {drive_id=xxxxx,
       req_queue=[...]},
      {drive_id=xxxxx,
       req_queue=[...]}
 }]}

STATS:
 {families=[
 { family="tape",
   pending_req_read=42,
   pending_req_write=23,
   drives=[
      {drive_id=xxxxx,
       pending_req_count=13,
       pending_req_size=192348092438,
       ...
     }, ...]
}


This monitoring of the current activities of an LRS can be useful to inspect the
current workload of an LRS, for administrative or load balancing purposes, for
instance.
