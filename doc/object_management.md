% Phobos object-related commands

# Using the API
You can access data in phobos using the phobos API:

* Header file is `phobos_store.h`.
* Library is `libphobos_store` (link using `-lphobos_store`).
* Dependency: glib-2 (link using `-lglib-2.0`)

Calls for writing and retrieving objects are `phobos_put()` and
`phobos_get()`.

# Using the CLI
The CLI wraps `phobos_put()` and `phobos_get()` calls.

## Writting objects
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

A newer version of an object can be pushed in a phobos system. In this case, the
old version is considered deprecated, and basic operations can no longer be
executed on it if its version number is not given.

```
echo "new data" >> myinput_file
phobos put --overwrite myinput_file foo-12345
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

## Reading objects
To retrieve the data of an object, use `phobos get`. Its arguments are the
identifier of the object to be retrieved, as well as a path of target file.

For example:
```
phobos get obj0123 /tmp/obj0123.back
```

An object can also be targeted using its uuid and/or its version number:
```
phobos get --uuid aabbccdd --version 2 obj0123 /tmp/obj0123.back
```

## Reading object attributes
To retrieve custom object metadata, use `phobos getmd`:
```
$ phobos getmd obj0123
cksum=md5:7c28aec5441644094064fcf651ab5e3e,user=foo
```

## Deleting objects
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

## Listing objects
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

## Listing extents
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

## Locating objects
Tapes can move from one drive to another, and be reachable from different
hosts. The locate command helps you find the best host to reach your object:
```
phobos locate obj0123
```

A `--best-host` option is also available for the get command, to retrieve the
object only if the request is executed on the best host:
```
phobos get --best-host obj0123 /tmp/obj0123.back
```
