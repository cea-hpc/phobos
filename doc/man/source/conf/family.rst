Configuring the storage family
==============================

This section explains how to configure the families supported by Phobos.
Currently, Phobos supports three families: **dir**, **tape** and **rados_pool**.
All family-related parameters should be listed under the **[dir]**, **[tape]**
and **[rados_pool]** section.

*Common parameters*
-------------------

Some parameters can be defined individually for each family, allowing different
values for each. Below is a list of all the parameters that can be configured
for the families supported by Phobos. In this context, **%s** can be replaced
with any of the families supported by Phobos.


*%s_full_threshold*
~~~~~~~~~~~~~~~~~~~

The **%s_full_threshold** parameter specifies the reserved space in percentage
for a medium of this family. If the used space on a medium exceeds the
total capacity times the reserved percentage, the medium is considered full.

If this parameter is not specified, Phobos defaults to the following for each
family:

    * **tape_full_threshold = 5**
    * **dir_full_threshold = 1**
    * **rados_pool_full_threshold = 1**

With a threshold at 5%, a tape is considered full if its used space is at 95% of
its total capacity.

Example:

.. code:: ini

    [tape]
    tape_full_threshold = 5

    [dir]
    dir_full_threshold = 1

    [rados_pool]
    rados_pool_full_threshold = 1
