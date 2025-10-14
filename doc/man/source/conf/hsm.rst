[hsm] - Configuring the HSM commands
====================================

This section explains how to configure the HSM commands. These commands allow
copying and moving objects between storage tier to achieve hierarchical storage
management.

The phobos_hsm_sync_dir command creates a new destination copy for each
object that has a source copy with extents on dirs owned by the host which
is running the command. This command only takes into account the extents
that are into a time window beginning from the last time the command was called
(from **synced_time_path**) to the current time minus a **sync_delay_second**.

The phobos_hsm_release_dir command deletes copies of objects on the local dirs.
If the fill rate of one local dir is above the **release_higher_threshold**,
the phobos_hsm_release_dir command deletes copies of object with extents on this
dir to decrease the fill rate under the **release_lower_threshold**. To be
deleted, a "to release" copy must have an existing backend copy. The older
copies are deleted first. "To release" copies with a creation time younger than
"current_time - **release_delay_second**" are not deleted.

*Recording and using the last synced time*
------------------------------------------

phobos_hsm_sync_dir records the time of the last previously executed sync. All
extents older than this limit will not be checked by a new sync command because
they are supposed to be already synced.

The **synced_time_path** parameter defines the path to the file containing this
last already synced time.

The synced ctime file must contains a 'date +"%Y-%m-%d %H:%M:%S.%6N"' value,
of format"YYYY-mm-dd HH:MM:SS.uuuuuu" as "2025-09-26 18:17:07.548048", always
26 characters.

If this parameter is not specified, Phobos defaults to the following:
**synced_time_path = /var/lib/phobos/hsm_synced_ctime**.

Example:

.. code:: ini

    [hsm]
    synced_time_path = /var/lib/phobos/hsm_synced_ctime

The hsm sync command takes into account the registered date to filter the
extents that it needs to check and updates this date.

If this file does not exist, the hsm sync commands check the existing objects
without any old limit and will create this file to register the new synced
time.

*Sync delay*
------------

The **sync_delay_second** parameter defines the minimum delay in second before
any new object is synced. It avoids syncing an object that is too
recent before this object is deleted.

If this parameter is not specified, Phobos defaults to the 0 value and all
recent objects are synced.

Example:

.. code:: ini

    [hsm]
    sync_delay_second = 3600

*Release delay*
-----------------

The **release_delay_second** parameter defines the minimum delay in second
between the creation time of a copy and its deletion by a release.

If this parameter is not specified, Phobos defaults to the 0 value and all
recent objects could be released.

Example:

.. code:: ini

    [hsm]
    release_delay_second = 1800

*dir_release_higher_threshold*
--------------------------------

The dir_release_higher_threshold parameter is the limit to start to release
copies on a dir. This parameter must be a percentage, a positive integer value
between 1 and 100. dir_release_higher_threshold must be strictly higher than
dir_release_lower_threshold. A value of 100 means that no release purge will
never happens.

If this parameter is not specified, Phobos defaults to the 95 value.

Example:

.. code:: ini

    [hsm]
    dir_release_higher_threshold = 95

*dir_release_lower_threshold*
-------------------------------

The dir_release_lower_threshold parameter is the limit to achieve when a release
is started on a dir. This parameter must be a percentage, a positive integer
value between 0 and 99. dir_release_lower_threshold must be strictly lower than
dir_release_higher_threshold. A value of 0 means that if a release purge starts,
every selectable copy will be deleted.

If this parameter is not specified, Phobos defaults to the 80 value.

Example:

.. code:: ini

    [hsm]
    dir_release_higher_threshold = 80
