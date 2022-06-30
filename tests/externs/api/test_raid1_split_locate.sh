#!/bin/bash
# -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
# vim:expandtab:shiftwidth=4:tabstop=4:

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

# This integration script tests the raid1 layout on a split case.
#
# put and get the object content "AAAABBBB" with a replica-count of 2 on :
# medium 1 : [AAAA]
# medium 2 : [AAAA]
# medium 3 : [BBBB]
# medium 4 : [BBBB]

# set python and phobos environment
test_dir=$(dirname $(readlink -e $0))
. $test_dir/../../test_env.sh
. $test_dir/../../setup_db.sh
. $test_dir/../../test_launch_daemon.sh

set -xe

export PHOBOS_LRS_families="dir"
export PHOBOS_STORE_default_layout="raid1"
export PHOBOS_STORE_default_family="dir"
export PHOBOS_LAYOUT_RAID1_repl_count=2

test_raid1_split_locate_bin=$test_dir/test_raid1_split_locate

IN_FILE=/tmp/raid1_split_in_file
OUT_FILE=/tmp/raid1_split_out_file
OBJECT=raid1_split_test
DIR1=/tmp/raid1_split_testdir1
DIR2=/tmp/raid1_split_testdir2
DIR3=/tmp/raid1_split_testdir3
DIR4=/tmp/raid1_split_testdir4
PART1_SIZE=1024
PART2_SIZE=1024
((FULL_SIZE=PART1_SIZE + PART2_SIZE))

function resize_dir
{
    $PSQL << EOF
UPDATE media SET
    stats = '{"nb_obj":0, "logc_spc_used":0, "phys_spc_used":0,\
              "phys_spc_free":$PART1_SIZE, "nb_load":0, "nb_errors":0,\
              "last_load":0}'
    WHERE family = 'dir' AND (id = '$DIR1' OR id = '$DIR2');

UPDATE media SET
    stats = '{"nb_obj":0, "logc_spc_used":0, "phys_spc_used":0,\
              "phys_spc_free":$PART2_SIZE, "nb_load":0, "nb_errors":0,\
              "last_load":0}'
    WHERE family = 'dir' AND (id = '$DIR3' OR id = '$DIR4');

EOF
}

function setup
{
    setup_tables
    invoke_daemon

    # set start context
    dd if=/dev/random of=$IN_FILE count=$FULL_SIZE  bs=1

    mkdir -p $DIR1 $DIR2 $DIR3 $DIR4
    $phobos dir add $DIR1 $DIR2 $DIR3 $DIR4
    $phobos dir format --fs POSIX --unlock $DIR1 $DIR2 $DIR3 $DIR4
    waive_daemon
    resize_dir
    invoke_daemon
}

function cleanup
{
    waive_daemon
    rm -rf $DIR1 $DIR2 $DIR3 $DIR4
    rm -rf $IN_FILE $OUT_FILE
    drop_tables
}

trap cleanup EXIT
setup

$phobos --verbose put $IN_FILE $OBJECT
$LOG_COMPILER $test_raid1_split_locate_bin $OBJECT
