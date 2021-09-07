# Phobos documentation

## Installation
### Requirements for tape access
You need to install LTFS >= 2.4 to enable phobos access to tapes.

LTFS RPM can be found on IBM Fix Central: [ltfs 2.4](https://www-945.ibm.com/support/fixcentral/swg/selectFixes?parent=Tape%20drivers%20and%20software&product=ibm/Storage_Tape/Long+Term+File+System+LTFS&release=2.4&platform=Linux&function=all)

You can also retrieve its sources on gihub:
https://github.com/LinearTapeFileSystem/ltfs

If you want to build RPMs from these sources, you can find packaging resources
(i.e. spec file) for LTFS here: https://github.com/piste2750/rpm-ltfs

Note: since LTFS 2.4, `lin_tape` driver is no longer needed to access tapes.
LTFS now uses the standard linux tape driver (st).

### Phobos installation
Install phobos and its requirements:
```
yum install phobos
```

### Database setup

#### On RHEL8/CentOS8

Install postgresql:
```
dnf install postgresql-server postgresql-contrib
```

Initialize postgresql directories:
```
postgresql-setup --initdb --unit postgresql
```

Edit `/var/lib/pgsql/data/pg_hba.conf` to authorize access from phobos host
(localhost in this example):
```
# "local" is for Unix domain socket connections only
local   all             all                                     trust
# IPv4 local connections:
host    all             all             127.0.0.1/32            md5
# IPv6 local connections:
host    all             all             ::1/128                 md5
```

Start the database server:
```
systemctl start postgresql
```

Finally, create phobos database and tables as postgres user (the password for
SQL phobos user will be prompted for unless provided with -p):
```
sudo -u postgres phobos_db setup_db -s
```

#### On RHEL7/CentOS7

Install postgresql version >= 9.4 (from EPEL or any version compatible with
Postgres 9.4):
```
yum install postgresql94-server postgresql94-contrib
```

Initialize postgresql directories:
```
/usr/pgsql-9.4/bin/postgresql94-setup initdb
```

Edit `/var/lib/pgsql/9.4/data/pg_hba.conf` to authorize access from phobos host
(localhost in this example):
```
# "local" is for Unix domain socket connections only
local   all             all                                     trust
# IPv4 local connections:
host    all             all             127.0.0.1/32            md5
# IPv6 local connections:
host    all             all             ::1/128                 md5
```

Start the database server:
```
systemctl start postgresql-9.4.service
```

Finally, create phobos database and tables as postgres user (the password for
SQL phobos user will be prompted for unless provided with -p):
```
sudo -u postgres phobos_db setup_db -s
```

#### Error on database setup/migration
If the database setup failed because `generate_uuid_v4()` is missing, it means
the psql extension `uuid-ossp` is missing. To make it available, execute the
following command as SQL phobos user. Phobos user needs to have `create` rights
to execute the command.

```
CREATE EXTENSION "uuid-ossp";
```

In case SQL phobos user does not have `create` rights, you can still use the
first following command to give them to it and change them back with the second
one:

```
ALTER USER phobos WITH SUPERUSER;
ALTER USER phobos WITHOUT SUPERUSER;
```

## Upgrade of an existing instance

After upgrading phobos, run the DB conversion script (credentials to connect to
the database will be retrieved from /etc/phobos.conf):
```
/usr/sbin/phobos_db migrate
# 'y' to confirm
```

## First steps
### Launching phobosd
To use phobos, a daemon called phobosd needs to be launched. It mainly manages
the drive/media allocation and sets up resources to read or write your data.

To launch the daemon:
```
# systemctl start phobosd
```

To stop the daemon:
```
# systemctl stop phobosd
```

phobosd comes with its option set:
- -i: launch phobosd in an interactive way (attached to a terminal)
- -c: path to phobosd configuration file
- -s: print log messages into syslog
- -q/v: decrease/increase log verbosity

### Adding drives
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
### Adding media
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
phobos tape format --unlock 073200L6
```

### Writing data

#### Using the API
You can access data in phobos using the phobos API:

* Header file is `phobos_store.h`.
* Library is `libphobos_store` (link using `-lphobos_store`).

Calls for writing and retrieving objects are `phobos_put()` and
`phobos_get()`.

#### Using the CLI
The CLI wraps `phobos_put()` and `phobos_get()` calls.

##### Writting objects
When pushing an object you need to specify an **identifier** that can be later
used to retrieve this data.

Phobos also allows specifying an arbitrary set of attributes (key-values) to be
attached to the object. They are specified when putting the object using the
'-m' option.
```
# phobos put <file>       <id>      --metadata  <k=v,k=v,...>
phobos put  myinput_file  foo-12345 --metadata  user=$LOGNAME,\
    put_time=$(date +%s),version=1
```
For better performances when writing to tape, it is recommanded to write batches
of objects in a single put command. This can be done using `phobos mput`
command.

`phobos mput` takes as agument a file that contains a list of input files (one
per line) and the corresponding identifiers and metadata, with the following format:

`<src_file>  <object_id>  <metadata|->`.

Example of input file for mput:
```
/srcdir/file.1      obj0123    -
/srcdir/file.2      obj0124    user=foo,md5=xxxxxx
/srcdir/file.3      obj0125    project=29DKPOKD,date=123xyz
```
The mput operation is simply:
```
phobos mput list_file
```
##### Reading objects
To retrieve the data of an object, use `phobos get`. Its arguments are the
identifier of the object to be retrieved, as well as a path of target file.

For example:
```
phobos get obj0123 /tmp/obj0123.back
```

##### Reading object attributes
To retrieve custom object metadata, use `phobos getmd`:
```
$ phobos getmd obj0123
cksum=md5:7c28aec5441644094064fcf651ab5e3e,user=foo
```

##### Listing objects
To list objects, use `phobos object list`:
```
$ phobos object list --output oid,user_md
| oid   | user_md         |
|-------|-----------------|
| obj01 | {}              |
| obj02 | {"user": "foo"} |
```

You can add a pattern to match object oids:
```
phobos object list "obj.*"
```

The accepted patterns are Basic Regular Expressions (BRE) and Extended
Regular Expressions (ERE). As defined in [PostgreSQL manual](https://www.postgresql.org/docs/9.3/functions-matching.html#POSIX-SYNTAX-DETAILS),
PSQL also accepts Advanced Regular Expressions (ARE), but we will not maintain
this feature as ARE is not a POSIX standard.

The option '--metadata' applies a filter on user metadata
```
phobos object list --metadata user=foo,test=abd
```

##### Listing extents
To list extents, use `phobos extent list`:
```
$ phobos extent list --output oid,ext_count,layout,media_name
| oid  | ext_count | layout | media_name             |
|------|-----------|--------|------------------------|
| obj1 |         1 | simple | ['/tmp/d1']            |
| obj2 |         2 | raid1  | ['/tmp/d1', '/tmp/d2'] |
```

The option `--degroup` outputs an extent per row instead of one object per row:
```
$ phobos extent list --degroup --output oid,layout,media_name
| oid  | layout | media_name  |
|------|--------|-------------|
| obj1 | simple | ['/tmp/d1'] |
| obj2 | raid1  | ['/tmp/d1'] |
| obj2 | raid1  | ['/tmp/d2'] |
```

The option `--name` applies a filter on a medium's name. Here, the name does not
accept a pattern.
```
phobos extent list --name tape01
```

You can add a POSIX pattern to match oids, as described in "Listing objects"
section:
```
phobos extent list "obj.*"
```

## Device and media management
### Listing resources
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

### Locking resources
A device or media can be locked. In this case it cannot be used for
subsequent 'put' or 'get' operations:

```
phobos drive {lock|unlock} <drive_serial|drive_path>
phobos media {lock|unlock} <media_id>
```

### Tagging resources
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
phobos dir update --tags '' [073000-073099]L8
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


### Media families

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

### Storage layouts

For now, phobos supports 2 storage layouts: simple and raid1 (for mirroring).

The default layout to use when data is written can be specified in the phobos
configuration file:

```
[store]
# indicate 'simple' or 'raid1' layout
default_layout = simple
```

Alternatively, you can override this value by using the '--layout' option of
your put commands:

```
# put data using a raid1 layout
phobos put --layout raid1 file.in obj123
```

Layouts can have additional parameters. For now, the only additional parameter
is for raid1 number of replicas. This is for now specified using the following
env variable:

```
# put data using a raid1 layout with 3 replicas
PHOBOS_LAYOUT_RAID1_repl_count=3 phobos put --layout raid1 file.in obj123
```

### Configuring device and media types

#### Supported tape models

The set of tape technologies supported by phobos can be modified by
configuration.

This list is used to verify the model of tapes added by administrators.
```
[tape_model]
# comma-separated list of tape models, without any space
# default: LTO5,LTO6,LTO7,LTO8,T10KB,T10KC,T10KD
supported_list = LTO5,LTO6,LTO7,LTO8,T10KB,T10KC,T10KD
```
