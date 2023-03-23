# Extent migration

Extent migration is when we move an extent from one medium to another. It
could happen when we repack a tape into a new one for example. All extents
of the repacked tape are moved to the new one.

## Steps of this migration :

a) allocate the medium to repack for read and a destination medium for write
b) create the new extent on the DSS (state : pending)
c) create the new extent on the new medium by copying data from source extent
d) set the new extent state on the DSS as sync
e) replace the old extent by the new one in the layout
f) set the old extent as pending
g) remove the old extent from the source medium
h) remove the old extent from the DSS

### a) allocate the medium for read and a destination medium for write

#### mixed read + write LRS allocation

This could be done by asking to the LRS a read allocation of the source medium
THEN a write allocation to the destination medium.

OR

This could be done by asking the source medium read allocation AND the
destination medium allocation at the same time to the LRS into the same unique
request.

The second solution could offer better optimisation and avoid resource
management deadlock. But the LRS is currently (1.94) not ready to answer mixed
read/write requests, so the first solution could be a good fallback solution.

We will face the need to simultaneously allocate some media in read mode and
other in write mode not only at extent migration but also when we will try to
achieve layout extent reconstruction on failure for example.

#### Partial write LRS allocation not allowed to cut one extent

The read and write allocations could be done to migrate one extent or to migrate
a whole medium (repack intent). In both cases, the write allocation answer could
be a partial one. We could accept partial answer for global allocation
containing several objects but we do not accept at this step of the design a
write allocation answer that doesn't allow to copy at least one complete extent.
In other words, we cannot allow extent splitting at extent migration. The
layout of the object must be unchanged so the existing extent must stay
unchanged, just moved.

#### Sync during write allocation to many extents at a time

If we plan to ask global allocation to write many extents (full repack of one
tape for example), we need to add a new release mode if we want to ask sync
during allocation without ending this allocation. Indeed, currently (phobos
1.94), sync can be asked only when we finally release the allocation.

#### RAO at read allocation to many extents at a time

Of course, if we plan to migrate many extents from the same source media, as
for a repack for example, the RAO (Recommended Access Order) could be very
usefull to optimize all the read.

### b) create the new extent on the DSS

We could manage this new extent into the existing (SQL DB schema 1.93)
extents JSONB field of the extent table. This will add a new info into this
JSONB.

Or, we could create a new table dedicated to extent, separated from layout, and
create this new extent into this "new" extent table.

This new schema will split the existing (SQL DB schema 1.93) extent table
into a new extent table and a layout table. The extent table will list all
extents whereas the layout table will store the extent to object relation
which is a n to 1 if we don't have deduplication (each extent is only related to
one object) or a n to m if we achieve deduplication (each extent could be
reused in several objects). The layout type and parameter per object (existing
lyt_info field of the SQL DB schema 1.93) could be moved to the object table.

Such a new schema is useful to allow extent deduplication that could not be
achieved in current SQL DB schema 1.93. It will also allow to list extent per
medium without the expensive extents_mda_id_idx index (SQL DB schema 1.93). This
index extracts which extent is on which medium from the extents JSONB. This
index must be updated at each object creation (that is to say each PUT request)
whereas it is used only at repack or tape removal on error. With the new "extent
independent from layout" SQL schema, PUT operation and registering a new extent
will be done without computing any index. Listing extent per medium will also
be done without using any index but only by directly querying the new extent
table.

A schema where extents are managed independently from their layouts in a
dedicated table also allow to easily add lock per extent instead of only lock
per object. This could be useful to efficiently manage concurrency between
extent migration and other existing requests as put or get (see corresponding
paragraph below)

### e) replace the old extent by the new one in the layout

This must be done in the existing (SQL DB schema 1.93) JSONB extents field
or in the new layout table.

### g) remove the old extent from the source medium

This action could be ignored if the situation allows to let it. For example for
a repacked medium that will be erased or formatted, we don't need to remove old
extents at their migration.

## Lock and concurrency

These new extent migration operations could be executed concurrently to existing
PUT or GET operation, possibly on same object. We need to carefully manage
this concurrency by enforcing existing locking mechanism.
