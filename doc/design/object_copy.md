Object *copy*
====

## Feature description

The goal of the object *copy* is to copy objects on different families,
tags, etc. This feature is generic enough to implement security mirroring on
multiple sites, mass data migration or Hierarchical Storage Management.

## What is an object *copy*?

The *copy* of an object represents a copy of its data on a (set of) medium
(media). It is not a new version or a new generation of a given object.

## New set of commands: `phobos copy`

The following new commands will allow one to interact with *copies*. All those
commands can also work for deprecated versions of objects, but they will ask for
`uuid` and `version` instead of `oid`.

1) `phobos copy create "oid" "copy_ref"`

    The only way to create a *copy* of an object, and thus copying it to
    another set of media, or on a different storage tier, without updating a new
    version/generation.

    The command can accept any kind of option that put will accept i.e.
    `family`, `tag`, `layout`, etc. Of course, they can be represented by an
    `alias`. A default profile for new copies can be defined in the
    configuration file the same way default family is defined for `put`
    operations.

    The `copy_ref` argument is the *copy* ID, and will be used in commands that
    may directly target a *copy*.

2) `phobos copy list "oid"`

    This command will allow one to list all *copies* of an object, and their
    information.

3) `phobos copy delete "oid" "copy_ref"`

    This command will allow one to delete the target *copy* of an object, thus
    invoking `delete --hard` command. This operation will not work if there is
    only one remaining *copy*.

## Modifications of the database

The database schema needs to be updated to consider *copies*. The `extent` table
does not need any modification: *copy* extents will be referenced as new extents
with new UUIDs. The `layout` table's primary keys are the object reference
(UUID and version) and a layout index. *Copies* cannot be inserted in the
`layout` table as is because they will have the same UUID and version as the
other *copies*. Extents with the same layout index of two *copies* will create
conflicts in the primary keys.

It is not possible to add the list of *copies* names as an `object` or
`deprecated_object` field because some fields of an object will be attributes
of a *copy*, like the `layout_info` or `access_date` fields. In this case we
propose to create a new table which binds *copies* to their objects.

### Change of `layout` primary key

For now, a layout is referenced using three pieces of information: the UUID
and the version of the object, and the index of the extent in the layout. The
*copy* name needs to be added.

### New table: `copy`

A new table is designed to store all *copies* of objects, including some fields
that are removed from the original `object`/`deprecated_object` table:

uuid (PK) | version (PK) | copy_name (PK) | layout_info | access_date | status
----------|--------------|----------------|-------------|-------------|-------
0000-abcd |            1 | source         | {'type'.. } | 4-nov 17:03 | sync
0000-abcd |            1 | cache          | {'type'.. } | 5-nov 10:54 | sync
0000-abcd |            1 | archive        | {'type'.. } | 4-nov 20:18 | sync

This will avoid duplication of information in the main `object` table.

## Impact on other behaviors

This section will describe the impacts of having a new *copy* feature on Phobos
mechanisms.

### Phobos data structures

For now, some informations are linked to a Phobos object, such as
`layout_info`, but this behavior will not be correct with the addition of the
*copy* feature. To that extent, it will be mandatory to create a new data
structure, which is contained in an object, and can be updated/retrieved
to/from the database.

### Main *copy*

As the *copy* feature will define secondary *copies*, we need to consider
the base object as a *copy*. A default name for this original *copy* can be
given in the configuration file.

```ini
[copy]
default_copy_name=source
```

This source *copy* name can be indicated with the `phobos put` command using
a specific option:

```bash
phobos put [--copy-name copy] /path/to/file oid
```

### Overwriting objects with *copies*

If an object is overwritten, *copies* of the deprecated version will be
preserved, but the new version will initially only have a single instance.

Once the garbage collection mechanism is implemented, it will be possible to
offer another way to deal with overwritten object *copies*, removing them
asynchronously. This may be detailed in a future design document.

### *Copy* references

*Copy* references may be defined in the configuration file, to help target a
defined hardware solution when creating them, using aliases:

```ini
[alias "fast"]
family=dir
library=raid1
lyt_params=repl_count=1

[copy "cache"]
alias=fast
```

In this example, the *copy* named `cache` will be automatically stored on
directories, using the `fast` alias. This *copy* can be created using the
following command:

```bash
phobos copy oid cache
```

### Getting the best *copies*

In case different *copies* are available for an object, and we want to retrieve
or locate its data, we may automatically target the best *copy*. To be able to
do this, we may add to the configuration file, in a given order, some *copies*
reference we will target:

```ini
[copy]
get_preferred_order=fast,cache
```

If no best references are found, the default *copy* is selected. If not found,
the first result of the database retrieval is selected.

In some cases, one will want to directly target a specific *copy* for its get or
locate commands:

```bash
phobos get [--copy-name copy] oid /path/to/file
phobos locate [--copy-name copy] oid
```

### Getting a copy vs getting THE copy

If some *copies* are not reachable, because the medium failed or is not
available on the current host, we may want to try other *copies*. If a given
*copy* is requested by the user, we suppose they absolutely want this copy and
will not attempt other ones.

The decision algorithm for this behavior is described in the following diagram:

```
                                +------------+
                                | phobos get |
                                +------------+
                                      |
                                      v
                             +-----------------+
                             | an error occurs |
                             +-----------------+
                                      |
                                      v
                       /-----------------------------\
                       | a given copy was requested? |
                       \-----------------------------/
                           ?                      ?
                        +=====+                +====+
                        | YES |                | NO |
                        +=====+                +====+
                           |                      |
                           v                      v
                      +---------+       +------------------+
                      | failure |       | try other copies |
                      +---------+       +------------------+
```

### Preventing not desired *copy* names

In case one does not want users to choose any *copy* name, this can be
prevented thanks to the configuration file:

```ini
[copy]
forbid_undefined_names=true
```

The defined *copy* names are those set in the configuration file with the
`default_copy_name` and the `get_preferred_order` keys and those described in
`copy` sections.

### Listing information

Extent listing needs to be modified according to the *copy* feature addition.
It can specifically target a *copy* in addition to its other identifier,
for now the object ID:

```bash
phobos extent list [oid [copy]]
```
