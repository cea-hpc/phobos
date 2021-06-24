% LRS device and media locking

# Overview

This document describes how the LRS daemon manages concurrency on devices and
media.

## Global concurrency between phobos servers

Global devices and media locks are stored in the DSS. They allow all phobos
servers to register exclusive access to resources such as drives, tapes or
directories. A LRS owning a lock on a specific resource has the guarantee to be
the only one able to access and use the resource.

These locks form the first global level managing concurrency among distributed
phobos servers.

## Local concurrency in one phobos server among all I/O operations

When a phobos server owns the global lock on a resource, it can use it for local
I/O operations. It can manage many parallel I/O operations on many resources.

Currently, we chose as first step a simple design to manage local concurrency.
Each resource could be dedicated to only one operation. To achieve this, LRS
manages a `ongoing_io` boolean flag per device to know if this device is
already used for one operation. There is no flag attached by the LRS to a medium
because we always use a medium through a device and the concurrency is already
managed by the flag of the device.

In the future, this local concurrency strategy could evolve to manage many I/O
in parallel on the same resource. We can imagine replacing the `ongoing_io`
boolean flag by a counter and adding to a device the list of current running I/O
operations.


# Global device locking

Devices are loaded in the DSS with a host and a owner (see database.md).

When a LRS starts, it locks all devices attached to the current host with an
unlocked admin status and when it stops, it unlocks all of them.


# Global media locking

When a medium is attached to a device, the LRS owning this locked device must
lock the medium and it must remain locked until the medium is detached. For
example, a tape is locked when it is mounted into a drive.

When a medium is detached from a device, the LRS must release the corresponding
lock.

When a LRS starts, if it finds any media already attached to devices it owns, it
must lock them. For example, if a LRS finds tapes already mounted in its
devices, it locks them. Because corresponding dir device and medium are always
attached, a dir medium is locked as soon as the corresponding dir device is
locked.

When a LRS stops, it unloads and then unlocks all its media.


# Global lock cleaning at LRS startup

At startup, a LRS should care about existing DSS locks.

- If a LRS finds an existing lock on its devices or their media with its
hostname but a different owner, it must release the old lock and replace it by
a new lock with the current owner and the same hostname.

- If a LRS finds an existing lock belonging to another hostname on its devices
or their media, the LRS must not acquire any locks on these resources and should
consider them as failed.

- If a LRS finds an existing lock with its hostname but a former owner on a
resource that is now owned by another node (device with a different hostname or
a medium which is not loaded into one of its devices), it must release it.

For each of these situations, a warning must then be emitted to inform
the administrator about this unexpected situation.


# Administrative status versus LRS resource locking

An admin can lock/unlock a resource by changing its admin status to prevent the
use of this resource by any phobos host (see
admin_resource_operation_control.md).

Administrative status supersedes LRS resource locking. When a resource has a
locked admin status, no LRS can use it and any LRS lock on it must be removed.
When an administrative status is set to locked, if a LRS was holding a lock on
this resource, it must finish the current action on the resource if any and
must not schedule any new operations (we can imagine adding a more agressive
admin status that leads to the abortion of the current actions in addition of
preventing new ones). Then, it unloads any targeted medium (medium directly
targeted by the locked admin status or medium loaded into devices targeted by
the locked admin status). Finally, it unlocks the corresponding resource.

When the locked administrative status is removed, the LRS hosting this resource
must get back a lock on it if it still owns the resource.
