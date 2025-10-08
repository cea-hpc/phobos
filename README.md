# Phobos documentation

## Introduction

Phobos, standing for Parallel Heterogeneous OBject Store, is a tool designed to
manage large volumes of data for various kinds of storage technologies from SSD
to tapes. Phobos can efficiently handle very large datasets on inexpensive media
without sacrificing scalability, performance, or fault-tolerance requirements.

Phobos is designed to allow the easy integration of new modules for layouts such
as mirroring and erasure coding or, in a near future, I/O adapters.

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
(localhost in this example, change it if your network is a different one):
```
# "local" is for Unix domain socket connections only
local   all             all                                     trust
# IPv4 local connections:
host    all             all             127.0.0.1/32            md5
# IPv6 local connections:
host    all             all             ::1/128                 md5
```

If your phobos host is on an other node than your postgresql server, you must
change the `listen_adresses` option of the `/var/lib/pgsql/data/postgresql.conf`
from `localhost` to the good one matching your needs.

Start the database server:
```
systemctl start postgresql-9.4.service
```

Finally, create phobos database and tables as postgres user (the password for
SQL phobos user will be prompted for unless provided with -p):
```
sudo -u postgres phobos_db setup_db -s
```

### Database setup (On RHEL8/CentOS8)
Install postgresql version >=9.4. For this distribution, the name of the
PostgreSQL packages are different, you must install the following:
```
yum install postgresql-server postgresql-contrib
```

Then, you must initialize the postgresql directories:
```
postgresql-setup --initdb --unit postgresql
```

Move the created configuration file to the PostgreSQL directory in `/var/lib`:
```
mv /tmp/pg_hba.conf /var/lib/pgsql/data/
```

Modify the library configuration of your system to include PostgreSQL:
```
echo "/usr/lib64" > /etc/ld.so.conf.d/postgresql-pgdg-libs.conf
echo "LD_LIBRARY_PATH=/usr/lib64:$LD_LIBRARY_PATH" > /etc/profile
ldconfig -v
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

## First steps

To use Phobos, you must start a daemon called `phobosd`, which mainly manages
the drive/media allocation and sets up resources to read or write your data.
One must be started on each node that may access drives/media.

Moreover, if you want to use tapes in your system, you must start another daemon
called `phobos_tlc` (Tape Library Controller) before the `phobosd`. You must
start one `phobos_tlc` per tape library. Each `phobos_tlc` daemon must be
started on a node that has an access to manage the corresponding tape library.
`phobos_tlc` needs to be launched before any `phobosd` that will use this tape
library.

### Launching phobos_tlc

On the node that has access to the tape library management device, you must
set the `lib_device` option of the corresponding `tlc` configuration section
into /etc/phobos.conf . (The default of the installed template is `/dev/changer`
for the default `legacy` tape library.)

To launch/stop the daemon:
```
# systemctl start/stop phobos_tlc
```

### Launching phobosd

To launch/stop the daemon:
```
# systemctl start/stop phobosd
```

### Scanning libraries
Use the `phobos lib scan` command to scan a given library. For instance, the
following will give the contents of the default tape library:
```
phobos lib scan
```

When using the library (such as when running the above command), if phobos
returns an "invalid argument" error, and when you check the logs you see the
following:
```
<WARNING> SCSI ERROR: scsi_masked_status=0x1, adapter_status=0, driver_status=0x8
<WARNING>     req_sense_error=0x70, sense_key=0x5 (Illegal Request)
<WARNING>     asc=0x24, ascq=0 (Additional sense: Invalid field in cbd)
<ERROR> Sense key 0x5 (converted to -22): Invalid argument
<ERROR> scsi_element_status failed for type 'drives': Invalid argument
```
It may be a tape library limitation. For some type of tape libraries, for
instance IBM libraries, one can't query a drive's serial number and it's volume
label in the same request. To prevent this, phobos can separate the query in two
by activating the parameter 'sep_sn_query' in the section 'lib_scsi', as shown
[here](doc/cfg/template.conf#L63), or as the environment variable
'PHOBOS_LIB_SCSI_sep_sn_query=true'.

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

## Additionnal features
### Autocomplete
Phobos has autocomplete enabled for the commands you can use and their options.
To take advantage of this, install the `python3-argcomplete` package, the
`argcomplete` Python module, and then source the following file:
```
source /etc/bash_completion.d/phobos-argcomplete.sh
```
