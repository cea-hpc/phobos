#!/bin/bash

# as user 'postgres' first:
# createuser -P phobos
# createdb phobos
# then either:
#   createuser root
#   in psql -- GRANT phobos TO root;
# or
#   in pg_hba.conf: local phobos phobos   md5


PSQL="psql phobos"

setup_tables() {
	$PSQL << EOF
CREATE TYPE techno AS ENUM ('LTO6', 'LTO5', 'T10KB');
CREATE TYPE adm_status AS ENUM ('locked', 'unlocked');
CREATE TYPE fs_status AS ENUM ('blank', 'empty', 'used', 'full');

-- enums extensibles: ALTER TYPE type ADD VALUE 'value'


--  id UNIQUE ? pk (type, id) ?
--  host json because host array
CREATE TABLE device(type techno, id varchar(32) UNIQUE, host jsonb,
                    adm_status adm_status, PRIMARY KEY (id));
--  CREATE INDEX ON device(type, id);
CREATE INDEX ON device USING gin(host);

CREATE TABLE media(type techno, id varchar(32) UNIQUE, adm_status adm_status,
                   fs_status fs_status, vol_used bigint, vol_free bigint,
                   PRIMARY KEY (id));


CREATE TABLE object(oid varchar(32), user_md jsonb, st_md json,
                    PRIMARY KEY (oid));

CREATE TABLE extent(oid varchar(32), layout_idx int, media varchar(32),
                    address varchar(256), size bigint, PRIMARY KEY (oid));
CREATE INDEX ON extent(media);

-- that or put all layouts for a single object in a json array?
-- But not sure we can index/query on any elemetns of an array...


EOF

}

drop_all() {
	$PSQL << EOF
DROP SCHEMA public CASCADE;
CREATE SCHEMA public;
EOF
}

insert_example() {
	$PSQL << EOF
insert into device values ('LTO6', '4212', '["foo1"]', 'locked');
EOF
}

select_example() {
	$PSQL << EOF
select host->>0 as firsthost from device; -- needs 9.3
select * from device where host ? 'foo1'; -- needs 9.4
EOF

}

