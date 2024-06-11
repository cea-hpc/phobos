# Multi Library

## Goal
Some phobos systems can be extended to more than one tape library. Phobos
should be able to manage tapes and drives split among several libraries.

Even if it is less obvious, we can also consider that dirs could be separated
into different pools and rados pools can belong to different clusters. So, this
concept of distributing media across severals separated groups seems to be
relevant for any medium family.

## Configuration
Because one tlc must run per library, we will replace the `[tlc]` section by as
many `[tlc "library name"]` sections as there are existing libraries, where
`"library_name"` must match the value of the corresponding library field of
media and devices.

Each `[tlc "library name"]` section will contain the same field as the
former `[tlc]` section:
- `hostname`,
- `port`,
- `listen_hostname`,
- `listen_port`,
- `lib_device`.

We will add one field per medium family to the `[store]` section:
- `default_dir_library = default_dir_pool_name`,
- `default_rados_library = default_rados_cluster_name`,
- `default_tape_library = default_tape_library_name`.

## Media, device, extent and logs DSS tables and the pho_id structure
We add one `varchar(255) NOT NULL` library column in the media, the device,
the extent and the logs tables. We will add this new `library` field to the
primary key of the media and device tables in addition to the existing
`family` and `id` ones. The id column of the device and media table are no more
'UNIQUE' because we consider that we can have the same medium id in two
different libraries.


We also add the corresponding `char *` library field in the `struct pho_id`
structure.

### DSS schema migration
The new library field will be filled with the default value from the
configuration file.

## Device selection
The new `library` criteria needs to be taken into account by the
`tape_drive_compat` function for put, get and format actions.

For locate, in addition to the `tape_drive_compat_models` function, the library
compatibility must also be taken into account.

## The new `library` parameter of API, CLI and TLC service script

The library id should be added as a new optional parameter of the device, media
and lib commands. If this parameter is not set, the default value from the
configuration will be used.

We add a new admin command to change the library id of a device or a medium.

We add a new admin command to change a library name to a new one. All devices
and media with the library name value will have the new library name. This
command must be secured against any running LRS by checking existing DSS locks.

At put, we will add a new optional library parameter to fix the library on
which the object must be created. This new "put" option will also be taken into
account by the alias layer.

The TLC daemon system script must also offer the possibility to set the library
name.
