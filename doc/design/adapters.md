# Adapters purpose

Phobos core addresses objects using the couple (uuid, version).

According to the chosen layout, objects can be stored as multiple parts called
extents. To the unique key addressing an object, Phobos adds an extent tag to
identify an extent of an object. For instance, this extent tag could contain
the name and parameters of the layout and the number of this extent: r1-2_0 for
the extent number 0 of a raid 1 layout with 2 copies. An extent is uniquely
identified by the triplet (uuid, version, extent_tag).

For each extent, phobos builds the extent_key string: version.extent_tag.uuid .
This extent_key is unique and allows the storage backend to generate a unique
'address' to store the extent.

An adapter is a software layer to access a given type of storage backend
(POSIX filesystem, tar file, object store, ...). It abstracts the way
entries are addressed, organized, and accessed in this backend.

## Requirements

### General requirements

* On a put, in addition of the extent_key, Phobos provides to an adapter a
  human-readable extent_description string: the object id.

* On a put, the adapter turns the extent_key and the extent_description to an
  internal representation called 'address' (e.g. a POSIX path or an opaque
  object key). As "result" of the put, the adapter gives back to phobos the
  generated 'address' and phobos stores this 'address' to be able to get back
  the extent from the adapter. The adapter could rely on the uniqueness of the
  extent_key to generate a unique 'address'.

* An adapter must be able to locate an extent by its 'address'.

* On a get or a delete, phobos gets back the extent from the adapter by
  providing the corresponding registered 'address'.

* For sysadmin convenience, the adapter should keep the human-readable
  extent_description in the object address, if possible. It can however complete
  it with other internal representations (eg. a hash). These addressing
  components can however be modified for technical reasons or performance
  considerations.  For instance, the extent_description can be truncated, '/'
  characters can be changed to another delimiter, non_printable character can be
  replaced, etc...

* Adapters should provide a way to attach an arbitrary set of metadata to
  extents (e.g. as extended attributes, or a metadata_blob). This information is
  critical to rebuild phobos database in case of accidental loss.

* Adapters should provide an object-based interface: put/get/del

* If possible, it must provide a feature to manage media transactions,
  that could be performed after writing an extent or after writing a set of
  files.

* Adapater interface to other layers must be available as a vector of functions.

### POSIX requirements

* To ensure maximum compatibility with all components of current and future
  software stacks (OS, filesystems, drivers, firmware, tools, ...), paths in a
  storage backend should not exceed 255 meaningful characters[1].

* The number of extents in a directory should be limited to a "reasonnable"
  value, to avoid:
  * reaching limitations on directory size
  * performance impact on object lookup and access
  * locking contentions on namespace access
  * too long readdir() operations

* However, the number of levels of sub-directories to traverse to access an
  extent should be maintained as low as possible, in particular when the number
  of stored object is low.

## Example

Example implementation in a POSIX namespace:
* storing 2 objects ("foo1/id1", p1) and ("foo2_id2", s3):
  * 5BC7/foo-id1.p1
  * AB6C/foo_id2.s3
* storing 10000 objects:
  * 5B/C7/foo-id1.p1
  * AB/6C/foo_id2.s3
  * 2E/5F/foo_id3.p3
  * 8A/23/foo_id4.r2
  ...

## Design and implementation



## References
[1] http://msdn.microsoft.com/en-us/library/windows/desktop/aa365247%28v=vs.85%29.aspx
