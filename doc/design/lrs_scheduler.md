# LRS I/O Scheduling

## Objectives

The I/O scheduler can aim at optimizing:

- the time required to process requests
- the media usage

These objectives can be different depending on the type of request. For example,
we might want to optimize read requests for speed and write request for data
placement and media usage.

## Optimization opportunities

The overall I/O performance of Phobos can be improved in several ways:

* by balancing the load on each device:
    * it can be balanced by size or by request completion time
    * it can also take the type of the request into account
* by minimizing the amount of time a device is idle:
    * when waiting for other devices in case of raid1 layout for example
* by maximizing the read/write speed on each media:
    * by optimizing the order of requests on each device
    * by using a number of concurrent I/O suitable to each device/media
    * by grouping requests with a fix optimal size
* by minimizing the number of mount/umount operations on each device:
    * we can use a minimum number of media:
        * bin-packing-like problem
        * use soon to be used read/formatted tapes for upcoming write operations
* by reducing tape rewinds
    * using the RAO feature

These criteria are simpler to deal with than the global objectives listed at the
beginning and can help contribute to their optimization.

## Parameters

There are a number of hardware constraints:

* the duration of a mount/umount/sync operation
* the number of available devices and media as well as their type and access
  mode (R/W or read-only)
* the optimal number of concurrent I/O per device/media
* the read and write speed of each device/media
* the space available in each media
* constraints between media and device technologies
* the media health

Constraints related to the requests to be scheduled:

* the medium used in format requests
* the media that can be used for read operations
* the media compatible with a write operation: tags and sizes
* mput/mget: groups of requests that may need to be scheduled differently

Constraints due to already scheduled requests:

* requests in the scheduled queues
* running requests
* mounted media
* age of a request

And parameters that can be set by the admin through Phobos's configuration:
* priority between types of request: put/get/format
* weights associated to tags or requests
* fairness between:
    * types of requests
    * size of the requests
    * tags

### Algorithms

The complex task of scheduling I/O operations on the LRS can be broken down into
several simpler ones:

* associating a medium to a request (for read and write)
* associating a device to a set of requests (which can target the same medium
  for example)
* ordering requests based on priorities and optimal media access patterns
* choosing the next media to mount

## In practice

### A first implementation

Read performance on tape can greatly benefit from the minimization of mount and
unmount operations, and tape movements (mainly rewinds). The following sections
describe algorithms that may improve the performance of Phobos but without
specifying every implementation details such as request queues.

**Tape movements:**

To minimize tape movements, a simple solution is to group read allocation
requests by tape. If the tape is already mounted and ready to use, the request
should be pushed directly to the device. Otherwise, a device selection algorithm
will decide where the media should be mounted.

We can later optimize this further by looking at all the read allocations and
pick which media to read from per request (if several candidates exist) to
minimize movements further. This means that if an object has been written on
tapes A and B, it may be more efficient (depending on other requests or media
type and health) to mount tape A for example.

Another important optimization is for objects with several extents: once we
receive a read request for an extent, we know that the others will be
accessed.

**Rewinds:**

In order to minimize rewinds, we can rely on the RAO feature of enterprise tape
drives. To achieve that, the LDM module has to be extended to build GRAO and
RRAO SCSI requests which can then be passed to LTFS through the extended
attribute "ltfs.vendor.IBM.rao" in the `fs_adaptor`.

Each time one or more requests are added to the device's scheduled queue, the
RAO has to be computed again. These requests will target the media currently
loaded. This raises the question of how often the RAO can be computed. This
computation can be done only when the device selects a new request to reply to.

If the given drive does not support such a feature, we can:

* sort requests by logical block index which is not optimal but can produce
  decent performances when the number of recalls is high enough or if the data
  is located in one track (in this case, this heuristic is optimal);
* a more complete approach would be to create our own RAO. Some methods have
  been developed to estimate the position and locate time of a block at a low
  cost on tape. These methods seem to be rather efficient at least when the
  compression ratio is not too big but this seems to be a limitation of
  enterprise tape drives' RAO anyway. Still, building such an algorithm is not
  an easy task but can be based on the work already done in this area.

The RAO depends on the drive technology as well as the tape. Additionally, the
access time depends on physical parameters of tapes which are not constant for
every tape of the same model. Therefore, the algorithm depends on the tape and
the drive but also which data has to be read.

Currently, the RAID1 layout only requires one medium at a time to read an
object. This means that the RAO can be computed independently on each device.
But, future layouts such as erasure coding or RAID0 could prevent such
optimization as the LRS will not be able to compute the RAO freely. One
way to mitigate this issue could be to make sure that objects with the same
layout are written in the same order on each tape so that the read can be done
in the same manner for each request.

Up to version 1.93, the LRS stops searching for media as soon as a mounted one
is found or the first medium that can be mounted on a free device is found. This
behavior might prevent further optimization as not every tape is considered for
the read request.

**Device load balancing:**

In order to use the resources well, we have to balance the requests between
devices. The question here is: how to choose which device will mount the next
tape?

A first approach could be to look at the total size of requests handled by each
device and queue requests associated to a new tape on the least busy one.

Further optimization could involve counting the number of failures on each drive
and put more work on stable ones.

In short, the first algorithm would look like this for read allocations:

```
schedule(new_requests)
    for request in new_requests
        if request.tape is loaded on device D
            send request to D
        else
            send to least busy device

device thread
    ...
    if new read request received
        compute RAO
    ...
```

For write allocations, the most important issue is to balance the load between
devices. The same approach as read allocations can be used. The main advantage
here is that we can choose any tape with the right tags and enough space.

If several tapes are involved in a write allocation, we have to choose by order
of least busy devices.

The choice of tapes can use health metrics as well.

**Putting it all together:**

The last issue is to manage the three different types of requests efficiently:
read, write and format. The simplest solution is to prevent request types from
colliding on a device. The administrator can select ahead of time devices which
will only perform read, write or format. This means that the device selection
algorithm has to pick devices allowed to handle the request's type.

This imposes a new complexity: a request can be sent to a LRS that cannot handle
it. This means that the locate feature has to be extended to write and format
requests as well. Furthermore, the type of operation that can be done by a
device could be selected dynamically but Phobos lacks the ability to have a
global view of the system to make such a decision. Still, this idea could be
explored at the level of each LRS.

Also, if an object is written on a tape mounted on a write-only device, it will
not be accessible. This means that read and write requests may have to coexist
anyway. Depending on the importance of read requests relative to write, we can
either schedule writes first and queue reads behind or the other way around.

**mput: grouped write**

Some applications require that related objects are written on the same tape.
The details of how the user can express this through the API or the CLI to the
LRS are not discussed here. From a scheduling perspective, the simplest way to
handle this is by considering the write allocations for these related objects as
one request whose size would be the sum of each object's size.

The difficulty is that the total sum of the objects may not fit on the currently
mounted tapes or any tape if the request is too big. In the second case, the
objects have to be split. But in the first case, the scheduler can choose to put
this request on hold until a new tape can be mounted. This choice depends on how
important it is for the user to have everything in one tape. If this is
mandatory, the request will wait for an opportunity to be scheduled but if this
is just a hint, more complex scheduling strategies can be involved. A simple
criteria being how long the request can wait before the response.

**Choose the "best" tape to mount next:**

Grouping read requests by tape is one thing but we still have to chose the next
tape to mount. The same goes for a write: once a tape is full, which tape should
we mount?

For format requests, the problem is simpler, the tape is determined by the
request. Furthermore, if a drive is dedicated to format operations, the order in
which they are performed will not matter. Therefore, the scheduler can perform
them in a FIFO order.

A simple heuristic for read requests is to select the tape which has the most
requests associated to it. We can look at the number of requests or the total
volume and priorities (when they are implemented). The age of a request is
important as well.

For write requests, the problem can be much simpler. As long as the tape has
enough free space and is compatible with the request and the available drives,
it can be used. It is probably more efficient to minimize splitting objects. So
the last objects written can be chosen to minimize that. Also, some load
balancing algorithm optimally schedule big tasks first and use heuristics to
balance the load with small tasks. If such algorithm is implemented, we have to
take the request's age into account.

Improving on the media selection algorithm depends on the type of behavior
required. Write operations can influence the performance of reads. Formats
could be performed by the LRS when the number of formated tapes drops below a
given threshold.

We can also optimize data placement by filling tapes as much as possible. We
can try to keep tapes mounted for as long as possible so that data recently
stored can still be accessed reasonably quickly. This implies that read and
write operations can be done on the same device.

Tapes can be ordered using the media health parameter provided by LTFS and the
number of failures that occurred in the past. For example, the number of times a
mount or an I/O failed on a particular media or device.

### Conclusion

As a first step, we can implement the simple version of these algorithms and
proceed to test new ideas when we are able to assess their performance.

## Performance tests

Once these first heuristics are implemented, they can be compared to the old
FIFO implementation and new ideas can be tested. A number of metrics can be
measured in order to assess the performances of this algorithm:

* number of tape mounts and rewinds;
* time during which a tape stays mounted;
* average throughput;
* flow time: $C_i - r_i$ where $C_i$ is the time at which request $i$ is
  finished and $r_i$ is the time at which it was received;
* total size read or written per tape mount;
* number of requests per tape (reset after an unmount);
* number of tapes used;
* volume used per tape.

These parameters can be evaluated in different scenarios where:

* the size of the object varies;
* the number of concurrent requests varies for a fixed size;
* the object layout varies;
* the number of request varies (with a fixed size per request or total);
* the distribution of read, write and format requests varies (the repartition
  between the number of devices reserved for a given type of request may be
  influenced by the distribution).

## Further improvement ideas

### A mechanism for reserving tapes

The scheduler can decide in advance which tapes will be used and ensure that
it won't change in the future by reserving them. This could be done through a
regular DSS lock (or a new type of lock).

The two advantages are:
1. the scheduler knows that the device thread will be able to mount the tape
   when necessary, no one will use it;
2. other Phobos features like locate can use this lock to know where a tape will
   be accessible in the "near" future.

The scheduler can also provide more information with the lock such as the
estimated time before the tape will be mounted so that other Phobos clients can
decide whether this reservation will be useful to them or not.

### A good estimation of the duration of an I/O

It can be useful for the scheduler to infer the duration of an I/O from the size
and type of I/O and media so that the scheduling can be more precise. This may
be a too small optimization in a first version of this scheduling.

A good estimation of the duration could allow the scheduler to know when a
synchronisation is required and improve its scheduling that way. A first step
in that direction could be to estimate how good "size / average speed" is in
practice.

This estimation may be possible for tapes because the RAO feature can provide
the estimated time to move the tape head from one block to the next. The total
time can be computed by adding this information with the duration of read
operations (`size / avereage speed`). For write operations, computing the RAO on
the last written block will provide the time it takes to move the head from its
current position to the start of the write.

### Handling idle devices

A device can be idle when waiting for other devices to complete their I/Os for a
grouped operation (i.e. raid1 layout with repl_count > 1).

To minimize this effect, the scheduler can put partial read/write (or complete
if a small enough one can fit) operations in these empty slots or even a
synchronization if appropriate. This may be useful for some types of media but
may hinder performance on tapes for example.

This kind of optimization would require a good estimation of the I/O durations.

### Mixing reads, writes and formats

Format operations could be performed in the background when the number of
formated media drops below a certain threshold. They could be scheduled
efficiently when a device is idle. The same goes for repacks which may be
triggered by certain metrics on the medium and scheduled depending on the
current system's load.

Reading performance may be improved further by carefully choosing where to place
the write requests. To achieve that, the scheduler has to be able to infer when
a read is likely to happen and which data will be fetched. This can be achieved
through hints given by the user, the administrator or a policy engine.

We could place objects with different hints on different sets of tapes so that a
given hint can be fetched efficiently from a few tapes, thus reducing the number
of mounts and rewinds. Or allocate a share of each tape for a few hints on each
tape. We could then write objects of the same hint consecutively to reduce
rewinds when fetching the data. This would also allow more control over how long
a tape stays mounted so that recently written data can still be accessed rather
efficiently.

With a system like this, policies can be implemented to fetch data that is
likely to be accessed by a job or user when a certain hint is requested. Even if
this system may not improve the read performance a lot, it may still be useful
to group related data on a few media.

These hints can also be used to group data on tape if we want to export it from
the system. In this case, a hint would be associated to one or several tapes and
the LRS have to use these tapes to write these objects. No other hint can be
written on them.

## Development steps

Below is a list of somewhat isolated features to implement in order to achieve
the goals described above:

- Define and implement a data structure which will hold the information
  necessary for the scheduling. It must at least contain:
  - a list of requests per media;
  - a list of media per device;
  - a quick way to access the current/next request that will be sent to the
    device;
  - and maybe a list of unprocessed (incoming) requests.
- Improve `mput` behavior: put every object on the same media
  - The current `mput` does not convey any meaning to the LRS to indicate what
    to do with the requests and the LRS will try to parallelize the requests.
    This means that there is currently no benefit to using `mput` apart from its
    simpler use;
  - For optimization purposes, the user should be able to convey this
    information to Phobos through the store API and `mput` seems like the
    appropriate candidate to do so;
  - The question remains as to how it should be done:
    - the client could send:
      - one write allocation whose size is the sum of each file's size;
      - one write allocation with a list of files to write;
      - one write allocation per file to write but with a flag indicating that
        they are related.
    - it seems that this optimization requires to modify the layout as they
      create the requests;
- RAO:
  - the client must send the name of the extent it wants to read;
  - test whether the drive supports RAO or not;
  - build the input that must be written to the file given to LTFS through
    "ltfs.vendor.IBM.rao" (GRAO SCSI request);
  - interpret the result of the RAO written by LTFS to the corresponding output
    file (RRAO SCSI request);
  - compute the RAO inside the device thread. We should probably compute it only
    when necessary to reduce latency as a huge number of requests may take a
    while to be processed.
- Restrict the type of requests that a device can handle (Read/Write/Format):
  - the admin will set these rules in the DSS through a new CLI command
    analogous to the one for media;
  - the LRS has to fetch these rules when loading a device and be notified when
    they change;
  - the LRS has to take them into account when selecting a device;
  - the implementation must allow "write-only" devices to be read from otherwise
    recently written objects can't be read without unloading a tape.
- Implement the main scheduling routine:
  - this routine needs to be configurable as selecting different routines will
    be necessary for performance measurements. As a first step to prepare the
    code, we can make the FIFO routine configurable.
