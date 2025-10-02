[iosched] - Configuring the I/O scheduling
==========================================

This section explains how to configure the I/O scheduling in Phobos. Currently,
I/O scheduling algorithms are supported for the **dir** and **tape** family.
All I/O scheduling-related parameters should be listed under the
**[io_sched_dir]** and **[io_sched_tape]** section, respectively.

*dispatch_algo*
---------------

* The dir family does not support any algorithm to perform the repartition of
  device to I/O schedulers. The only value accepted is **none**.

  If this parameter is not specified, Phobos defaults to the following:
  **dispatch_algo = none**.

  Example:

  .. code:: ini

        [io_sched_dir]
        dispatch_algo = none

* Only the tape family support algorithm to perform repartition of device to the
  I/O schedulers. It can be configured using the **dispatch_algo** parameter.
  The supported algorithms are: **none** and **fair_share**.

  If this parameter is not specified, Phobos defaults to the following:
  **dispatch_algo = none**.

  Example:

  .. code:: ini

        [io_sched_tape]
        dispatch_algo = fair_share


 If the **fair_share** algorithm is select for the tape family, additional
 parameters can be configured in the **[io_sched_tape]** section. The fair share
 parameters define the minimum and maximum number of drives of a specific
 technology that should be allocated for each operation type: read, write and
 format.

 Each drive technology has two corresponding parameters: one for the minimum
 number of drives and one for the maximum.

 **Parameter key:**

 The key must be structured as "fair_share_%s_%s", where:

 * The first "%s" is replaced by the drive technology (e.g., LTO6)

 * The second "%s" is replaced by either "min" or "max" to indicate whether it
   sets the minimum or maximum allocation.

 **Parameter value:**

 The value must be a list of three integers, separated by commas without spaces,
 corresponding to the number of drives allocated for read, write and format
 operations, respectively.

 *Example usage*

 * If the maximum value is "5,5,1", it means Phobos can use up to 5 drives for
   reading, 5 drives for writing and 1 drive for formatting simultaneously. If an
   operation's value is "0", the number of drives is not restricted.

 * If the minimum value is "2,1,0", Phobos will reserve at least 2 drives for
   reading, 1 drive for writing and no drives for formatting.

 Example:

 .. code:: ini

    [io_sched_tape]
    dispatch_algo = faire_share

    fair_share_LTO4_min = 2,1,0
    fair_share_LTO4_max = 5,5,1

    fair_share_LTO5_min = 0,0,0
    fair_share_LTO5_max = 5,5,5

*format_algo*
-------------

The scheduling algorithm used for format requests with the dir and tape family
can be configured using the **format_algo** key. The only supported algorithms
is **fifo**.

If this parameter is not specified, Phobos defaults to the following for both
family: **format_algo = fifo**.

Example:

.. code:: ini

    [io_sched_dir]
    format_algo = fifo

    [io_sched_tape]
    format_algo = fifo

*read_algo*
-----------

The scheduling algorithm used for read requests with the dir and tape family can
be configured using the **read_algo** key. The supported algorithms are:
**fifo** and **grouped_read**.

If this parameter is not specified, Phobos defaults to the following for both
family: **read_algo = fifo**.

Example:

.. code:: ini

    [io_sched_dir]
    read_algo = fifo

    [io_sched_tape]
    read_algo = fifo

*write_algo*
------------

The scheduling algorithm used for write requests with the dir and tape family
can be configured using the **write_algo** key. The only supported algorithms is
**fifo**.

If this parameter is not specified, Phobos defaults to the following for both
family: **write_algo = fifo**.

Example:

.. code:: ini

    [io_sched_dir]
    write_algo = fifo

    [io_sched_tape]
    write_algo = fifo

The differents algorithms available with the I/O scheduler
==========================================================

*fifo*
------

With the **fifo** algorithm, the scheduler processes requests in order. The
oldest requests in the queue is processed first.

Note: The **fifo** algorithm does not guarantee a strict fifo behavior, as the
request can be requeued if no device is available. In that case, it will be
processed later.

*grouped_read*
--------------

The **grouped_read** algorithm attempts to group together requests that target
the same medium. Each request is pushed into the queue of every medium it
requires. If a medium is already loaded in a device, the request is immediately
placed in that device's queue.

The **grouped_read** algorithm orders by default its queues conforming to each
request QOS (Quality Of Service) and priority. The QOS of all read requests is
currently set to 0 and the priority is reversely set to the creation time of the
corresponding object copy. By default, medium per medium, the **grouped_read**
algorithm will first schedule the requests of the older object copies. This
scheduling heuristic aims to order the extent reading in the same order as they
were written. This feature may improve performance on tape but may have no
impact on directories. One could disable this ranking and use a basic fifo order
by setting to false the **ordered_grouped_read** option of the corresponding
**[io_sched_dir]** or **[io_sched_tape]** config section.

Example:

.. code:: ini

    [io_sched_dir]
    ordered_grouped_read = false

    [io_sched_tape]
    ordered_grouped_read = true

*fair_share*
------------

This algorithm is explained with the **dispatch_algo** parameter as it is the
only supported algorithm.
