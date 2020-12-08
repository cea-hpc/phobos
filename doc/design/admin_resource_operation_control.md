How to restrict operations which can run on a given resource
============================================================

Administrative locking
----------------------

### Concept and use cases

Administratively locking a resource prevents phobos from using it for any
purpose (get, put, delete, format).

This can be used in case of a resource failure, if a media is damaged, at risk
of loosing data, or currently unavailable (eg. tape on shelf). Administrative
locking is different from resource locking which is used to manage concurrent
access to resources.

Administrative locking prevents future access, but does not cancel the ones
currently running. (only the concurrent locking system ensures an exclusive
access to the resources).

### Implementation
Administrative locking is implemented as an `adm_status` enum field in
`device` and `media` tables. This enum can take the following values:
* `unlocked`: the resource can be used.
* `locked`: the resource has been locked by an administrator.
* `failed`: the resource has been automatically locked by the system because
of too many failures.

A `locked` or `failed` resource is never used for any I/O operation
(put/get/delete).

A `locked` or `failed` resource is not used for any administrative
operation (like format), unless explicitely requested (--force).

### CLI
Admin commands allow locking/unlocking resources:
```console
phobos drive {lock|unlock} 'l5-dr0'
phobos tape {lock|unlock}  'T00001L5'
```

Administrative locking prevents format if not `--force`:
```console
$ # Lock one drive.
$ phobos drive lock 'l5-dr0'

$ # This command gets another drive to format the tapes.
$ phobos tape format 'T000[01-15]L5'

$ # Trying to explicitely use the locked drive returns an error.
$ phobos tape format 'T000[01-15]L5' -d 'l5-dr0'
Error: Drive 'l5-dr0' (S/N 'E143DB0006') is locked by admin.

$ # This command force using the specified drive.
$ phobos tape format 'T0000[01-15]L5' -d 'l5-dr0' --force
Warning: Using locked drive 'l5-dr0' (S/N 'E143DB0006').
```

Media operation flags: put, get and delete
------------------------------------------

### Concept and use cases
If an administrator wants to prevent any put, get or delete, of any extent on a
medium, they can do so by setting the corresponding flag. These flags are a
software emulation of hardware switches that can be found media to prevent
writing for example.

Like administrative locking, disabling a set of operations on a resource does
not cancel currently running operations. (only the concurrent locking system
ensures an exclusive access to the resources).

If an administrator wants to continue to use an old medium only to access
pre-existing old extents but prevent the creation of new extents on it, they can
disable put operation on it.

If an administrator wants to secure extents on a medium from being erased, they
can forbid delete operations on it.

If an administrator wants to prevent any modification on a medium and use it as
a "read-only" medium, they can disable put and delete operations and
they can enable only get operations.

### Implementation
`put`, `get` and `delete`, flags are implemented as separate boolean
fields into the `media` table. By default, these flags are `TRUE` and put, get
and delete, are authorized on any medium.

The administrator can use the phobos cli to set these flags.

### CLI

    > phobos tape set-access --help
    usage: phobos tape set-access [--help] FLAGS RESOURCE [RESOURCE ...]

    Set media operation flags

    positional arguments:
    FLAGS     [+|-]LIST, where LIST is made of capital letters among PGD,
                  P: put, G: get, D: delete
    RESOURCE  Resource(s) to add

    Examples:
    phobos tape set-access GD   # allow all except new object creation (no put)
    phobos tape set-access -PD  # forbid put and delete, (others are unchanged)
    phobos tape set-access +G   # allow get on medium (others are unchanged)


If an administrator wants to stop adding new data to a tape, they can use the
admin command:

    > phobos tape set-access -P 'T00001L5'
    Tape 'T00001L5' is updated: puts are disabled

If an administrator wants to authorized again or be sure that put operations are
authorized on a media, they can use the following command:

    > phobos tape set-access +P 'T00001L5'
    Tape 'T00001L5' is updated: puts are enabled

If an administrator wants to prevent any modification on a medium and allowing
only get operations, they can disable the put and delete flags by using
the following command:

    > phobos tape set-access G 'T00001L5'
    Tape 'T00001L5' is updated: puts are disabled
    Tape 'T00001L5' is updated: gets are enabled
    Tape 'T00001L5' is updated: deletes are disabled

If an administrator wants to allow puts and gets on a medium and to secure
existing data by preventing any deletion, he can use the following command:

    > phobos tape set-access PG 'T00001L5'
    Tape 'T00001L5' is updated: puts are enabled
    Tape 'T00001L5' is updated: gets are enabled
    Tape 'T00001L5' is updated: deletes are disabled

Administrative locking versus operation flags
---------------------------------------------

If you disable every media operation flags on a medium, the result is similar to
setting the `adm_status` to `locked` on the medium: no new operation will be
scheduled on this medium. But it is useful to keep both mechanisms concurrently
alive. Indeed, using the `adm_status` allows to keep safe the media operation
tags when the medium is unlocked again.

Imagine you have a *read-only* medium (due to a hardware failure, or too
sensitive, or only used to import data from an other system), an admin can use
the `phobos set-access -PD` command to disable put and delete flags. If there is
a transient need to stop get operations on it (remove the medium from the
library for example for a short time), the administrator can use the
`adm_status` `locked` in addition of preset media operation flags.
