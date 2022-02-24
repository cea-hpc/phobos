# Phobos listing commands

Some of the main targets (`drive`, `dir`, `tape`, `object`, `extent`)
have a `list` command associated. They display various information depending on
the resource asked for listing: if a resource is specified, only that resource
information will be displayed; if no resource is specified, all resources
currently in the associated table of the database will be displayed.

We will here detail the listing commands, their options and outputs. However,
if you want more information on the content of the database, please read
[database.md](database.md).

## General options

* `phobos <target> list {-f|--format} fmt`
  * format the output of the command, supported `fmt` values are:
    * human (default)
    * xml
    * json
    * csv
    * yaml
* `phobos <target> list {-o|--output} columns`:
  * `columns`: select the fields to display, written in CSV format (e.g.
    `phobos object list -o oid,user_md`)
  * selectable columns depend on the target being displayed
  * the columns `lock_hostname`, `lock_owner` and `lock_ts` are available for
    `drive`, `dir` and `tape`. They correspond to details about a concurrency
    lock held out by a specific phobosd, and mean:
    * `lock_hostname`: the hostname of the owner
    * `lost_owner`: the pid of the phobosd
    * `lock_ts`: the moment the lock was taken (written as a timestamp)

## Drive options

The list command can be used on a single `name` to display that specific drive.

* `phobos drive list {-o|--output} columns`:
  * `adm_status`: admin lock status, either `locked`, `unlocked` or `failed`
  * `family`: family type of the drive
  * `host`: the hostname of the node managing the drive
  * `lock_hostname`, `lock_owner` and `lock_ts`
  * `model`: the model of the drive
  * `name`: serial number of the drive
  * `path`: path to the linux device in /dev

* `phobos drive list {-m|--model} model`:
  * only display drives matching the given model

## Media options

The `dir` and `tape` objects are separate, but their list command options are
the same, and they are both a type of media.

The list command can be used on a single `name` to display that specific medium.

* `phobos {dir|tape} list {-o|--output} columns`:
  * `addr_type`: how to access the data stored, either `PATH`, `HASH1` or
    `OPAQUE`
  * `adm_status`: admin lock status, either `locked`, `unlocked` or `failed`
  * `put_access`, `get_access` and `delete_access`: booleans showing if the
    put, get and delete operations are allowed on the media
  * `family`: family type of the drive, either `tape` or `dir`
  * `lock_hostname`, `lock_owner` and `lock_ts`
  * `model`: the model of the medium
  * `name`: how the media is refered to as
  * `tags`: user or system provided metadata for the medium
  * `fs.*`: information regarding the filesystem on the medium:
    * `fs.type`: type of the filesystem, either `POSIX` or `LTFS`
    * `fs.status`: status of the filesystem, either `blank` (unformatted media),
      `empty` (formatted but without data stored), `used` (formatted with data
      stored) or `full` (formatted and completely full of data)
    * `fs.label`: name of the filesystem
  * `stats.*`: various stats on the medium:
    * `stats.nb_obj`: number of objects stored on the medium
    * `stats.logc_spc_used`: sum of the size of all the objects on the medium
    * `stats.phys_spc_used`: correspond to `total_space - free_space` (may be
      different from `logc_spc_used` because of compression or metadata written
      alongside the objects), retrieved from statfs
    * `stats.phys_spc_free`: free space on the medium, retrieved from statfs
    * `stats.nb_load`: number of times the media was loaded into a drive
    * `stats.nb_errors`: number of errors that ocurred with the medium
    * `stats.last_load`: epoch of the last object load

* `phobos {dir|tape} list {-T|--tags} tags`:
  * only display media with tags containing the ones given (written as CSV)

## Object options

The list command can be used on a single `oid` to display that specific object.

* `phobos object list {-o|--output} columns`:
  * `oid`: oid of the object
  * `uuid`: unique identifier of the object
  * `version`: the current version of the object
  * `user_md`: user-provided metadata of the object

* `phobos object list {-m|--metadata} md`:
  * only display objects that have the given metadata (written as 'key=value'
CSV)

* `phobos object list {-d|--deprecated}`:
  * also display deprecated objects. Add the `deprec_time` column to the output,
    showing when the object was deprecated

* `phobos object list {-p|--pattern}`:
  * filter the objects to display based on the given POSIX regexp instead of
    exact oid

## Extent options

The extents correspond to the logical location of the objects, depending on
their layout, the number of replicas, etc. As such, they are by default grouped
by the object they refer to.

The list command can be used on a single `oid` to display the extents of the
corresponding object.

* `phobos extent list {-o|--output} columns`:
  * `address`: where the extents of the object are stored on the media (written
    as a list of strings)
  * `ext_count`: number of extents of the object
  * `family`: the family of the media used to store the extents of the objects
    (written as a list of either `dir` or `tape`)
  * `layout`: name of the layout used to store the object
  * `media_name`: name of the media used to store the object (written as a list
    of strings)
  * `oid`: oid of the object the extents correspond to
  * `size`: size of the extents on each media
  * `uuid`: uuid of the object the extents correspond to
  * `version`: version of the object the extents correspond to

* `phobos extent list {-n|--name} name`:
  * only display extents stored on the given medium name

* `phobos extent list {--degroup}`:
  * display extents by themselves and not grouped by the objects they correspond
    to

* `phobos extent list {-p|--pattern}`:
  * filter the extents to display based on the given POSIX regexp instead of
    exact extent
