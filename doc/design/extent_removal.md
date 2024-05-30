Extent removal
==============

# Context
In case a user wants to remove its data from Phobos system, they can use the
`delete` command, which labels the old object as `deprecated` without removing
any extent on the hardware. Data removal is thus operated by garbage collection
mechanisms, for instance during a `repack` operation, where all old deprecated
objects are deleted from the database, and their extents labelled as `orphan`.

For now, except for the `repack` operation, there is no way in Phobos to remove
an extent on the hardware.

# Operations
Removing extents can be done for the families managed by Phobos:

* `dir`:   through the system call `unlink`
* `rados`: through the call `rados_aio_remove`
* `tape`:  using repack operation, which will format the tape, but an `unlink`
           call can still be done through LTFS

# IO Adapter
The adapter already possesses calls to `remove` operations, as described above.
The call `ioa_del()` being implemented for both posix and rados systems:

```
struct pho_io_adapter_module_ops {
    ...
    int (*ioa_del)(struct pho_io_descr *iod);
    ...
};

int pho_posix_del(struct pho_io_descr *iod);
int pho_rados_del(struct pho_io_descr *iod);
```

# CLI commands
Those calls will be implemented in a "best effort" way. In the case of `dir`
and `rados` families, both can remove data from the systems. But for `tape`s,
data will be erased only after the repack operation.

Phobos already has a `delete` command, which indicates to the database that the
object is no longer alive. For the new behavior, we opted for the following, to
avoid confusing the user with both `delete` and `remove` commands:

```
$ phobos object delete --hard oid
```

# Process steps
The extent removal will imply, at least, the following steps:

* retrieval of the extents bound to the object;
* deletion of the object from the database;
* status modification for extents to `orphan`;
* _allocation of media which store the extents_;
* _call to io adapter deletion_;
* _deletion of the extents from the database_;
* _release of media_.

_The four last steps_ do not concern tapes, as we want to maintain what remains
on a tape for stats, and allow its selection by the garbage collection/repack
mechanism.

# Implementation hints
Some hints to help the brave developer:

* the Xfer already possesses a delete operation, a `hard-delete` flag can be
  added to specify the extent removal operation;
* communication protocol can easily be modified to manage this operation, by
  letting the `size`/`size_written` fields for write/release requests be
  negative, to indicate bytes are removed;
* going through the layout may be not relevant, as we want to remove all extents
  without considering their nature (mirroring, parity, etc.);
* lrs must be updated to handle negative sizes.
