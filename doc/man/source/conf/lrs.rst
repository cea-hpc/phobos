[lrs] - Configuring the LRS (Local Resource Scheduler)
======================================================

This section explains how to configure the Local Resource Scheduler (LRS) in
Phobos. All LRS-related parameters should be listed under the **[lrs]** section.

*families*
----------

The **families** parameter defines the families that Phobos can handle. Phobos
can manage multiple resource families (dir, tape, etc).

The supported families are: **dir**, **tape** and **rados_pool**.
Managed families must be listed as a comma-separated list without spaces.

If this parameter is not specified, Phobos defaults to the following:
**families = tape,dir,rados_pool**.

Example:

.. code:: ini

    [lrs]
    families = tape,dir,rados_pool

*fifo_max_write_per_grouping*
-----------------------------

The **fifo_max_write_per_grouping** parameter defines the maximum number of
concurrent write operation per grouping per fifo scheduler.
Any strictly positive value limits concurrent writes up to this maximum number.
0 stands for no limit.
This parameter is only used by the fifo write schedulers.
fifo_max_write_per_grouping = 0
which will be used by Phobos to communicate between clients and the daemon.

If this parameter is not specified, Phobos defaults to the following:
**fifo_max_write_per_grouping = 0**.

Example:

.. code:: ini

    [lrs]
    fifo_max_write_per_grouping = 7

*lock_file*
-----------

The **lock_file** parameter defines the location of the Phobos daemon's lock
file. This file is used to prevent a new Phobos daemon from starting while
another instance is already running.

If this parameter is not specified, Phobos defaults to the following:
**lock_file = /run/phobosd/phobosd.lock**.

Example:

.. code:: ini

    [lrs]
    lock_file = /run/phobosd/phobosd.lock

*max_health*
------------

The **max_health** parameter defines the maximum value of the health counter of
a resource (dir, drive, rados_pool and tape). When an error occurs due to any
SCSI-related reason, the health counter is decreased, and it is increased when
an operation is successful. If the counter reaches zero, the resource is
considered as failed.

If this parameter is not specified, Phobos defaults to the following:
**max_health = 1**.

Example:

.. code:: ini

    [lrs]
    max_health = 5

*mount_prefix*
--------------

The **mount_prefix** parameter defines where to mount Phobos' filesystem when
using tapes. This parameter can also be used to add a prefix to the mount point.

If this parameter is not specified, Phobos defaults to the following:
**mount_prefix = /mnt/phobos-**.

Example:

.. code:: ini

    [lrs]
    mount_prefix = /mnt/phobos-

*policy*
--------

The **policy** parameter defines which policy the Phobos daemon will use to
determine which media to use to fulfill the requests it receives. The available
options are: **first_fit** or **best_fit**.

If this parameter is not specified, Phobos defaults to the following:
**policy = best_fit**.

Example:

.. code:: ini

    [lrs]
    policy = best_fit

The available policies
~~~~~~~~~~~~~~~~~~~~~~

* **first_fit**:
    The first_fit policy selects the first medium with enough space to
    satisfy the request.

* **best_fit**:
    The best_fit policy selects the medium with the smallest available space
    that can satisfy the request.

*server_socket*
---------------

The **server_socket** parameter defines the location and name of the socket
which will be used by Phobos to communicate between clients and the daemon.

If this parameter is not specified, Phobos defaults to the following:
**server_socket = /run/phobosd/lrs**.

Example:

.. code:: ini

    [lrs]
    server_socket = /run/phobosd/lrs

Thresholds for synchronization mechanism
----------------------------------------

Phobos has a synchronization mechanism to keep its file system up to date. At
the end of an I/O operation, the LRS receives a release request telling
it to perform a synchronization. To avoid triggering a synchronization every
time a release request is received, three thresholds have been introduced to
determine when synchronization should occur. Each family in Phobos has its own
thresholds.

*sync_nb_req*
~~~~~~~~~~~~~

The **sync_nb_req** parameter defines the number of released requests required
to trigger a synchronization. Its value must be a comma-separated list of
"key=value" pairs for each family. The specified value must be between **0** and
**2^32**.

If this parameter is not specified, Phobos defaults to the following:
**sync_nb_req = tape=5,dir=5,rados_pool=5**.

Example:

.. code:: ini

    [lrs]
    sync_nb_req = tape=5,dir=5,rados_pool=5

*sync_time_ms*
~~~~~~~~~~~~~~

The **sync_time_ms** parameter defines the maximum time (in milliseconds) a
request can remain released without being secured by a synchronization.
Its value must be specified as a comma-separated list of "key=value" pairs.
The value for each family must be between **0** and **2^64**.

If this parameter is not specified, Phobos defaults to the following:
**sync_time_ms = tape=10000,dir=10,rados_pool=10**.

Example:

.. code:: ini

    [lrs]
    sync_time_ms = tape=10000,dir=10,rados_pool=10

*sync_wsize_kb*
~~~~~~~~~~~~~~~

The **sync_wsize_kb** parameter defines the maximum global size written for all
released requests before executing a synchronization. Its value must be
specified as a comma-separated list of "key=value" pairs for each family. The
specified value is in KiB and must be between **0** and **2^54**.

If this parameter is not specified, Phobos defaults to the following:
**sync_wsize_kb = tape=1048576,dir=1048576,rados_pool=1048576**.

Example:

.. code:: ini

    [lrs]
    sync_wsize_kb = tape=1048576,dir=1048576,rados_pool=1048576

*locate_lock_expirancy*
-----------------------

The **locate_lock_expirancy** parameter defines the time a lock must be kept by
the LRS before eventually releasing it. This allow one to avoid releasing a
medium too early in case a locate command targeted it. Its specified value is
in ms, and must be between **0** and **2^64**.

If this parameter is not specified, Phobos defaults to the following:
**locate_lock_expirancy = 0**.

Example:

.. code:: ini

    [lrs]
    locate_lock_expirancy = 300000
