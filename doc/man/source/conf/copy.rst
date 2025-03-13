Configuring the object copies
=============================

This section explains how to configure the copies in Phobos. All the
copy-related parameters should be listed under the **[copy]** section.

*default_copy_name*
-------------------

The **default_copy_name** parameter defines the default name used by Phobos when
creating a new copy. Its value can be overridden with the **--copy-name** option
at put or when creating a new copy.

If this parameter is not specified, Phobos defaults to the following:
**default_copy_name = ""**.

Example:

.. code:: ini

    [copy]
    default_copy_name = source

*get_preferred_order*
---------------------

The **get_preferred_order** parameter defines a priority-ranked list of copies
that Phobos will retrieve. Phobos will attempt to retrieve the first copy in the
list, and if it fails, it will proceed to the next one, continuing in order
until all options have been exhausted. This parameter is used by the get and
locate operations. This parameter can be overridden by specifiyng the
**--copy-name** option to the get and locate commands.

The value must be a list separated by commas without spaces.

If this parameter is not specified, Phobos defaults to the following:
**get_preferred_order = null**

Example:

.. code:: ini

    [copy]
    get_preferred_order = fast,cache
