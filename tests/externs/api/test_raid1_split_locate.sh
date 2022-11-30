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
. $test_dir/../../tape_drive.sh

set -xe

export PHOBOS_STORE_default_layout="raid1"
export PHOBOS_LAYOUT_RAID1_repl_count=2
# Force a tape split count of 2 to be sure to split on two different media
# by writting second split before syncing first one (syncing on tape
# update phy_spc_free and resize a tape to its "true" phy_spc_free, instead
# of keeping the "fake" 1024 bytes size of this test)
export PHOBOS_LRS_sync_nb_req="dir=1,tape=2,rados_pool=5"

test_raid1_split_locate_bin=$test_dir/test_raid1_split_locate
IN_FILE=/tmp/raid1_split_in_file
PART1_SIZE=1024
PART2_SIZE=1024
((FULL_SIZE=PART1_SIZE + PART2_SIZE))

# DIR
DIR_OBJECT=dir_raid1_split_test
dir_array=(/tmp/raid1_split_testdir1 /tmp/raid1_split_testdir2 \
           /tmp/raid1_split_testdir3 /tmp/raid1_split_testdir4)

# TAPE
TAPE_OBJECT=tape_raid1_split_test
declare -a tape_array

function dir_setup
{
    mkdir -p ${dir_array[@]}
    $phobos dir add ${dir_array[@]}
    $phobos dir format --fs POSIX --unlock ${dir_array[@]}
}

function tape_setup
{
    local N_LTO6_TAPES=4
    local N_LTO6_DRIVES=4

    # get LTO6 tapes
    local lto6_tapes="$(get_tapes L6 $N_LTO6_TAPES)"
    echo "adding tapes $lto6_tapes"
    $phobos tape add --type lto6 "$lto6_tapes"

    # unlock all tapes
    for t in $lto6_tapes; do
        $phobos tape unlock "$t"
    done

    # get LTO6 drives
    local drives=$(get_lto_drives 6 $N_LTO6_DRIVES)
    echo "adding drives $drives..."
    $phobos drive add $drives

    # unlock all drives
    for d in $($phobos drive list); do
        $phobos drive unlock $d
    done

    # format lto6 tapes
    $phobos --verbose tape format $lto6_tapes --unlock
    tape_array=($(nodeset -e $lto6_tapes))
}

function resize_media
{
    local family=$1
    shift
    local media_array=($@)

    $PSQL << EOF
UPDATE media SET
    stats = '{"nb_obj":0, "logc_spc_used":0, "phys_spc_used":0,\
              "phys_spc_free":$PART1_SIZE, "nb_load":0, "nb_errors":0,\
              "last_load":0}'
    WHERE family = '$family' AND (id = '${media_array[0]}' OR \
                                  id = '${media_array[1]}');

UPDATE media SET
    stats = '{"nb_obj":0, "logc_spc_used":0, "phys_spc_used":0,\
              "phys_spc_free":$PART2_SIZE, "nb_load":0, "nb_errors":0,\
              "last_load":0}'
    WHERE family = '$family' AND (id = '${media_array[2]}' OR \
                                  id = '${media_array[3]}');

EOF

    $PSQL << EOF
SELECT * from media;
EOF
}

function setup
{
    setup_tables
    invoke_daemon

    # set start context
    dd if=/dev/random of=$IN_FILE count=$FULL_SIZE  bs=1
    dir_setup
    if [[ -w /dev/changer ]]; then
        tape_setup
    fi
    waive_daemon
    resize_media dir ${dir_array[@]}
    if [[ -w /dev/changer ]]; then
        resize_media tape ${tape_array[@]}
    fi
    invoke_daemon
}

function cleanup
{
    waive_daemon
    drain_all_drives
    drop_tables
    rm -rf ${dir_array[@]}
    rm -rf $IN_FILE
}

trap cleanup EXIT
setup

$phobos --verbose put -f dir $IN_FILE $DIR_OBJECT
$LOG_COMPILER $test_raid1_split_locate_bin dir $DIR_OBJECT

if [[ -w /dev/changer ]]; then
    $phobos --verbose put -f tape $IN_FILE $TAPE_OBJECT
    $LOG_COMPILER $test_raid1_split_locate_bin tape $TAPE_OBJECT
fi
