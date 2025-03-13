[scsi] - Configuring the SCSI protocol
======================================

This section explains how to configure the SCSI protocol in Phobos. All
SCSI-related parameters should be listed under the **[scsi]** section.

*inquiry_timeout_ms*
--------------------

The **inquiry_timeout_ms** parameter defines the maximum time (in milliseconds)
Phobos will wait for a response to a SCSI inquiry request. The value must be
a positive integer.

If this parameter is not specified, Phobos defaults to the following:
**inquiry_timeout_ms = 10**.

Example:

.. code:: ini

    [scsi]
    inquiry_timeout_ms = 10

*max_element_status*
--------------------

The **max_element_status** parameter defines the chunk size used for read
element status requests, controlling how much data is retrieved in a single
request.  Setting it to **0** allows an unlimited chunk size, preventing the
request from being split into multiple smaller chunk.

If this parameter is not specified, Phobos defaults to the following:
**max_element_status = 0**.

Example:

.. code:: ini

    [scsi]
    max_element_status = 0

*move_timeout_ms*
-----------------

The **move_timeout_ms** parameter defines the maximum time (in milliseconds)
Phobos will wait for a tape move operation to complete. If the operation
exceeds this time, it is considered failed. The value must be a positive
integer.

If this parameter is not specified, Phobos defaults to the following:
**move_timeout_ms = 300000**.

Example:

.. code:: ini

    [scsi]
    move_timeout_ms = 300000

*query_timeout_ms*
------------------

The **query_timeout_ms** parameter defines the maximum time (in milliseconds)
Phobos will wait for a response to a SCSI query request. If the device does
not respond within this time, the request is considered failed. The value
must be a positive integer.

If this parameter is not specified, Phobos defaults to the following:
**query_timeout_ms = 1000**.

Example:

.. code:: ini

    [scsi]
    query_timeout_ms = 1000

*retry_count*
-------------

The **retry_count** parameter specifies the number of times a SCSI request
should be retried in case of failure. This helps prevent transient errors from
causing an immediate operation failure.

If this parameter is not specified, Phobos defaults to the following:
**retry_count = 5**.

Example:

.. code:: ini

    [scsi]
    retry_count = 5

*retry_long*
------------

The **retry_long** parameter defines the delay (in second) to be used when the
device is busy or an unexpected error occurs. This delay applies to errors that
may require some time to resolve, such as busy device that needs to finish its
current operation before retrying. The value must be a positive integer.

If this parameter is not specified, Phobos defaults to the following:
**retry_long = 5**.

Example:

.. code:: ini

    [scsi]
    retry_long = 5


*retry_short*
-------------

The **retry_short** parameter defines the delay (in second) to be used when the
SCSI controler indicates that a request should be retried immediately. The value
must be a positive integer.

If this parameter is not specified, Phobos defaults to the following:
**retry_short = 1**.

Example:

.. code:: ini

    [scsi]
    retry_short = 1


[lib_scsi] - Configuring the SCSI library
=========================================

This section explains how to configure the SCSI library in Phobos. All SCSI
library-related parameters should be listed under the **[lib_scsi]** section.
Currently, there is only one parameter to configure.

*sep_sn_query*
--------------

The **sep_sn_query** parameter indicates whether Phobos should query the drive
serial number and the volume label in separate SCSI requests. Some libraries
can't report both in one request (e.g IBM library).

If this parameter is not specified, Phobos defaults to the following:
**sep_sn_query = false**.

Example:

.. code:: ini

    [lib_scsi]
    sep_sn_query = false
