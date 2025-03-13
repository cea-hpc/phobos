[io] - Configuring the I/O
==========================

This section explains how to configure the I/O operations in Phobos. All I/O
related parameters should be listed under the **[io]** section. The available
parameters are listed below.

*fs_block_size*
---------------

The **fs_block_size** parameter specifies the block size of the mounted file
system to Phobos. This value is used to calculate the exact size of a put after
the synchronization of the file system.

It helps prevent errors when performing a no-split mput. Without this parameter,
the total size of all objects after synchronization may exceed the available
space on the allocated media. Phobos uses this parameter to calculate the
required size when allocating a medium.

Its value must be specified as a list of key = value pairs, separated by commas
without spaces, for each family.

If this parameter is not specified, Phobos defaults to the following:
**fs_block_size = tape=524288,dir=1024,rados_pool=1024**.

Example:

.. code:: ini

    [io]
    fs_block_size = tape=524288,dir=1024

*io_block_size*
---------------

The **io_block_size** parameter forces the block size (in bytes) used for
writing data to all media. If the value is null or not specified, Phobos will
use the value provided by the storage system (statfs.f_bsize, see statfs(2)).

Its value must be specified as a list of "key=value" pairs, separated by
commas, where key is a family managed by Phobos (i.e. `tape`, `dir` or
`rados_pool`) and value is block size in bytes.

If this parameter is not specified, Phobos defaults to the following:
**io_block_size = tape=0,dir=0,rados_pool=0**.

Example:

.. code:: ini

    [io]
    io_block_size = tape=1048576,dir=1048579,rados_pool=1048579
