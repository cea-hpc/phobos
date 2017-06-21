#!/bin/bash
#
# Converts phobos 1.0 DB schema to 1.1 DB schema.
#

# DB schema changes:
#   - drop type extent_lyt_type
#   - media: add field fs_label after fs_type with the previous value of 'id'
#   - extent:
#       ° pk of extent is only oid
#       ° drop field copy_num
#       ° drop field lyt_type
#       ° lyt_info='{"name":"simple","major":0,"minor":1}'

export PGPASSWORD="phobos"
PSQL="psql -U phobos -h localhost phobos"

convert_db() {
    $PSQL << EOF
-- column order doesn't matter
ALTER TABLE media ADD COLUMN fs_label VARCHAR(32) DEFAULT '' NOT NULL;

-- First remove PRIMARY KEY attribute of former PRIMARY KEY
ALTER TABLE extent DROP CONSTRAINT extent_pkey;
-- Then set the new PRIMARY KEY
ALTER TABLE extent ADD PRIMARY KEY (oid);

-- drop fields copy_num and lyt_type
ALTER TABLE extent DROP COLUMN copy_num;
ALTER TABLE extent DROP COLUMN lyt_type;

-- set layout info for existing extents
UPDATE extent SET lyt_info='{"name":"simple","major":0,"minor":1}';

-- Drop the type once no column use it
-- Don't precise 'IF EXISTS' as the DB schema is supposed to be 1.0
-- so the type is supposed to exist.
DROP TYPE extent_lyt_type;

EOF
    return $?
}

echo "You are about to run DB conversion to upgrade from phobos 1.0 to phobos \
1.1"
echo "Do you want to continue? [y/N]"
read x
[ x$x != xy ] && exit 1
echo "Start converting..."
convert_db
