[copy] - Configuring the object copies
======================================

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

*Copy name link with a profile*
-------------------------------

A copy name can be linked to a profile. When a new copy is created, it will
check whether the copy name is associated with a profile. If so, the put
parameters specified in the profile will be used to create the new copy.

To define which profile a copy should use, a new section must be added to the
configuration file using the following format: **[copy "%s"]**, where "%s"
should be replaced with the copy name.

If no section is specified, Phobos will not use a profile when creating the new
copy.

Example:

.. code:: ini

    [copy "cache"]
    profile = fast
