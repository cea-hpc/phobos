#!/bin/bash
# -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
# vim:expandtab:shiftwidth=4:tabstop=4:

#
#  All rights reserved (c) 2014-2019 CEA/DAM.
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

# This integration script tests the simple layout on a split case.
#
# put and get the object content "AAAABBBB" :
# medium 1 : [AAAA]
# medium 2 : [BBBB]

set -xe

export PHOBOS_LRS_families="dir"
export PHOBOS_STORE_default_layout="simple"
export PHOBOS_STORE_default_family="dir"
LOG_VALG="$LOG_COMPILER $LOG_FLAGS"

# set python and phobos environment
test_dir=$(dirname $(readlink -e $0))
. $test_dir/test_env.sh
. $test_dir/setup_db.sh
. $test_dir/test_launch_daemon.sh

IN_FILE=/tmp/simple_split_in_file
OUT_FILE=/tmp/simple_split_out_file
OBJECT=simple_split_test
DIR1=/tmp/simple_split_testdir1
DIR2=/tmp/simple_split_testdir2
PART1_SIZE=1024
PART2_SIZE=1024
((FULL_SIZE=PART1_SIZE + PART2_SIZE))

function insert_dir
{
    local PSQL="psql phobos -U phobos"
    local host=$(hostname -s)

    $PSQL << EOF
insert into device (family, model, id, host, adm_status, path)
    values ('dir', NULL, '$host:$DIR1', '$host', 'unlocked', '$DIR1'),
           ('dir', NULL, '$host:$DIR2', '$host', 'unlocked', '$DIR2');

insert into media (family, model, id, adm_status, fs_type, address_type,
                   fs_status, stats, tags)
    values ('dir', NULL, '$DIR1', 'unlocked', 'POSIX', 'HASH1', 'empty',
            '{"nb_obj":0, "logc_spc_used":0, "phys_spc_used":0,\
              "phys_spc_free":$PART1_SIZE, "nb_load":0, "nb_errors":0, \
              "last_load":0}', '[]'),
           ('dir', NULL, '$DIR2', 'unlocked', 'POSIX', 'HASH1', 'empty',
            '{"nb_obj":0, "logc_spc_used":0, "phys_spc_used":0, \
              "phys_spc_free":$PART2_SIZE, "nb_load":0, "nb_errors":0, \
              "last_load":0}', '[]');

EOF
}

function dir_setup
{
    rm -rf $DIR1 $DIR2
    mkdir -p $DIR1 $DIR2
}

function cleanup
{
    waive_daemon
    rm -rf $DIR1 $DIR2
    rm -rf $IN_FILE $OUT_FILE
    drop_tables
}

function create_in_file
{
    rm -rf $IN_FILE
    dd if=/dev/random of=$IN_FILE count=$FULL_SIZE  bs=1
}

# start with a clean/empty phobos DB
drop_tables
setup_tables
# set start context
create_in_file
dir_setup
insert_dir
invoke_daemon
# clean at exit
trap cleanup EXIT

# put file
$LOG_VALG $phobos --verbose put $IN_FILE $OBJECT

# get file
$LOG_VALG $phobos --verbose get $OBJECT $OUT_FILE

# check got file
cmp $IN_FILE $OUT_FILE

trap - EXIT
cleanup || true

