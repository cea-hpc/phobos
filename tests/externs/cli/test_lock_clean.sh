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

#
# Integration tests for lock clean commands
#

test_dir=$(dirname $(readlink -e $0))
. $test_dir/../../test_env.sh
. $test_dir/../../setup_db.sh
. $test_dir/../../test_launch_daemon.sh

set -xe

PSQL="psql phobos -U phobos"

# local host name to insert examples in DB
host=$(hostname -s)

function setup
{
    setup_tables # necessary for the daemon's initialization
}

function insert_locks
{
    $PSQL << EOF
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
}

function cleanup
{
    waive_daemon
    drop_tables
}

function test_errors
{
    # Global without --force attribute
    $phobos lock clean --global \
        && error "global lock clean operation without force attribute " \
                 "should have failed" || true

    # Invalid type parameter
    $phobos lock clean -t wrong_type \
        && error "local lock clean operation should have failed : " \
                 "invalid type parameter" || true

    # Invalid family parameter
    $phobos lock clean -f wrong_family \
        && error "local lock clean operation should have failed : " \
                 "invalid family parameter" || true

    # No type given with valid family parameter
    $phobos lock clean -f dir \
        && error "local lock clean operation should have failed : " \
                 "No type given with valid family parameter" || true

    # Object type given with valid family parameter
    $phobos lock clean -t object -f dir \
        && error "local lock clean operation should have failed : " \
                 "object type given with family parameter" || true
}

function test_local_daemon_on
{
    invoke_daemon

    #Using phobos command without force attribute
    $phobos lock clean \
        && error "Using lock clean command without --force when phobos on " \
                 "should have failed" \
        || true

    #Using phobos command with force attribute when deamon is on
    $phobos lock clean --force \
        || error "--force attribute on lock clean command failed"
}

# TODO: verify if the right type locks are cleaned
function test_type_param
{
    $phobos lock clean -t device \
        || error "lock clean operation on 'device' type failed"

    $phobos lock clean -t media_update \
        || error "lock clean operation on 'media_update' type failed"

    $phobos lock clean -t media \
        || error "lock clean operation on 'media' type failed"

    $phobos lock clean -t object --global --force \
        || error "lock clean operation on 'object' type failed"
}

# TODO: verify if the right family locks are cleaned
function test_family_param
{
    $phobos lock clean -t media -f dir --global --force \
        || error "lock clean operation on 'dir' family failed"

    $phobos lock clean -t media_update -f disk \
        || error "lock clean operation on 'disk' family failed"

    $phobos lock clean -t device -f tape \
        || error "lock clean operation on 'tape' family failed"
}

# TODO: verify if the right locks are cleaned
function test_ids_param
{
    # remove object with id '3' on localhost
    $phobos lock clean -t object -i 3 \
        || error "local lock clean operation on object of id '3'"

    # globally remove media_update of id '2' and '3'
    $phobos lock clean --global --force -t media_update -i 2 3 \
        || error "global lock clean operaton on media_update of ids '2' and '3'"

    # clean an element with all parameters
    $phobos lock clean -t device -f dir -i 2 --global --force \
        || error "global lock clean operation with every parameter failed"

    # clean all elements with id '1'
    $phobos lock clean -i 1 --global --force\
        || error "global lock clean operation on elements of id '1' failed"
}

# TODO: Verify if the database is empty
function test_clean_all
{
    $phobos lock clean --force \
        || error "local lock clean operation on every element failed"

    $phobos lock clean --global --force \
        || error "global lock clean operation on every element failed"
}


trap cleanup EXIT
setup

insert_locks
test_errors

test_ids_param
test_family_param
test_type_param
test_clean_all
test_local_daemon_on
