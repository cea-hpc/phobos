% Database

Note: all primary key fields are highlighted as follows: __oid__.

# Object store metadata
This section describes the tables related to object management.

## Object table
The object table stores living object metadata, and is the first target of
DSS requests when doing basic object phobos commands, such as **PUT**, **GET**
and **DELETE**.

This table is composed of the following fields: __oid__, uuid, version
and user_md. The field uuid must be unique.

It only exists one living object per oid, and so only one living (uuid, version)
pair per oid. The other ones are stored in the deprecated object table.

| field             | description                           |
|-------------------|---------------------------------------|
| oid               | object identifier                     |
| uuid              | unique identifier to distinguish      |
|                   | different generations of the same oid |
| version           | unique identifier inside the same     |
|                   | uuid, from 1 to n                     |
| user_md           | blob of user metadata (JSON)          |

## Deprecated object table
The deprecated object table store deprecated object metadata, those which
were deleted. They can be retrieved, under restrictions, using an **UNDELETE**.

This table is composed of the following fields: oid, __uuid__, __version__,
user_md and deprec_time.

| field             | description                           |
|-------------------|---------------------------------------|
| deprec_time       | date of migration from object table   |
|                   | to deprecated_object table            |

## Extent table
The extent table stores extent metadata, for all phobos objects.

This table is composed of the following fields: oid, __uuid__, __version__,
state, lyt_info, extents.

| field             | description                           |
|-------------------|---------------------------------------|
| state             | extent state (pending, sync, orphan)  |
| lyt_info          | blob of layout parameters (JSON)      |
| extents           | blob of extent metadata (JSON)        |

_lyt_info_ examples:

- simple layout: '' (empty)
- raid1 layout: {"cnt":"2"} (two copies)

_extents_ examples:
[{"media":"G00042","addr":"XXXXXX","sz":"215841651"},
 {"media":"G00048","addr":"XXXXXX","sz":"215841638"}]

# Storage resource metadata
This section describes the tables related to storage resource management.

## Device table
The device table stores device metadata and is mainly used by the phobos
daemon. Some admin calls can directly interact with it such as **DRIVE ADD**
commands.

This table is composed of the following fields: __id__, family, model, host,
adm_status and path.

| field             | description                           |
|-------------------|---------------------------------------|
| id                | unique device identifier (ex: serial) |
| family            | device technology (dir, tape, ...)    |
| model             | device model (ULTRIUM-TD6)            |
| host              | machine which is bound to the device  |
| adm_status        | admin status (locked, unlocked)       |
| path              | device path                           |

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
