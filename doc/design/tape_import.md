# Tape import

Currently Phobos cannot import a previously written tape into its own system
without having to format it (this implies that the data it contains would be
lost).
The goal of the tape import feature is to resolve this issue.

## Context

Consider the following configuration:
 - Suppose two different Phobos systems,
 - One is mobile but has little storage capacity,
 - The other has a much bigger capacity but is fixed in space,
 - Both of them use tapes as a storage mean.
The goal is to be able to write data on the first system (in-situ
measures for instance) and to transfer the tapes into the second system (for
long-time storage for instance).

Another situation for which this feature would be useful is do to a complete
recovery of a Phobos system in case of a disaster. The Phobos system would then
be able to analyse each of its tapes to recreate from scratch its database,
thus implementing the disaster recovery feature as well.

## Information available from a Phobos file

When data is put to Phobos, an extent, represented by a file is created on the
used medium, and it is named in such a way that it is easy to make an entry
inside the DSS from it.
A file name is structured as follow:
`objectId.version.layoutInfo.objectUuid`
With:
 - objectID:    Identifier of the object the extent corresponds to,
 - version:     The version of the object the extent corresponds to,
 - layoutInfo:  Information on the layout used to encode the object,
 - objectUuid:  Uuid of the object in the database the extent corresponds to.

The layoutInfo is dependent on the layout used and the information required
to construct it. For instance, for RAID1 layout, the layoutInfo is divided into
`r1-replCount_extentIndex`, with:
 - r1:          Identifies the Raid1 layout,
 - replCount:   Number of copies of the extent,
 - extentIndex: Identifies the extent index (replCount*splitCount extents in
                total).

In addition, the file is affixed with the following extended attributes:
 - user.md5:                Md5 hash of the extent, if available,
 - user.xxh128:             Xxh128 hash of the extent, if available,
 - user.raid1.offset:       Offset regarding the initial object the extent
                            corresponds to,
 - user.raid1.object_size:  The size of the object,
 - user.raid1.last_split:   1 if the extent is one of the copies of the
                            last split of the object, 0 otherwise
 - user.user_md:            User-written metadata of the object.

## Object-related status

Objects and deprecated objects will have a new status stored in the database.
The three possible values for this status will be:
 - "incomplete":    The object has not enough copies of its splits to be
                    reconstructed,
 - "readable":      The object has not all of its copies, but the copies it has
                    are enough to reconstruct the object,
 - "complete":      The object has all of its copies available.

## How to recover an object

Using the file's name and the extended attributes, Phobos is able to fully
recover an extent's metadata and add a new entry to the DSS. However,
reconstructing an object requires assembling matching extents together, and
checking that at least one full copy of the object is then readable.
An object can be split in multiple parts, and if even one split is missing,
the object cannot be rebuilt.
An object can also be replicated several times depending on the layout used.
If an object has the same oid as another in the DSS:
- if they have the same UUID:
    - if its version number is strictly higher, then it is added to the DSS in
    the `object` table with the "incomplete" status and the other object is
    moved to the `deprecated objects` table.
    - if its version number is strictly lower, then it is added to the DSS in
    the `deprecated object` table with the "incomplete" status.
- else, then the new one is renamed with the ".import-{current-date}" suffix.\*

At the end of the import of all tapes, the `object` and `deprecated object`
tables can be browsed to update the status of the object or deprecated object.
If the object can be rebuilt, but some copies are missing, its status can be
changed to "readable". Then, if all the copies of the object are found, its
status can be changed to "complete".
\* Note: the file renaming is for now a temporary way to deal with multiple
oids with multiple uuids.

## Commands

A new command will be added to Phobos: `phobos tape import <TapeID>` whose
goal will be to import a newly-added, non-empty tape inside the Phobos system.

### Optional parameters

- `--check-hash`:   Recalculates and compares with md5/xxh128 from extended
                    attributes,
- `--unlock`:       Unlocks the tape after the import.

## "Importing" status for media

For `media`, a new fs_status value called "importing" will be available. The
goal is to enable the tape to be added to the DSS without having to keep the
"blank" status provided by the `phobos tape add` function. This new status will
be called only for tape import, and will also ensure that these tapes cannot
be accessed by other users until the end of the import.

## Wanted functionnalities

The `tape import` has to make the same work as the `tape add` function, but by
changing the tape status to "importing" instead of "blank". If the previous
step is successful, the tape import command should be able to:
```
for each tape to be imported
    for each file on the tape
        - read file name and extended attributes to recover information
        if `--check-hash` option is enabled:
            - read xattrs to find md5/xxh128
            - recalculate md5/xxh128
            - compare md5/xxh128 with recalculations from file inner data
            if hashs mismatch:
                - mark extent with the "corrupted" status
            fi
        fi

        - add new extent entries to the `extent` table as "orphan"
        if no other object with the same id in the `object` and `deprecated
        object` tables
            - create new "incomplete" object with version of extent
        else
            for objects and deprecated objects with the same oid
                if both have same uuid
                    if version of extent > version of obj
                        - create new "incomplete" object with version of extent
                        - move older object to the `deprecated object` table
                    elif version of extent < version of obj
                        - create new "incomplete" deprecated object with
                        version of extent
                    else
                        if there is already one extent with the same lyt_index
                            - ignore this extent, and raise an error message
                        fi
                    fi
                else
                    - rename the new extent with the ".import-{current-date}"
                    suffix
                fi
            done
        fi
        if extent was successfully inserted:
            - set `extent` status to "sync"
        fi
    done
done
for each "incomplete" or "usable" `object` and `deprecated object`
    if at least one copy for each split of the object
        set object status to "usable"

    if all copies of the object are detected
        set object status to "complete"
done
```
