# Grouped Put

/!\ user is used for both admins and non-admins throughout this document, unless
explicitely said otherwise.

Currently, Phobos does not allow a user to indicate relationships between
objects. Related objects are more likely to be read together. If they are
scattered across several tapes, reading them will take a long time.

Furthermore, the command `phobos mput` does not garantee that objects will be
stored on the same tape. This can also be problematic as it is likely that the
objects written with mput will be read together as well.

A solution to these issues is to be able to do grouped puts, meaning puts that
are linked together, but do not prevent the tapes from being used for other
operations.

## Context

Consider the following configuration:
 - suppose a Phobos system with:
   - 3 objects A, B and C, with B and C logically linked
   - 2 tapes Ta and Tb
 - A is already on Ta
 - length(B) + length(C) > available\_size(Ta)
 - length(B) < available\_size(Ta), length(C) < available\_size(Ta)

The goal is to write B and C on the same tape, as they are logically linked
together, so Tb should be picked here.

## Use case

One of the use case that we have for this feature is with Lustre and an extended
attribute on files. In that case, it would work as such:
 - a user sets an extended attribute on some of their files to express a logical
link between them, as only them can know this link
 - RobinHood sees a list of files that should be archived
 - it starts the archival of the files, and send them to the copytool
 - copytool transfers the request with this information to Phobos
 - Phobos receives the request with the additional information
 - it tries to use the info to select a medium where the data should go

The above procedure is one example of a use case, but statistics other than
extended attributes could be used in the same fashion. As such, we could
use properties of entries to archive to determine if they should be grouped. For
instance, archive all entries in the same directory or from the same user in
the same medium, or all entries sharing a same job ID.

## Specific issues

### Logical link between objects

The first issue to tackle is the logical link between objects: how can the user
indicate to Phobos the link between objects? This can be done by having a
special user metadata associated to each object, so that we can for instance
easily establish the list of objects that are linked together.

Then, to have that link be meaningful to Phobos, they need to be stored as much
as possible on the same medium, to reduce the number of media needed to read
the linked objects. For this, similarly, we add a medium tag that will be
associated to that object link, ensuring all the linked objects are written on
that medium.

In both cases, we propose to introduce a new concept that can express that
group of linked data either for the length of time of a batch request or for a
longer period of time for singular requests. It will simply be called the
"grouping".

### Length of time of the logical link

Since data can be put either in a batch using an mput, or trickled down in
singular requests, we need to be able to express a logical link over a possibly
extended period of time.

Moreover, when a request with an already existing grouping is received, we may
want to use the same medium again. That means the groupings have to be
permanently recorded.

### Duration of the groupings

For the duration of the groupings, we propose to keep the grouping as long as
the medium is not full. If it isn't full, but cannot hold another object of the
grouping, another medium will be selected to be part of that grouping. When a
medium reaches its maximum capacity, the medium is removed from the grouping.

### Location of the extents

A medium can be part of multiple groupings. Since we need to know which
groupings are stored on a medium, we need to associate each medium with its set
of groupings. The grouping can be stored as a special tag (prefixed with
"grouping." for example) or stored in a separate column as a JSON array to keep
the two concepts separated.

On the other hand, we also want to keep the link between the objects in the
database, so we have to do a similar choice for the `object` and `deprecated
object` tables. Therefore, we choose to prefix the groupings with a specific
string that the user does not have access to.

To allow multiple groupings, the grouping value given when inserting objects
will be a comma-separated list of groupings, and those groupings will then be
stored as the object's metadata with the key `groupings` and the value the same
list of groupings.

Finally, when a put with a grouping is received, we must check if that grouping
exists. If it does, we retrieve the list of media associated to that grouping,
and select one of them for the put. If none is available, we revert back to the
standard procedure and instead pick any medium.

## Implementation plan

- Implement the `multi put` feature
  - Add a `--file` option to the `put` CLI command
    - if that option is given, `src_file` and `oid` are not necessary
    - file given will follow the same syntax as `mput`'s one
  - Remove the `mput` command
  - Add an option `no-split` to prevent the spliting of entries over
multiple media
    - Calculate the total size required to put that list
    - Ask the LRS for a put of that size
      - If there is no medium with free space of the size required available,
return an error
    - Put all the objects of the obtained medium
  - Add an option `grouping` to the `put` CLI command and API function
    - Add the object's grouping in its user metadata
    - Send the put request to the LRS with the grouping as an additional
parameter
    - If that grouping exists in the database (verified by checking if a medium
has a tag corresponding to the grouping), try to use a medium tagged with
that grouping
    - If not possible or the grouping doesn't exist in the database, select a
new medium and tag it with that grouping
