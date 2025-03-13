[layout_raid1|raid4] - Configuring the layouts
==============================================

This section explains how to configure the available layouts in Phobos.
Currently, Phobos supports two layouts: **raid1** and **raid4**. All
layout-related parameters should be listed under the **[layout_raid1]** and
**[layout_raid4]** section, respectively.

Common parameters
-----------------

Some parameters can be defined individually for each layout, allowing different
values for each. Below is a list of all the parameters that can be configured
for the layouts supported by Phobos.

*check_hash*
~~~~~~~~~~~~

The parameter **check_hash** is a boolean indicating whether Phobos should
verify the checksum integrity of each extent used when performing a get
operation. If one of the extents used has a checksum different from the one
stored when it was written, the get operation fails with an error.

The only value expected is **true** or **false**.

If this parameter is not specified, Phobos defaults to the following for each
layout:

    * raid1 : **check_hash = true**
    * raid4 : **check_hash = true**

Example:

.. code:: ini

    [layout_raid1]
    check_hash = true

    [layout_raid4]
    check_hash = true

*extent_md5*
~~~~~~~~~~~~

The parameter **extent_md5** is a boolean indicating whether Phobos should
compute the MD5 value of each written extent.

The **extent_md5** and **extent_xxh128** are independant. They can be both set
to true and Phobos will compute the MD5 value and XXH128 value for each extent.

If this parameter is not specified, Phobos defaults to the following for each
layout:

    * If Phobos is compiled with md5:

        * raid1 : **extent_md5 = true**
        * raid4 : **extent_md5 = true**

    * If phobos is not compiled with md5:

        * raid1 : **extent_md5 = false**
        * raid4 : **extent_md5 = false**

Example:

.. code:: ini

    [layout_raid1]
    extent_md5 = false

    [layout_raid4]
    extent_md5 = false

*extent_xxh128*
~~~~~~~~~~~~~~~

The parameter **extent_xxh128** is a boolean indicating whether Phobos should
compute the XXHASH128 (https://github.com/Cyan4973/xxHash/blob/dev/doc/xxhash_spec.md)
value of each written extent. In environments where xxhash has a version lower
than 0.8.0, this parameter is ignored and the xxh128 is not computed. If set to
"true", the client will issue a warning to indicate this inconsistency.

The **extent_md5** and **extent_xxh128** are independant. They can be both set
to true and Phobos will compute the MD5 value and XXH128 value for each extent.

If this parameter is not specified, Phobos defaults to the following for each
layout:

    * If Phobos is compiled with xxh128:

        * raid1 : **extent_xxh128 = true**
        * raid4 : **extent_xxh128 = true**

    * If phobos is not compiled with xxh128:

        * raid1 : **extent_xxh128 = false**
        * raid4 : **extent_xxh128 = false**

Example:

.. code:: ini

    [layout_raid1]
    extent_xxh128 = true

    [layout_raid4]
    extent_xxh128 = true

Raid1 specific parameters
-------------------------

*repl_count*
~~~~~~~~~~~~

The parameter **repl_count** defines the number of data replicas to create when
doing a put operation. It can be overridden by using the **--layout-params**
or **--profile** options. A replica count of 1 means that there is only one copy
of the data (the original), and 0 additional copies of it. Therefore, a replica
count of 2 means the original copy of the data, plus 1 additional copy.

If this parameter is not specified, Phobos defaults to the following:
**repl_count = 2**.

Example:

.. code:: ini

    [layout_raid1]
    repl_count = 2
