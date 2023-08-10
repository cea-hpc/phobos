#!/bin/bash

#
#  All rights reserved (c) 2014-2022 CEA/DAM.
#
#  This file is part of Phobos.
#
#  Phobos is free software: you can redistribute it and/or modify it under
#  the terms of the GNU Lesser General Public License as published by
#  the Free Software Foundation, either version 2.1 of the License, or
#  (at your option) any later version.
#
#  Phobos is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU Lesser General Public License for more details.
#
#  You should have received a copy of the GNU Lesser General Public License
#  along with Phobos. If not, see <http://www.gnu.org/licenses/>.
#

cur_dir=$(dirname $(readlink -m ${BASH_SOURCE[0]}))
db_helper="$cur_dir/../scripts/phobos_db"

phobos_conf="$cur_dir/phobos.conf"
test_db="$(grep "dbname" "$phobos_conf" | awk -F 'dbname=' '{print $2}' | \
           cut -d ' ' -f1)"

PSQL="psql $test_db -U phobos"
. "$cur_dir/test_env.sh"

# host name to insert examples in DB
host=$(hostname -s)

insert_examples() {

#  initially mounted tapes don't have enough room to store a big file
	$PSQL << EOF
insert into device (family, model, id, host, adm_status, path)
    values ('dir', NULL, '$host:/tmp/pho_testdir1', '$host',
	    'unlocked', '/tmp/pho_testdir1'),
           ('dir', NULL, '$host:/tmp/pho_testdir2', '$host',
	    'unlocked', '/tmp/pho_testdir2'),
           ('dir', NULL, '$host:/tmp/pho_testdir3', '$host',
	    'unlocked', '/tmp/pho_testdir3'),
           ('dir', NULL, '$host:/tmp/pho_testdir4', '$host',
	    'unlocked', '/tmp/pho_testdir4'),
           ('dir', NULL, '$host:/tmp/pho_testdir5', '$host',
	    'unlocked', '/tmp/pho_testdir5');
insert into media (family, model, id, adm_status, fs_type, address_type,
		   fs_status, stats, tags)
    values ('dir', NULL, '/tmp/pho_testdir1', 'unlocked', 'POSIX',
	    'HASH1', 'empty', '{"nb_obj":5, "logc_spc_used":3668841456,\
	      "phys_spc_used":3668841456,"phys_spc_free":12857675776,\
          "nb_errors":0,"last_load":0}', '[]'),
           ('dir', NULL, '/tmp/pho_testdir2', 'unlocked', 'POSIX',
	    'HASH1', 'empty', '{"nb_obj":6,"logc_spc_used":4868841472,\
	      "phys_spc_used":4868841472,"phys_spc_free":12857675776,\
          "nb_errors":0,"last_load":0}', '["mytag"]'),
           ('dir', NULL, '/tmp/pho_testdir3', 'unlocked', 'POSIX',
	    'HASH1', 'empty', '{"nb_obj":0,"logc_spc_used":4868841472,\
	      "phys_spc_used":4868841472,"phys_spc_free":12857675776,\
          "nb_errors":0,"last_load":0}', '[]'),
           ('dir', NULL, '/tmp/pho_testdir4', 'unlocked', 'POSIX',
	    'HASH1', 'empty', '{"nb_obj":7,"logc_spc_used":4868841472,\
	      "phys_spc_used":4868841472,"phys_spc_free":12857675776,\
          "nb_errors":0,"last_load":0}', '["mytag"]'),
           ('dir', NULL, '/tmp/pho_testdir5', 'unlocked', 'POSIX',
	    'HASH1', 'empty', '{"nb_obj":7,"logc_spc_used":4868841472,\
	      "phys_spc_used":4868841472,"phys_spc_free":12857675776,\
          "nb_errors":0,"last_load":0}', '["mytag"]');

insert into object (oid, user_md, lyt_info, obj_status)
    values ('01230123ABC', '{}',
            '{"name":"raid1","major":0,"minor":1,"repl_count":1}', 'complete');

insert into deprecated_object (oid, object_uuid, version, user_md, lyt_info,
                               obj_status)
    values ('01230123ABD', '00112233445566778899aabbccddeeff', 1, '{}',
            '{"name":"raid1","major":0,"minor":1,"repl_count":1}', 'complete');

insert into extent (state, size, medium_family, medium_id, address, hash)
    values ('pending', 21123456, 'dir', '/tmp/pho_testdir1',
            'test3/oid3.v3.lyt-0_3.uuid3', '{}'),
           ('pending', 2112555, 'dir', '/tmp/pho_testdir2',
            'test4/oid4.v4.lyt-0_4.uuid4', '{}');

insert into layout (object_uuid, version, extent_uuid, layout_index)
    values ((select object_uuid from object where oid = '01230123ABC'), 1,
            (select extent_uuid from extent where address =
                'test3/oid3.v3.lyt-0_3.uuid3'), 0),
           ((select object_uuid from deprecated_object
               where oid = '01230123ABD'), 1,
            (select extent_uuid from extent where address =
                'test4/oid4.v4.lyt-0_4.uuid4'), 0);
EOF
}

select_example() {
	$PSQL << EOF
select host->>0 as firsthost from device; -- needs 9.3
select * from device where host ? 'foo1'; -- needs 9.4
EOF

}

function resize_medium
{
    local id="$1"
    local size="$2"

    $PSQL << EOF
UPDATE media SET
    stats = '{"nb_obj":0, "logc_spc_used":0, "phys_spc_used":0,\
              "phys_spc_free":$size, "nb_load":0, "nb_errors":0,\
              "last_load":0}'
    WHERE id = '$id';
EOF
}

setup_tables() {
    $db_helper setup_tables
}

drop_tables() {
    $db_helper drop_tables
}

usage() {
    echo "Usage: . $0"
    echo "  OR   $0 ACTION [ACTION [ACTION...]]"
    echo "where  ACTION := { setup_db | drop_db | setup_tables |"
    echo "                   drop_tables | insert_examples }"
    exit -1
}

# if we're being sourced, don't parse arguments
[[ $(caller | cut -d' ' -f1) != "0" ]] && return 0

if [[ $# -eq 0 ]]; then
    usage
fi

if [ "$1" == "insert_examples" ]; then
    insert_examples
else
    $db_helper $*
fi
