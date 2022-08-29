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

#
# Context initializer for lock clean API call tests
#

test_bin_dir=$(dirname $(readlink -e $0))
test_bin="$test_bin_dir/test_lock_clean"
. $test_bin_dir/../../test_env.sh
. $test_bin_dir/setup_db.sh
. $test_bin_dir/test_launch_daemon.sh

set -xe

function setup
{
    setup_tables
    psql phobos phobos << EOF
insert into lock (type, id, owner, hostname)
values
        ('media'::lock_type, '1', 1, 'host1'),
        ('object'::lock_type, '1', 3, 'host1'),
        ('device'::lock_type, '1', 2, 'host1'),
        ('media_update'::lock_type, '1', 4, 'host1'),

        ('media'::lock_type, '2', 3, 'host2'),
        ('object'::lock_type, '2', 2, 'host2'),
        ('device'::lock_type, '2', 1, 'host2'),
        ('media_update'::lock_type, '2', 4, 'host2'),

        ('media_update'::lock_type, '3', 4, '$host'),
        ('device'::lock_type, '3', 1, '$host'),
        ('object'::lock_type, '3', 3, '$host'),
        ('media'::lock_type, '3', 2, '$host'),

        ('device'::lock_type, '4', 1, '$host'),
        ('media_update'::lock_type, '4', 4, '$host');

insert into device (family, model, id, host, adm_status,path)
values
        ('disk'::dev_family, NULL, '1',
            'host1', 'locked'::adm_status, 'path1'),
        ('dir'::dev_family, NULL, '2',
            'host2', 'unlocked'::adm_status, 'path2'),
        ('tape'::dev_family, NULL, '3',
            'host3', 'locked'::adm_status, 'path3'),
        ('disk'::dev_family, NULL, '4',
            '$host', 'locked'::adm_status, 'path4');

insert into media (family, model, id, adm_status, fs_type, fs_label,
                   address_type, fs_status, stats, tags)
values
        ('disk'::dev_family, NULL, '1', 'locked'::adm_status,
            'POSIX'::fs_type, 'label1', 'PATH'::address_type,
            'full'::fs_status, '{}', '{}'),
        ('dir'::dev_family, NULL, '2', 'unlocked'::adm_status,
            'POSIX'::fs_type, 'label2', 'PATH'::address_type,
            'empty'::fs_status, '{}', '{}'),
        ('tape'::dev_family, NULL, '3', 'locked'::adm_status,
            'POSIX'::fs_type, 'label3', 'PATH'::address_type,
            'blank'::fs_status, '{}', '{}'),
        ('disk'::dev_family, NULL, '4', 'locked'::adm_status,
            'POSIX'::fs_type, 'label4', 'PATH'::address_type,
            'full'::fs_status, '{}', '{}');
EOF
    waive_daemon
}

function cleanup
{
    waive_daemon
    drop_tables
}

trap cleanup EXIT
drop_tables
setup

$LOG_COMPILER $test_bin
