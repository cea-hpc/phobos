% Phobos documentation

This document presents basic information about Phobos installation and the first
steps of its utilization. More information can be found in the following links:
* [Installation/migration and issues](doc/setup_and_issues.md)
* [Admin commands](doc/admin_commands.md)
* [Object-related commands](doc/object_management.md)

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

### Database setup (on RHEL7/CentOS7)

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

## First steps
### Launching phobosd
To use phobos, a daemon called phobosd needs to be launched. It mainly manages
the drive/media allocation and sets up resources to read or write your data.

To launch/stop the daemon:
```
# systemctl start/stop phobosd
```

### Scanning libraries
Use the `phobos lib scan` command to scan a given library. For instance, the
following will give the contents of the /dev/changer library:
```
phobos lib scan /dev/changer
```

### Device and media management
#### Adding drives
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

#### Adding media
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

#### Locking resources
A device or media can be locked. In this case it cannot be used for
subsequent 'put' or 'get' operations:

```
phobos drive {lock|unlock} <drive_serial|drive_path>
phobos media {lock|unlock} <media_id>
```

#### Listing resources
Any device or media can be listed using the 'list' operation. For instance,
the following will list all the existing tape identifiers:
```
phobos tape list
```

### Object management
The rest of this document will describe object management using CLI calls. An
API is available and is more described [here](doc/object_management.md).

#### Writting objects
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

#### Reading objects
To retrieve the data of an object, use `phobos get`. Its arguments are the
identifier of the object to be retrieved, as well as a path of target file.

For example:
```
phobos get obj0123 /tmp/obj0123.back
```

#### Reading object attributes
To retrieve custom object metadata, use `phobos getmd`:
```
$ phobos getmd obj0123
cksum=md5:7c28aec5441644094064fcf651ab5e3e,user=foo
```

#### Deleting objects
To delete an object, use `phobos del[ete]`:
```
phobos del obj0123
```

WARNING: the object will not be completely removed from the phobos system ie.
its data will still exist and be accessible. Thus the object is considered as
deprecated and basic operations can no longer be executed on it if its
uuid/version is not given. A future feature will allow the complete removal of
an object.

This deletion can be reverted using `phobos undel[ete]`:
```
phobos undel obj0123
```

To revert an object deletion, the object ID needs not to be used by a living
object.

##### Listing objects
To list objects, use `phobos object list`:
```
$ phobos object list --output oid,user_md
| oid   | user_md         |
|-------|-----------------|
| obj01 | {}              |
| obj02 | {"user": "foo"} |
```

The `--output` option describes which field will be output. `all` can be used as
a special field to select them all.
