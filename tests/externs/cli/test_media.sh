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

test_dir=$(dirname $(readlink -e $0))
. $test_dir/../../test_env.sh
. $test_dir/../../setup_db.sh
. $test_dir/../../test_launch_daemon.sh
. $test_dir/../../tape_drive.sh
. $test_dir/../../utils_generation.sh

set -xe

################################################################################
#                                    SETUP                                     #
################################################################################

TEST_MNT="/tmp/pho_testdir1 /tmp/pho_testdir2 /tmp/pho_testdir3 \
          /tmp/pho_testdir4 /tmp/pho_testdir5"

function setup()
{
    setup_tables
    if [[ -w /dev/changer ]]; then
        invoke_daemons
    else
        invoke_lrs
    fi

    rm -rf $TEST_MNT
    umask 000
    mkdir -p $TEST_MNT
    $phobos dir add $TEST_MNT
    $phobos dir format --fs POSIX --unlock $TEST_MNT
}

function cleanup() {
    if [[ -w /dev/changer ]]; then
        waive_daemons
        drain_all_drives
    else
        waive_lrs
    fi

    drop_tables
    for d in $TEST_MNT; do
        rm -rf $d
    done
}

trap cleanup EXIT
setup

################################################################################
#                         TEST MEDIA UPDATE WITH TAGS                          #
################################################################################

function test_put_update() {
    family=$1
    media_name=$2
    obj=$3

    # put first_tag on the media
    $valg_phobos $family update --tags first_tag $media_name

    # put an object asking a media tagged with "first_tag"
    $phobos put --family $family --tags first_tag /etc/hosts ${obj}1

    # put second_tag on the media when it is tagged with old first_tag
    $valg_phobos $family update --tags second_tag $media_name

    # put again one object using outdated tag and check it is updated
    $phobos put --family $family --tags first_tag /etc/hosts ${obj}2 &&
        error "Using an old tag to put should fail"

    return 0
}

echo

echo "**** TESTS: TAGS a currently used dir ****"
test_put_update "dir" "/tmp/pho_testdir1" "dir_obj"

if [[ -w /dev/changer ]]; then
    echo "**** TESTS: TAGS a currently used tape ****"
    drain_all_drives
    # add and unlock one LTO6 drive
    lto6_drive=$(get_lto_drives 6 1)
    $phobos drive add --unlock ${lto6_drive}
    # add one LT06 tape with first_tag
    tape_name=$(mtx status | grep VolumeTag | sed -e "s/.*VolumeTag//" |
                tr -d " =" | grep "L6" | head -n 1)
    $phobos tape add -t lto6 $tape_name
    # format and unlock the LTO6 tape
    $phobos tape format --unlock $tape_name

    test_put_update "tape" "$tape_name" "tape_obj"
fi

################################################################################
#                     TEST UPDATE OF MEDIA OPERATION FLAGS                     #
################################################################################

function test_dir_operation_flags() {
    local dir=$1
    local expected_put=$2
    local expected_get=$3
    local expected_delete=$4

    if [[ $($phobos dir list --output put_access $dir) != $expected_put ]]; then
        error "Error: $expected_put was expected for put operation flag for " \
              "$dir"
    fi

    if [[ $($phobos dir list --output get_access $dir) != $expected_get ]]; then
        error "Error: $expected_get was expected for get operation flag for " \
              "$dir"
    fi

    if [[ $($phobos dir list --output delete_access $dir) != \
        $expected_delete ]]; then
        error "Error: $expected_delete was expected for delete operation " \
              "flag for $dir"
    fi

    return 0
}

echo

echo "**** TESTS: CLI LIST MEDIA OPERATION TAGS ****"
$phobos dir list --output put_access,get_access,delete_access

echo "**** TESTS: CLI \"set-access\" bad syntax detection ****"
$valg_phobos dir set-access p /tmp/pho_testdir1 &&
    error "p should be a bad FLAG"
$valg_phobos dir set-access -- -PGJ /tmp/pho_testdir1 &&
    error "PGJ- should be a bad FLAG"

echo "**** TESTS: CLI SET MEDIA OPERATION TAGS ****"
$valg_phobos dir set-access P /tmp/pho_testdir1
test_dir_operation_flags /tmp/pho_testdir1 True False False
$valg_phobos dir set-access +G /tmp/pho_testdir1
test_dir_operation_flags /tmp/pho_testdir1 True True False
$valg_phobos dir set-access -- -PD /tmp/pho_testdir1
test_dir_operation_flags /tmp/pho_testdir1 False True False
$valg_phobos dir set-access +PD /tmp/pho_testdir1
test_dir_operation_flags /tmp/pho_testdir1 True True True

echo "**** TESTS: PUT MEDIA OPERATION TAGS ****"
# remove all dir put access
$valg_phobos dir set-access -- -P $($phobos dir list)
waive_lrs
invoke_lrs
# try one put without any dir put access
$phobos put --family dir /etc/hosts host1 &&
    error "Put without any medium with 'P' operation flag should fail"
# set one put access
$valg_phobos dir set-access +P /tmp/pho_testdir3
waive_lrs
invoke_lrs
# try to put with this new dir put access
$phobos put --family dir /etc/hosts host2
# check the used dir corresponds to the one with the put access
if [[ $($phobos extent list --output media_name host2) != \
    "['/tmp/pho_testdir3']" ]]
then
    error "Extent should be on the only medium with put access"
fi

echo "**** TESTS: GET MEDIA OPERATION TAGS ****"
# put a new object to get
$phobos put --family dir /etc/hosts obj_to_get
# remove all dir get access
$valg_phobos dir set-access -- -G $($phobos dir list)
waive_lrs
invoke_lrs
# try one get without any dir get access
$phobos get obj_to_get /tmp/gotten_obj && rm /tmp/gotten_obj &&
    error "Get without any medium with 'G' operation flag should fail"
# set get access on all dir
$valg_phobos dir set-access +G $($phobos dir list)
waive_lrs
invoke_lrs
# try to get
$phobos get obj_to_get /tmp/gotten_obj && rm /tmp/gotten_obj

################################################################################
#                              TEST MEDIA LIST                                 #
################################################################################

function test_media_list_library() {
    setup_test_dirs

    $phobos dir add  $DIR_TEST_IN --library lib1
    $phobos dir add  $DIR_TEST_OUT --library lib2

    if [[ -w /dev/changer ]]; then
        tapes=($(get_tapes L6 2 | nodeset -e))
        $phobos tape add --type lto6 ${tapes[0]} --library lib1
        $phobos tape add --type lto6 ${tapes[1]} --library lib2
    fi

    output=$($phobos dir list --library lib1)
    echo "$output" | grep "$DIR_TEST_IN" && ! echo "$output" | grep \
        "$DIR_TEST_OUT"

    output=$($phobos dir list --library lib2)
    echo "$output" | grep "$DIR_TEST_OUT" && ! echo "$output" | grep \
        "$DIR_TEST_IN"

    if [[ -w /dev/changer ]]; then
        output=$($phobos tape list --library lib1)
        echo "$output" | grep "${tapes[0]}" && ! echo "$output" | grep \
            "${tapes[1]}"

        output=$($phobos tape list --library lib2)
        echo "$output" | grep "${tapes[1]}" && ! echo "$output" | grep \
            "${tapes[0]}"
    fi

    cleanup_test_dirs
}

test_media_list_library
