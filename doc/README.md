# Phobos documentation

## Installation
### Requirements for tape access
If you need phobos to manage tapes, you need to install the `lin_tape` driver,
as well as the open-source LTFS implementation `ltfssde` (both can be found on
IBM Fix Central: [lin_tape](https://www-945.ibm.com/support/fixcentral/swg/selectFixes?parent=Tape%20drivers%20and%20software&product=ibm/Storage_Tape/Tape+device+drivers&release=1.0&platform=Linux+64-bit,x86_64&function=all]),
[ltfssde](https://www-945.ibm.com/support/fixcentral/swg/selectFixes?parent=Tape%20drivers%20and%20software&product=ibm/Storage_Tape/Long+Term+File+System+LTFS&release=2.2&platform=Linux&function=all)).

```
# Installing lin_tape driver
yum install lin_tape
modprobe lin_tape
# Your tape drives should now be seen as IBMtape devices:
ls -l /dev/IBMtape*
# Installing LTFS
yum install ltfssde
```

### Phobos installation
Install phobos and its requirements:
```
yum install phobos
```

### Database setup
After installing PostgreSQL server (version 9.4 or later), edit
`/var/lib/pgsql/9.4/data/pg_hba.conf` to authorize access from phobos host
(localhost in this example):

```
# "local" is for Unix domain socket connections only
local   all             all                                     trust
# IPv4 local connections:
host    all             all             127.0.0.1/32            md5
# IPv6 local connections:
host    all             all             ::1/128                 md5
```

Then initialize phobos database:
```
/usr/pgsql-9.4/bin/postgresql94-setup initdb
systemctl start postgresql-9.4.service
```

Finally,  create phobos database and tables (the password for SQL phobos user
will be prompted for unless provided with -p):
```
/usr/sbin/phobos_db setup_db -s
```

## Upgrade of an existing instance

After upgrading phobos, run the DB conversion script (credentials to connect to
the database will be retrieved from /etc/phobos.conf):
```
/usr/sbin/phobos_db migrate
# 'y' to confirm
```

## First steps
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
phobos tape add -t lto6 [073200-073222]L6
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
# phobos put <file>       <id>      -m  <k=v,k=v,...>
phobos put  myinput_file  foo-12345 -m  user=$LOGNAME,\
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

## Device and media management
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
phobos tape add -T class1,project2 -t LTO8 [073000-073099]L8

# Update tags for these tapes
phobos tape update -T class2 [073000-073099]L8

# Clears tags
phobos dir update -T '' [073000-073099]L8
```

When pushing objects to phobos, you can then restrict the subset of resources
that can be used to store data by specifying tags with the `-T` option.

```
# The following command will only use media with tags 'class1' AND 'projX'
phobos put -T class1,projX /path/to/obj objid
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
[lrs]
# indicate 'tape' or 'dir' family
default_family = tape
```

Alternativelly, you can override this value in the environment (like any other
configuration parameter) by setting the `PHOBOS_LRS_default_family` variable.

Example:
```
# put data to directory storage
PHOBOS_LRS_default_family=dir phobos put file.in obj123
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
