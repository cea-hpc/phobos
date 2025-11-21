% Phobos admin commands

# First steps
## Launching phobosd
To use phobos, a daemon called phobosd needs to be launched. It mainly manages
the drive/media allocation and sets up resources to read or write your data.

To launch/stop the daemon:
```
# systemctl start/stop phobosd
```

## Scanning libraries
Use the `phobos lib scan` command to scan a given library. For instance, the
following will give the contents of the default tape library:
```
phobos lib scan
```

# Adding drives
Use the `phobos` command line to add new tape drives to be managed by phobos.

It is recommanded to specify a device path which is persistent after reboot
(eg. managed by dev mapper).

By default, drives are added in a locked state, so they are not immediatly used
in production.

To enable using a drive in production as soon as it is added, specify the
'--unlock' option:
```
phobos drive add --unlock /dev/mapper/LTO6-012345
```

# Migrating drives
Use `phobos drive migrate` to change the host of drives.

Drives information will be updated only if they are not currently used by the
system i.e. no daemon is using them.

```
phobos drive migrate host2 /dev/mapper/LT06-012345
```

# Removing drives
Use the `phobos drive delete` command line to remove tape drives from phobos
system. Drives will only be removed from the phobos database. No specific
operations are done. For instance, you will still need to perform a SCSI release
command to change the host of a drive:

```
ltfs -o release_device -o devname=/dev/mapper/LT06-012345
```

Drives will be removed only if they are not currently used by the system
i.e. no daemon is using them.

```
phobos drive delete /dev/mapper/LTO6-012345
```

# Adding media
To add tapes to be managed by phobos, use the `phobos` command line.
It is mandatory to specify a media type (like LTO8, T10KD...) with option `-t`:
```
phobos tape add --type lto6 [073200-073222]L6
```
**Note1:** this operation requires no access to the medium.

**Note2:** the set of supported media types, drive types, and compatibility rules
between drives and media are configurable. See section '*Configuring device
and media types*' below for more details.

Once a media has been added, it needs to be formatted. You can optionally unlock
the tape when the operation is done, so it becomes usable in production.
```
phobos tape format --unlock [073200-073222]L6
```

Phobos can format multiple media at the same time (provided there are enough
devices to do so), and you can limit that number using the `nb-streams` command
option:
```
phobos tape format --unlock --nb-streams 3 [073200-073222]L6
```
With this command, a maximum of 3 requests for media formatting will be sent
at a time, and the client will wait a format is done before sending another.
If nothing or 0 is specified, no limit is applied.

Phobos can force the format of a tape, even if its status is not blank, by
using the following command. **Remember that using this option may lead to
orphan extents and/or lost objects.**
```
phobos tape format --force 073200L6
```
The option is not yet available for directories and rados pools.

# Repack media
To repack a medium, use the following command:
```
phobos tape repack 073200L6
```

The operation will copy any alive extent from the 073200L6 tape to another empty
tape and format the old one.

You can provide tags to define the empty tape:
```
phobos tape repack --tags repack_target 073200L6
```

The repack operation is available for tapes only.

# Listing resources
Any device or media can be listed using the 'list' operation. For instance,
the following will list all the existing tape identifiers:
```
phobos tape list
```

The 'list' output can be formatted using the --output option, following a list
of comma-separated attributes:
```
phobos tape list --output stats.phys_spc_used,tags,adm_status
```

If the attribute 'all' is used, then the list is printed with full details:
```
phobos tape list --output all
```

Other options are available using the --help option.

The file system status of a medium can be one of the following:
* blank: medium is not formatted;
* empty: medium is formatted, no data written to it;
* used: medium contains data;
* full: medium is full, no more data can be written to it.

## Querying the status of the drives

In order to have a complete view of the drives (which tape is loaded, ongoing
I/Os, etc.) a query can be sent to the daemon through:

```
phobos drive status
|   address | currently_dedicated_to   | device   | media    | mount_path       | name     | ongoing_io   |     serial |
|-----------|--------------------------|----------|----------|------------------|----------|--------------|------------|
|         6 | R                        | /dev/sg7 |          |                  | /dev/sg7 |              | 1843022660 |
|         7 | W                        | /dev/sg8 | Q00000L6 | /mnt/phobos-sg8  | /dev/sg8 | False        | 1843022660 |
```

`address` is the drive index as shown by `mtx status`. `ongoing_io` can be
either true or false indicating whether an I/O is ongoing on the corresponding
drive.

`currently_dedicated_to` can be a combination of R (Read), W (Write) and F
(Format). In the above example, we can see that the drive `/dev/sg7` only has
 `R`. This means that this drive will only be used by Phobos to read objects. It
won't be used for writing data or formating tapes. Similarly, the drive
`/dev/sg8` will only write data. In this example, no drive will be used to
format tapes. In such a case, you would expect any `phobos tape format` command
to be blocking until a drive is dynamicaly allocated to the format request
scheduler by the device fair share algorithm or by an admin configuration
modification.

This parameter is linked to the `dispatch_algo` that is documented in the
configuration [template](doc/cfg/template.conf) under the `io_sched` section.

# Locking resources
A device or media can be locked. In this case it cannot be used for
subsequent 'put' or 'get' operations:

```
phobos drive {lock|unlock} <drive_serial|drive_path>
phobos media {lock|unlock} <media_id>
```

The 'lock' and 'unlock' commands are asynchronous by default: the resource
status is updated in the database, then the daemon is notified for a device
update.

The 'drive lock' command accepts a --wait option, which waits for the complete
release of the drive by the daemon before returning.

# Setting access
Media can be set to accept or reject some types of operation. We distinguish
three kinds of operation: put (P), get (G) and delete (D).

```
# Set medium access: allow get and delete, forbid put
phobos tape set-access GD 073000L8

# Add access: add put and get (delete is unchanged)
phobos dir set-access +PG /path/to/dir

# Remove access: remove put (get and delete are unchanged)
phobos tape set-access -- -P 073000L8
```

# Tagging resources
Phobos implements a flexible mechanism to partition storage resources into
(possibly overlapping) subsets.

This mechanism is based on **tags**.

Multiple tags can be associated to the storage media at creation time or later
on:
```
# Add media with tags "class1" and "project2"
phobos tape add --tags class1,project2 --type LTO8 [073000-073099]L8

# Update tags for these tapes
phobos tape update --tags class2 [073000-073099]L8

# Clear tags
phobos tape update --tags '' [073000-073099]L8
```

Media can be listed following their tags:
```
phobos tape list --tags class1,project2
```

When pushing objects to phobos, you can then restrict the subset of resources
that can be used to store data by specifying tags with the `--tags` option.

```
# The following command will only use media with tags 'class1' AND 'projX'
phobos put --tags class1,projX /path/to/obj objid
```

* If several tags are specified for put, phobos will only use media having
**all** of the specified tags.
* A media is selected if its tags **include** the tags specified for the put
operation.

The following table illustrates conditions for matching tags:

| put tags    | media tags | match   |
|-------------|------------|---------|
| (none)      | (none)     | **yes** |
| (none)      | A,B        | **yes** |
| A           | **A**,B    | **yes** |
| A,B         | **A,B**    | **yes** |
| A,B,C       | A,B        | no      |
| A           | (none)     | no      |
| C           | A,B        | no      |
| A,C         | A,B        | no      |

# Locating media
Phobos allows you to locate its media by answering you on which host you should
execute phobos commands or calls that require access to a specific medium.

```
phobos tape locate 073000L8
phobos dir locate /path/to/dir
```

# Media families

For now, phobos supports 2 families of storage devices: tapes and directories.

* Tapes are media accessed by drives.
  * Drives are managed by `phobos drive ...` commands
  * Tapes are managed by `phobos tape ...` commands
* Directories are self-accessible. They do not need drives to be read.
  * Directories are managed by `phobos dir ...` commands.

To specify the default family where data is to be written, specify it in the
phobos configuration file:

```
[store]
# indicate 'tape' or 'dir' family
default_family = tape
```

Alternatively, you can override this value by using the '--family' option of
your put commands:

```
# put data to directory storage
phobos put --family dir file.in obj123
```

# Storage layouts

For now, phobos only supports the 'raid1' storage layout, for mirroring.

The 'simple' storage layout can be obtained by modifiying the number of
replicas to 1.

The default layout to use when data is written can be specified in the phobos
configuration file, in case multiple are defined:

```
[store]
default_layout = raid1
```

If a profile is defined, you can set it as default instead of the layout:

```
[store]
default_profile = simple

[profile "simple"]
layout = raid1
lyt-params = repl_count=1
```

Alternatively, you can override this default value by using the 'right' option
in your put commands:

```
# put data using a raid1 layout
phobos put --layout raid1 file.in obj123

# put data using a simple profile
phobos put --profile simple file.in obj123
```

Layouts can have additional parameters. For now, the only additional parameter
is the number of replicas for raid1. This is for now specified using the
following env variable:

```
# put data using a raid1 layout with 3 replicas
PHOBOS_LAYOUT_RAID1_repl_count=3 phobos put --layout raid1 file.in obj123
```

# Configuring device and media types

## Supported tape models

The set of tape technologies supported by phobos can be modified by
configuration.

This list is used to verify the model of tapes added by administrators.
```
[tape_model]
# comma-separated list of tape models, without any space
# default: LTO5,LTO6,LTO7,LTO8,T10KB,T10KC,T10KD
supported_list = LTO5,LTO6,LTO7,LTO8,T10KB,T10KC,T10KD
```
