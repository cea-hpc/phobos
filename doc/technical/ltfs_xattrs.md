# LTFS-specific extended attributes

This document summarizes the specific LTFS extended attributes that may be
interesting to use in Phobos.
In other words: it does not list all existing LTFS attributes, only the
relevant ones.

Source:
https://www.ibm.com/docs/en/spectrum-archive-sde/2.4.4?topic=attributes-virtual-extended

## Common information about LTFS xattrs

LTFS-specific xattrs are available with the `user.ltfs` xattr prefix (in the
user xattr namespace), e.g.  "user.ltfs.volumeBlocksize".

There are several levels of extended attributes:
* Extended attributes of **files** contain file-specific information. They are
available by querying extended attributes of a file stored in LTFS;
* Extended attributes of **volumes** contain filesystem-specific information.
They are available by querying extended attributes of the root directory of the
LTFS filesystem;
* Extended attributes of **media** contain tape-related information.
They are available by querying extended attributes of the root directory of the
LTFS filesystem.

Some attributes are read-only, some are write-only, and some are read-write.
They are respectively noted (R), (W), or (RW) in the rest of this document.


### Note: accessing LTFS xattrs from command line

It can be useful to query LTFS xattrs from command line to get an overview of
their contents. Two common commands can be used: `attr` or `getfattr`.

`attr` automatically appends the `user.` prefix to the xattr name, whereas
`getfattr` does not.

```sh
$ attr -g ltfs.startblock FOO
Attribute "ltfs.startblock" had a 1 byte value for FOO:
7

$ getfattr -n user.ltfs.startblock FOO
# file: FOO
user.ltfs.startblock="7"
```

## File attributes

* `ltfs.startblock` (R): block address where the first extent of the file is
  stored.
    * Interest: this may be used to optimize the order of reading multiple files
      on a tape.
    * Example value: `20490`

* `ltfs.fileUID` (R): unique file identified within the LTFS volume.
    * Interests:
        * it may be used as an argument for specific LTFS features;
        * it can be used to uniquely identify a file in a LTFS filesystem.

## Volume attributes

* `ltfs.commitMessage` (RW): allow attaching a message to the next sync of the
  filesystem index. If it is not modified, the message will still be used for
  the next syncs.
    * Interest: allow tagging each sync with a specific information (timestamp,
    version number, generation number, list of modified objects...). Some
    information are already recorded by the system at each sync (see the next
    item).

* `ltfs.index{Generation,Location,Previous,Time}` (R): information about the
  last synchronized LTFS index.
    * Interest: gives information about the last change of the filesystem.

* `ltfs.volumeBlocksize` (R): block size to read/write on the volume. This is
preferably the block size to be used for I/O operations, to avoid in-memory block
splitting and aggregation.
  Note this value should be returned by statfs().f_bsize.
    * Interest: determine optimal IO size if the value reported by statfs() is
    not relevant.

* `ltfs.volumeLockState` (RW): this can take 3 possible values: `unlocked`,
  `locked`, or `permlocked`.
    * Interest: the documentation indicates this could be used to manage the
    case of a crash during an index flush. See the reference document for more
    detailed information.

## Media attributes

* `ltfs.mediaEfficiency` (R): this is a tape health indicator (overall measure
  of the condition of the loaded medium). Its value can vary between 01
  (best condition) and FF (worst condition). 0 means the health status is
  "unknown".
    * Interest: Phobos could keep track of media health and take it into account
    when applying repack policies. It could also warn the administrator when it
    detects a medium with a bad condition, or even decide not to use a medium
    if its health indicator is above a given threshold.


LTFS also exposes a bunch of statistics about media accesses: number of files
read/written, total mega-bytes read/written, number of times the media was
loaded, rewinded, error counters...

It would be very long and probably useless to list them and describe them all
here.

Just keep in mind they exist and check the reference document if you need them.
