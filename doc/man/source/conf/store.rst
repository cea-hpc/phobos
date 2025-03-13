Configuring the store
=====================

This section explain how to configure the store operations in Phobos. All
store-related parameters should be listed under the **[store]** section.

*default_family*
----------------

The **default_family** parameter defines the family used by default in Phobos
when performing a put operation, if no family is specified. It can be overridden
by using the **--family** or **--profile** options.

If this parameter is not specified, Phobos defaults to the following:
**default_family = tape**.

Example:

.. code:: ini

    [store]
    default_family = tape

*default_layout*
----------------

The **default_layout** parameter defines the layout used by default in Phobos
when performing a put operation, if no layout is specified. It can be overridden
by using the **--layout** or **--profile** options.

If this parameter is not specified, Phobos defaults to the following:
**default_layout = raid1**.

Example:

.. code:: ini

    [store]
    default_layout = raid1

*default_profile*
-----------------

The **default_profile** parameter defines the profile used by default in Phobos
when performing a put operation, if no profile is specified. If a profile is
provided, it will override the default value with its own.

If this parameter is not specified, Phobos defaults to the following:
**default_profile = none**.

Example:

.. code:: ini

    [store]
    default_profile = simple

    [profile "simple"]
    layout = raid1
    lyt-params = repl_count=1
    library = legacy

*default_%s_library*
--------------------

The **default_%s_library** parameter defines the library used by default in
Phobos when performing a put operation, if no library is specified. It can be
overridden by using the **--library** or **--profile** options. Each family can
have a different default library. Here, "%s" should be replaced with the family
name.

If this parameter is not specified, Phobos defaults to the following for each
family:

    * **default_tape_library = none**
    * **default_dir_library = none**
    * **default_rados_library = none**

Example:

.. code:: ini

    [store]
    default_tape_library = legacy
    default_dir_library = legacy
    default_rados_library = legacy
