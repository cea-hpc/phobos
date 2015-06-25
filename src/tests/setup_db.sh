#!/bin/bash

PSQL="psql phobos"

setup_db() {
	su postgres -c "createuser phobos"
	su postgres -c "createdb phobos"
	su postgres -c "createuser root"
	# instead of creating user root, it's possible to change pg_hba.conf
	# and configure login as phobos directly
	su postgres -c "$PSQL" << EOF
GRANT ALL ON DATABASE phobos TO phobos;
ALTER SCHEMA public OWNER TO phobos;
GRANT phobos TO root;
EOF
}

drop_db() {
	su postgres -c "dropdb phobos"
	su postgres -c "dropuser phobos"
	su postgres -c "dropuser root"
}

setup_tables() {
	$PSQL << EOF
CREATE TYPE dev_family AS ENUM ('disk', 'tape');
CREATE TYPE dev_model AS ENUM ('ULTRIUM-TD6', 'ULTRIUM-TD5');
CREATE TYPE tape_model AS ENUM ('LTO6', 'LTO5', 'T10KB');
CREATE TYPE adm_status AS ENUM ('locked', 'unlocked');
CREATE TYPE fs_type AS ENUM ('POSIX', 'LTFS');
CREATE TYPE address_type AS ENUM ('PATH', 'HASH1', 'OPAQUE');

-- enums extensibles: ALTER TYPE type ADD VALUE 'value'


--  id UNIQUE ? pk (type, id) ?
--  host json because host array
CREATE TABLE device(family dev_family, model dev_model,
                    id varchar(32) UNIQUE, host jsonb, adm_status adm_status,
                    PRIMARY KEY (id));
--  CREATE INDEX ON device(type, id);
CREATE INDEX ON device USING gin(host);

CREATE TABLE media(model tape_model, id varchar(32) UNIQUE,
                   adm_status adm_status, fs_type fs_type,
                   address_type address_type, stats jsonb,
                   PRIMARY KEY (id));

CREATE TABLE object(oid varchar(1024), user_md jsonb, st_md json,
                    PRIMARY KEY (oid));

CREATE TABLE extent(oid varchar(1024), layout_idx int, media varchar(32),
                    address varchar(256), size bigint, PRIMARY KEY (oid));
CREATE INDEX ON extent(media);

-- that or put all layouts for a single object in a json array?
-- But not sure we can index/query on any elemetns of an array...


EOF

}

drop_tables() {
	$PSQL << EOF
DROP SCHEMA public CASCADE;
CREATE SCHEMA public;
EOF
}

insert_examples() {
	$PSQL << EOF
insert into device (family, model, id, host, adm_status)
    values ('tape', 'ULTRIUM-TD6', '1013005381', '["phobos1"]', 'locked'),
           ('tape', 'ULTRIUM-TD6', '1014005381', '["phobos1"]', 'unlocked');
insert into media (model, id, adm_status, fs_type, address_type, stats)
    values ('LTO6', '073220L6', 'unlocked', 'LTFS', 'HASH1',
            '{"nb_obj":"2","logc_spc_used":"6291456000",\
	      "phys_spc_used":"42469425152","phys_spc_free":"2365618913280"}'),
           ('LTO6', '073221L6', 'unlocked', 'LTFS', 'HASH1',
            '{"nb_obj":"2","logc_spc_used":"15033434112",\
	      "phys_spc_used":"15033434112","phys_spc_free":"2393054904320"}');
EOF
}

select_example() {
	$PSQL << EOF
select host->>0 as firsthost from device; -- needs 9.3
select * from device where host ? 'foo1'; -- needs 9.4
EOF

}

# if we're being sourced, don't parse arguments
[[ $(caller | cut -d' ' -f1) != "0" ]] && return 0

usage() {
	echo "Usage: . $0"
	echo "  OR   $0 ACTION [ACTION [ACTION...]]"
	echo "where  ACTION := { setup_db | drop_db | setup_tables |"
	echo "                   drop_tables | insert_examples }"
	exit -1
}

if [[ $# -eq 0 ]]; then
	usage
fi

while [[ $# -gt 0 ]]; do
	case "$1" in
	setup_db)
		setup_db
		;;
	drop_db)
		drop_db
		;;
	setup_tables)
		setup_tables
		;;
	drop_tables)
		drop_tables
		;;
	insert_examples)
		insert_examples
		;;
	*)
		usage
		;;
	esac
	shift
done
