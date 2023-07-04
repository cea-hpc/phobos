% Database

Note: all primary key fields are highlighted as follows: __oid__.

# Object store metadata
This section describes the tables related to object management.

## Object table
The object table stores living object metadata, and is the first target of
DSS requests when doing basic object phobos commands, such as **PUT**, **GET**
and **DELETE**.

This table is composed of the following fields: __oid__, object_uuid, version,
user_md and lyt_info. The field uuid must be unique.

There is only one living object per oid, and so only one living (object_uuid,
version) pair per oid. The other ones are stored in the deprecated object table.

| field             | description                           |
|-------------------|---------------------------------------|
| oid               | object identifier                     |
| object_uuid       | unique identifier to distinguish      |
|                   | different generations of the same oid |
| version           | unique identifier inside the same     |
|                   | uuid, from 1 to n                     |
| user_md           | blob of user metadata (JSON)          |
| lyt_info          | blob of layout parameters (JSON)      |

_lyt_info_ examples:

- raid1 layout: {"name": "raid1", "attrs": {"repl_count": "2"}, "major
": 0, "minor": 2}

## Deprecated object table
The deprecated object table store deprecated object metadata, those which
were deleted. They can be retrieved, under restrictions, using an **UNDELETE**.

This table is composed of the following fields: oid, __object_uuid__,
__version__, user_md, lyt_info and deprec_time.

| field             | description                           |
|-------------------|---------------------------------------|
| deprec_time       | date of migration from object table   |
|                   | to deprecated_object table            |

## Extent table
The extent table stores extent metadata.

| field             | description                           |
|-------------------|---------------------------------------|
| extent_uuid       | unique identifier                     |
|                   | (could be the checksum if we don't    |
|                   | allow two different checksum          |
|                   | mecanisms, or no checksum at all)     |
| state             | state (pending, sync, orphan)         |
| size              | size in byte                          |
| medium_family     | family of the medium on which this    |
|                   | extent is stored                      |
| medium_id         | id of the medium on which the extent  |
|                   | is stored                             |
| address           | address of this extent on the medium  |
| hash              | blob of hash checksums (JSON,         |
|                   | currently can manage MD5 and XXH128)  |
| info              | blob of miscellaneous info per extent |
|                   | (JSON, currently not used, only for   |
|                   |  future extension)                    |

## Layout table
This layout table stores the link between phobos objects and their extents.

This table is composed of the following fields: __object_uuid__,
__version__, __extent_uuid__, __layout_index__.

| field             | description                           |
|-------------------|---------------------------------------|
| layout_index      | index of this extent in the layout    |

# Storage resource metadata
This section describes the tables related to storage resource management.

## Device table
The device table stores device metadata and is mainly used by the phobos
daemon. Some admin calls can directly interact with it such as **DRIVE ADD**
commands.

This table is composed of the following fields: __id__, family, model, host,
adm_status and path.

| field             | description                             |
|-------------------|-----------------------------------------|
| id                | unique device identifier (ex: serial)   |
| family            | device technology (dir, tape, ...)      |
| model             | device model (ULTRIUM-TD6)              |
| host              | machine which is bound to the device    |
| adm_status        | admin status (locked, unlocked, failed) |
| path              | device path                             |

## Media table
The media table stores medium metadata and like the device table, is mainly
used by the phobos daemon. Some admin calls can directly interact with it
such as **MEDIA ADD** commands.

This table is composed of the following fields: __id__, family, model,
adm_status, fs_type, fs_label, fs_status, address_type, stats, tags, put, get
and delete.

| field             | description                           |
|-------------------|---------------------------------------|
| fs_type           | filesystem type                       |
| fs_label          | filesystem name                       |
| fs_status         | filesystem status                     |
|                   | (blank, empty, used, full)            |
| address_type      | medium selected address type          |
|                   | (path, hash1, opaque)                 |
| stats             | medium stats (number of objects, of   |
|                   | errors, used space, ...)              |
| tags              | medium tags                           |
| put               | put access (true if authorized)       |
| get               | get access (true if authorized)       |
| delete            | delete access (true if authorized)    |

# Database management
This section describes the tables related to database management.

## Schema info table
The schema info table is composed of a version field, which describes the
current version of the database schema.

## Lock table
The lock table is used to aggregate all locks of the phobos system. Those locks
can target database resources, such as devices or objects, but can also be used
for other means.

This table is composed of the following fields: __id__, __type__, hostname,
owner and timestamp.

Ids are currently limited to 2048 characters, owners to 32 characters, and
hostnames to 64 characters.

| field             | description                                     |
|-------------------|-------------------------------------------------|
| id                | lock identifier                                 |
| type              | type of the lock                                |
| hostname          | hostname of the lock owner                      |
| owner             | name of the lock owner on the host (e.g. a pid) |
| timestamp         | date when the lock is taken                     |
