# Local Resource Scheduler

C library: libpho_lrs

The LRS is responsible for scheduling media allocations to IO processes, which
is particulary performance critical when dealing with tape libraries. The LRS
does not perform any IO, but is responsible for flushing media appropriately.
The flush timing is also performance critical when dealing with tape libraries.

## Interfaces

* Receive local client requests (from the network layer).
  (v1: direct calls from object store)
* Receive local admin requests (from the network layer)
  (v1: `phobos` command line directly accesses the DB and the LRS API).
* Call LDM primitives for device control.
* Call DSS to discover and lock shared resources (media and drives).

## Client calls

Calls to the LRS are performed through the LRS protocol, defined and
documented in `pho_srl_lrs.h` and `pho_proto_lrs.proto`. This section lists the
main message types and their purpose.

The typical behaviour of the LRS when managing tapes would be to buffer
requests and answer to them in batches so that multiple IOs can be efficiently
performed on the allocated media.

### Writing

* Write allocation request: request N distinct media on which to write a given
  amount of data.
* Write allocation response: contains exactly N media identifiers (and mount
  point information) to satisfy the matching request. The number of media is
  equal to the one that was requested, but the available size on each of them
  may be smaller.

### Reading

* Read allocation request: request K among N media to be allocated. A list of
  N media identifiers is provided in the request, and the LRS shall allocate
  exactly K of them.
* Read allocation response: contains exactly K media identifiers (and mount
  point information) satisfying the matching request.

### Releasing

* Release request: ask to flush a given medium and signal that it will no
  longer be used by the requester.
* Release response: signals the requester that the medium has been flushed and
  is not considered in use by this requester anymore.

Release responses would typically be sent in batch for a given medium. When
writing to tapes, this allows to minimize the overhead of flushing LTFS.

### Format

* Format request: request to format a given medium (designated by its
  identifier) with a given filesystem.
* Format response: signals that a medium has successfully been formatted.

### Notify

* Notify request: notify that the LRS needs to update its internal information
  by retrieving a new state from the database.
* Notify response: signals that the LRS internal state was updated.

### Error

* Error response: this response type is sent in case of error when handling
  any of the available request types. It contains details about the error that
  happened.


## Internals

The LRS is Phobos's "brain". Its scheduling policies will drive Phobos
performance.

For now (2019.09.27) its scheduling algorithm is relatively trivial:

* The requests are handled one by one, in the order they arrive, with the
  release requests handled first.
* For write requests: the LRS first tries to find a mounted medium matching the
  request, otherwise it will attempt to find a matching medium in the DSS and
  mount it. A "matching medium" has enough space to contain the requested size
  and has all the requested tags.
* When mounting a medium: the first drive that is not currently in use is
  freed to mount the new medium.
* The first request that triggers an EAGAIN blocks the line of requests until
  it can be satisfied.


In the future, it will be critical to implement better strategies that look at
pending requests more globally to be able to take the most out of the
available hardware. It will also be necessary to be able to allocate a medium
that has less space than required if no other medium can fulfill this request;
this will have the effect of splitting the data over multiple media.
