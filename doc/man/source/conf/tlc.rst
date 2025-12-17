[tlc] - Configuring the TLC (Tape Library Controler)
====================================================

This section explains how to configure the TLC for a specific library. As Phobos
can handle multiple library, there should be a TLC instance for each library.
Each TLC should have its own section in the configuration file using the
following format: **[tlc %s]**. Here, "%s" should be replaced with the name of
the library. If there is only one TLC, the library's name should be the same as
the default library name.

*Library device*
----------------

The **lib_device** parameter defines a list of library changer for the TLC
server. This is a **critical parameter** as it tells the TLC where to find the
devices it needs to control. Specifying multiple lib devices will help make the
TLC more resilient to failures by adding redundancy to the lib devices it can
use. The value must be a comma-separated list without any spaces.

If this parameter is not specified, Phobos defaults to the following:
**lib_device = /dev/changer**.

Example:

.. code:: ini

    [tlc "legacy"]
    lib_device = /dev/changer1,/dev/changer2

*Listen hostname*
-----------------

The **listen_hostname** parameter defines the hostname to which the server
should bind. If this parameter is not specified, the TLC will use the value of
the parameter **hostname** to bind to. This parameter allows a different
internal bind than the hostname. This is useful with a load balancer who listens
on **hostname** and then redirects on **listen_hostname**.

If this parameter is not specified, Phobos will use the **hostname**.

Example:

.. code:: ini

    [tlc "legacy"]
    listen_hostname = 0.0.0.0

*Listen interface*
------------------

The **listen_interface** parameter defines the network interface to which the
server should bind. This can be used to restrict the server to a specific
network interface.

If this parameter is not specified, Phobos defaults to the following:
**listen_interface = null**.

Example:

.. code:: ini

    [tlc "legacy"]
    listen_interface = admin0

*Listen port*
-------------

The **listen_port** parameter defines the port number to which the server should
bind. This is useful for load balancing.

If this parameter is not specified, Phobos will use the **port**.

Example:

.. code:: ini

    [tlc "legacy"]
    listen_port = 20123

*Hostname*
----------

The **hostname** parameter defines the server address for the TLC. This address
will be used by the TLC's clients (the phobosd daemon instance) to communicate
with the TLCs. Also, if **listen_hostname** is not specified, the TLC will use
the **hostname** to bind to.

If this parameter is not specified, Phobos defaults to the following:
**hostname = localhost**.

Example:

.. code:: ini

    [tlc "legacy"]
    hostname = localhost

*Port*
------

The **port** parameter defines the port number on which the TLC server is
listening.

If this parameter is not specified, Phobos defaults to the following:
**port = 20123**.

Example:

.. code:: ini

    [tlc "legacy"]
    port = 20123
