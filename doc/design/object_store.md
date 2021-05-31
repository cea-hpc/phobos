# Object Store

C library: libphobos_store

## Interfaces

* Put/get calls from transfer tool or end-user (library or command line).
* Processes metadata requests (search for object, query object status).
* Call DSS to: save/query object location, store/query object metadata...
* (upcoming in v2) Call phobosd to prepare/release storage resources.
  Current implementation directly calls the LRS APIs.

## Calls

* mput([(id, src, md), ...])
* mget([(id, dst), ...])
* (unimplemented) del(id)
* (unimplemented) query(criteria)

## API

Transfer operations are described by the `pho_xfer_desc` structure, defined in
`phobos_store.h`.

Bulk operations (mput and mget) take an array of `struct pho_xfer_desc`.
These calls aim at allowing optimizations like deferred media sync by LRS.
Sub-operations can fail individually. Results are reported by invoking
xd_callback() for each transfer, with the corresponding error code. In the
future, a poll-like API could be considered, where new transfer requests are
added to an event loop, and a kind of `phobos_wait` function would allow to
retrieve the results of completed transfers.

## Internals

The `store` module relies on three other modules to put and get objects:

* `dss`: used as a store for object identifiers, user metadata and layouts.
* `layout`: abstracts how an object is written on a set of media (raid,
  compression, error correction, etc.). This is the module that triggers the
  actual IOs on media.
* `lrs`: schedules and makes media available for the `layout` modules to be
  able to perform their IOs.

Its roles are:

* To create the encoders and decoders that will handle each transfer (see
  `doc/design/layout.txt`).
* To forward messages between all the encoders / decoders and the LRS.
* To manage object metadata and layout persistance via the DSS.
