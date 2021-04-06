# Deletion and Versioning

This document describes the deletion/versioning feature, as a user may want to
discard or undelete old object versions. Phobos differentiates between
**DELETE** and **REMOVE**. When a user **DELETE**s an object or **PUT**s a new
version of it, the old version of the object is still accessible and is kept on
media until a future garbage collector or the user (through a future **REMOVE**
call) remove it. This document does not deal with extent cleaning on media nor
the definitive removal of objects. This will be described in a future
*removal_and_cleaning* design document.

A user can **GET** and **GETMD** any version of an object as long as this
version is deprecated but still accessible.

As long as a deleted object can be gotten and all these extents remain on
media, it may still be taken into account for quota calculation depending on the
implementation of the quota system.

---

## Use cases
> Alice, Bob and Charlie have access to a storage system using Phobos.

1. Spring cleaning

> Alice observes the list of objects she has pushed to the storage system and
> decides some of them are not necessary anymore. She wants to delete them.

2. Quota limitation

> Bob checks his quota and sees that it does not have enough space to push more
> objects. He wants to remove some objects to push new ones.

3. Versioning

> Charlie wants to keep the previous version(s) of an object when he updates it.
> This will allow him to pull that(those) version(s), and make some comparisons.

4. ID re-utilization

> Alice wants to push a new object in the storage system but sees that the
> required ID is already used for a distinct object which is no longer
> necessary. She needs to delete this previous object to push the new one with
> the same ID.

5. Undelete

> Bob deleted objects and now wants to undelete them.

---

## Data structures
This feature needs to add information in object metadata, and in the database.

### Version number
An object describes a state of a set of data and metadata. As a new version of
an object is pushed to the storage system, its state is modified. Each state is
identified by a number called __version__ number. This number is a strictly
positive integer which is incremented each time a new version of the object is
pushed. The highest this number is, the most recent the version is. The value
_0_ is not allowed and reserved for querying the latest existing __version__
with GET and GETMD calls.

#### Object creation
A new object is pushed with a __version__ number set to _1_. Beginning with 1
guarantees that __version__ number 0 is an unused one. The value 0 could be used
as an uninitialized __version__ number value.

#### Object update
A new object version of a pre-existing object is pushed with an incremented
__version__ number, going from _i_ to _i+1_.

### Generation and UUID
A set of versions of the same object is called a __generation__. When an object
is deleted, we pin the targeted generation as deprecated, and then its id can be
reused for another generation. To distinguish generations of objects which
shared the same ID, where at most one of them is alive, we use a __UUID__. The
__uuid__ field of an alive object is not known by the user.

#### Object creation
A __uuid__ is automatically generated when a new object is pushed.

#### Object deletion
The __uuid__ of the deleted object is given to the user, to allow them to
undelete it, as this is the only way to distinguish one object to another (if
they share the same ID). If this information is not known, or lost, by the user
when trying to undelete the object, timestamps can be compared using the object
list call.

### Difference between version and generation
Different generations of the same object ID are distinguished with a UUID.
Theses different UUIDs of the same object ID are not ordered.

Different versions of the same generation of an object are distinguished by
a version number. Version numbers are incrementally and continuously ordered.

---

## Feature implementation
The following section describes some implementation hints.

### On medium modification
Extent metadata must include the UUID and the version number of the object.
This will avoid conflicts in case of a medium recovery, where extents for two
different objects with the same ID are written on.

The ID of an extent on a medium must stay unique. As example, the LTFS path to
each extent must stay unique. It will depend on UUID and version. We must ensure
the compatibility of previous extent ID with no version and UUID.

### Database modification
This feature implies some database modifications.

#### ``object`` table
Each record in the database must hold a version number, to indicate the user
which one is the current version. We add to the database one integer
field: __version__. For a new object id which was not pre-existing in the
``object`` table, the new object entry into the object table will have a version
number of _1_. For a new version of an already existing object, the pre-existing
version of the object is moved to the ``deprecated_object`` table (see below)
and the new version is added to the ``object`` table with a version number
incremented by one.

Also, and to distinguish extents from different objects with the same ID, we add
the string field: __uuid__.

The primary key of the ``object`` table remains as __oid__ (for Object ID)
because this table only stores live objects state, which can not share the same
ID. There must be only one entry into the ``object`` table per __oid__. Other
object entry with the same __oid__ must be into the ``deprecated_object`` table
with different __uuid__ or with the same __uuid__ but with different and
inferior __version__. At any time, for a same (__oid__, __uuid__) pair, the
__version__ that stays into the ``object`` table must be the greatest one, other
and lower __version__ must stay into the ``deprecated_object`` table. Given a
__uuid__ is also unique accross __oid__s, this is also true for any __uuid__.

#### ``deprecated_object`` table
A new table needs to be created to gather the deprecated versions of an object:

- old versions after an update,
- old generations after a deletion.

This table, named ``deprecated_object`` gets the same fields as ``object`` table
with the addition of a timestamp field __deprec_time__ which indicates when the
update or deletion happened.

The __deprec_time__ field will be set to the current timestamp, when the object
becomes deprecated. This will make it possible to implement garbage collection
policies based on this timestamp. For instance a timestamp is outdated if the
guaranteed soft undelete time is dued.

The primary key of the ``deprecated_object`` table will be a combination of
(__uuid__, __version__).

#### Greatest version last to be removed
For each __uuid__, the greatest existing __version__ from ``object`` and
``deprecated_object`` tables can only be removed last, because this entry is the
only piece of information which tracks the current greatest already used
__version__ number for this __uuid__. The future *removal_and_cleaning* process
must deal with this constraint.

#### ``extent`` table
Each record in the database must hold a UUID and a version number, to
match against the ``object`` and ``deprecated_object`` tables. We add to the
database one string and one integer fields: __uuid__ and __version__.

The primary key of the ``extent`` table will then be the (__uuid__, __version__)
pair.

### API calls
This feature implies some modifications on the Phobos API.

#### Object deletion
The `phobos_delete()` call moves the current generation
of an object from the ``object`` to the ``deprecated_object`` table.

```c
int phobos_delete(struct pho_xfer_desc *xfers, int num_xfers);
```

As input, only the ``xd_oid`` field is taken into account for each ``xfer``.

The deletion mechanism moves objects from the ``object`` table to the
``deprecated_object`` table. This database transaction will need to be atomic to
avoid having the same version of an object in both tables in case a crash
happens.

As long as their extents remain on media, any deleted generation or version can
stay into the ``deprecated_object`` table. Extent cleaning could be
policy-driven and will be treated in the related design document:
"removal_and_cleaning". A future "REMOVE" action could be added to phobos API
and cli to definitely remove object from ``object`` and ``deprecated_object``
table from a user action in addition of automatic policy-driven action.

#### Object undelete
The `phobos_undelete()` call retrieves deprecated objects.

```c
int phobos_undelete(struct pho_xfer_desc *xfers, int num_xfers);
```

As input, xd_uuid or the oid field are only taken into account for each xfer and
the last corresponding version is selected to be undeleted from the
``deprecated_table``.

The undelete mechanism moves objects from the ``deprecated_object`` table to
the ``object`` table. This database transaction will need to be atomic to avoid
having the same version of an object in both tables in case a crash happens.

In case the undelete results in an ID conflict, which will happen if a new
object was pushed using the same ID, the call will fail for this object.

If there is several version of the same uuid into the ``deprecated_object``
table, the greatest one is undeleted.

#### Data structure

##### The xfer data structure
The xfer data structure is modified to incorporate the UUID and version number.
Those fields will serve as input for GET and GETMD calls, to target a
given object version, and as output for PUT calls, to return the new version
number.

```c
struct pho_xfer_desc {
    char                  *xd_objid;
    char                  *xd_uuid;      /* UUID */
    unsigned int           xd_ver_num;   /* Version number */
    enum pho_xfer_op       xd_op;
    union pho_xfer_params  xd_params;
    ...
};
```

If `xd_uuid` is set to `NULL` and `xd_ver_num` is set to _0_ for GET and GETMD
calls, it means that we want to target the latest alive version.

##### The object_info structure
The object_info structure is modified to incorporate the UUID and version
number. Those fields will serve as output of the phobos_store_object_list
function.

```c
struct object_info {
    char         *oid;
    char         *xd_uuid;     /* UUID */
    unsigned int  xd_ver_num;  /* Version number */
    char         *user_md;
};
```

### CLI calls
The CLI needs to be extended with a new keyword/command and new options for some
existing commands to manage generations and versions.

#### Object deletion
The `phobos delete` targets one (or more) object ID(s) to delete.

```
$ phobos delete obj_id [obj_id ...]
$ phobos del obj_id [obj_id ...]
```

#### Object undelete
The `phobos undelete` targets deprecated objects referenced by OIDs or UUIDs.

```
$ phobos undelete uuid uuid [uuid ...]
$ phobos undel uuid uuid [uuid ...]
$ phobos undelete oid obj_id [obj_id ...]
$ phobos undel oid obj_id [obj_id ...]
```

If a UUID is provided, and this UUID does not exist in the
``deprecated_object`` table the operation fails, else the targeted OID is the
one corresponding to entry with this UUID into the ``deprecated_object`` table.

If an OID is provided and there is no entry with this OID into the
``deprecated_table`` table the operation will fail. Else if there is several
UUIDs into the ``deprecated_table`` corresponding to this OID the operation will
fail. Else, there is only one UUID corresponding to this OID, it becomes the
targeted UUID.

If an object with the same targeted OID already exists in the ``object``
database, the operation fails.

The undeleted version is always the latest version of the targeted UUID.

#### Put
The `phobos put` command prints the version number of the pushed object. Its
default behavior does not change: if an object is pushed with an already
existing ID, the command fails.

The option `--overwrite` pushes a new version of an existing object, or creates
it as a new object if the ID does not exist.

```
$ phobos put [--overwrite] SRC obj_id
```

#### Get
The `phobos get` may need to target an object version to access it. Its default
behavior does not change.

```
$ phobos get [--uuid uuid] [--version ver] obj_id DEST
```

If `uuid` is not given, the call targets the current generation.

If `ver` is not given, the call targets the latest version.

#### GetMD
The `phobos getmd` command prints the current version number of an object.

#### Object list
The `phobos object list` command optionnaly prints deprecated versions of an
object.

```
$ phobos object list --deprecated [obj_id]
```

The call lists entries from the table ``deprecated_object``.

#### Extent list
The `phobos extent list` command prints extents of old reachable versions of an
object.

```
$ phobos extent list --deprecated [--uuid obj_uuid] [obj_id]
```

The call lists entries associated to objects from the table
``deprecated_object``. One can add obj_uuid and obj_id filters.

---

## Responses to use cases
The following section describes CLI calls utilization following the use cases
presented in the first section.

1. Spring cleaning

> Alice wants to delete two of her three objects.

```sh
$ phobos object list
obj_foo
obj_bar
obj_baz
$ phobos delete obj_foo obj_baz
Objects successfully deleted: 2
$ phobos object list
obj_bar
```

2. Quota limitation

> Bob wants to free some space and search for a unused and huge object
> to delete.

```sh
$ phobos object list --output ID,user_md
+---------+----------------------+
| ID      | user_md              |
+---------+----------------------+
| obj_foo | size=large,age=young |
| obj_bar | size=small           |
| obj_baz | size=huge,age=old    |
+---------+----------------------+
$ phobos delete obj_baz
Objects successfully deleted: 1
$ phobos object list --output ID,user_md
+---------+----------------------+
| ID      | user_md              |
+---------+----------------------+
| obj_foo | size=large,age=young |
| obj_bar | size=small           |
+---------+----------------------+
```

The deleted object stays into the ``deprecated_object`` table. It may continue
to be taken into account into the quota of Bob depending of the implementation
of the quota system.

The policy driven garbage automatic collection will remove it to definitely free
some quota for Bob or Bob could use the future user driven REMOVE call on a
deleted object.

3. Versioning

> Charlie wants to push an updated version of an object. He then can retrieve
> its old object version. Trying to put without the `--update` option will fail.

```sh
$ phobos object list --output ID,version
+---------+---------+
| ID      | version |
+---------+---------+
| obj_foo |       5 |
+---------+---------+
$ phobos put SOURCE obj_foo
ERROR: obj_foo already exists
$ phobos put --update SOURCE obj_foo
Object 'obj_foo@6' successfully pushed
$ phobos object list --output ID,version
+---------+---------+
| ID      | version |
+---------+---------+
| obj_foo |       6 |
+---------+---------+
$ phobos object list --deprecated --output ID,version
+---------+---------+
| ID      | version |
+---------+---------+
| obj_foo |      5  |
+---------+---------+
$ phobos get --version 5 obj_foo DEST
Object 'obj_foo@5' successfully retrieved
```

4. ID re-utilization

> Alice wants to re-use an old ID for a new object.

```sh
$ phobos object list --output ID,version
+---------+---------+
| ID      | version |
+---------+---------+
| obj_foo |       5 |
+---------+---------+
$ phobos delete obj_foo
Objects successfully deleted: 1
$ phobos object list
$ phobos put SOURCE obj_foo
Object 'obj_foo@1' successfully pushed
```

5. Undelete

> Bob wants to undelete an object he previously deleted.

```sh
$ phobos object list --output ID,version
$ phobos object list --deprecated --output ID,uuid,version
+---------+--------------------------------------+---------+
| ID      | uuid                                 | version |
+---------+--------------------------------------+---------+
| obj_bar | 99887766-5544-3322-1100-aabbccddeeff |      3  |
| obj_bar | 99887766-5544-3322-1100-aabbccddeeff |      2  |
| obj_foo | 00112233-4455-6677-8899-aabbccddeeff |      5  |
+---------+--------------------------------------+---------+
$ phobos undelete uuid "00112233-4455-6677-8899-aabbccddeeff"
Object 'obj_foo@5' successfully undeleted.
$ phobos object list --deprecated --output ID,uuid,version
+---------+--------------------------------------+---------+
| ID      | uuid                                 | version |
+---------+--------------------------------------+---------+
| obj_bar | 99887766-5544-3322-1100-aabbccddeeff |      3  |
| obj_bar | 99887766-5544-3322-1100-aabbccddeeff |      2  |
+---------+--------------------------------------+---------+
$ phobos object list --output ID,version
+---------+---------+
| ID      | version |
+---------+---------+
| obj_foo |       5 |
+---------+---------+
$ phobos undelete oid "obj_bar"
Object 'obj_bar@3' successfully undeleted.
$ phobos object list --deprecated --output ID,uuid,version
+---------+--------------------------------------+---------+
| ID      | uuid                                 | version |
+---------+--------------------------------------+---------+
| obj_bar | 99887766-5544-3322-1100-aabbccddeeff |      2  |
+---------+--------------------------------------+---------+
$ phobos object list --output ID,version
+---------+---------+
| ID      | version |
+---------+---------+
| obj_bar |       3 |
| obj_foo |       5 |
+---------+---------+
```
