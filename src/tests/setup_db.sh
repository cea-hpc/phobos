#!/bin/bash

cur_dir=$(dirname $(readlink -m $0))
db_helper=$cur_dir/../../scripts/pho_dss_helper

. $db_helper

# host name to insert examples in DB
host=$(hostname -s)

insert_examples() {

#  initially mounted tapes don't have enough room to store a big file
	$PSQL << EOF
insert into device (family, model, id, host, adm_status, path, lock)
    values ('dir', NULL, '$host:/tmp/pho_testdir1', '$host',
	    'unlocked', '/tmp/pho_testdir1', ''),
           ('dir', NULL, '$host:/tmp/pho_testdir2', '$host',
	    'unlocked', '/tmp/pho_testdir2', ''),
           ('dir', NULL, '$host:/tmp/pho_testdir3', '$host',
	    'unlocked', '/tmp/pho_testdir3', '');
insert into media (family, model, id, adm_status, fs_type, address_type,
		   fs_status, stats, lock)
    values ('dir', NULL, '/tmp/pho_testdir1', 'unlocked', 'POSIX',
	    'HASH1', 'empty', '{"nb_obj":5, "logc_spc_used":3668841456,\
	      "phys_spc_used":3668841456,"phys_spc_free":12857675776,\
          "nb_errors":0,"last_load":0}', ''),
           ('dir', NULL, '/tmp/pho_testdir2', 'unlocked', 'POSIX',
	    'HASH1', 'empty', '{"nb_obj":6,"logc_spc_used":4868841472,\
	      "phys_spc_used":4868841472,"phys_spc_free":12857675776,\
          "nb_errors":0,"last_load":0}', ''),
           ('dir', NULL, '/tmp/pho_testdir3', 'unlocked', 'POSIX',
	    'HASH1', 'empty', '{"nb_obj":0,"logc_spc_used":4868841472,\
	      "phys_spc_used":4868841472,"phys_spc_free":12857675776,\
          "nb_errors":0,"last_load":0}', '');

insert into object (oid, user_md)
    values ('01230123ABC', '{}');

insert into extent (oid, state, lyt_info, extents)
    values ('QQQ6ASQDSQD', 'pending', '{"name":"simple","major":0,"minor":1}',
           '[{"media":"/tmp/pho_testdir1","addr":"test3",
	   "sz":21123456,"fam":"dir"},\
            {"media":"/tmp/pho_testdir2","addr":"test4",
	   "sz":2112555,"fam":"dir"}]');

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
	insert_examples)
		insert_examples
		;;
	*)
		# forward all other calls to the db helper
		$db_helper $*
		;;
	esac
	shift
done
