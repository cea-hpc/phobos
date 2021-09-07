#!/bin/bash

#
#  All rights reserved (c) 2014-2021 CEA/DAM.
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
set -xe

test_bin_dir=$(dirname "${BASH_SOURCE[0]}")

. $test_bin_dir/../../test_env.sh
. $test_bin_dir/setup_db.sh
. $test_bin_dir/test_launch_daemon.sh

################################################################################
#                                    SETUP                                     #
################################################################################

function cleanup() {
    waive_daemon
    drop_tables
    for d in $TEST_MNT; do
        rm -rf $d
    done
}

trap drop_tables ERR EXIT

TEST_MNT="/tmp/pho_testdir1 /tmp/pho_testdir2 /tmp/pho_testdir3 \
          /tmp/pho_testdir4 /tmp/pho_testdir5"
for dir in $TEST_MNT; do
    # clean and re-create
    rm -rf $dir/*
    # allow later cleaning by other users
    umask 000
    [ ! -d $dir ] && mkdir -p "$dir"
done

drop_tables
setup_tables
insert_examples
trap cleanup ERR EXIT
invoke_daemon

################################################################################
#                         TEST MEDIA UPDATE WITH TAGS                          #
################################################################################

function test_put_update() {
    family=$1
    media_name=$2
    obj=$3

    # put first_tag on the media
    $phobos $family update --tags first_tag $media_name

    # put an object asking a media tagged with "first_tag"
    $phobos put --family $family --tags first_tag /etc/hosts ${obj}1

    # put second_tag on the media when it is tagged with old first_tag
    $phobos $family update --tags second_tag $media_name

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
    # add and unlock one LTO6 drive
    $phobos drive add --unlock /dev/st4
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
$phobos dir set-access p /tmp/pho_testdir1 && error "p should be a bad FLAG"
$phobos dir set-access -- -PGJ /tmp/pho_testdir1 &&
    error "PGJ- should be a bad FLAG"

echo "**** TESTS: CLI SET MEDIA OPERATION TAGS ****"
$phobos dir set-access P /tmp/pho_testdir1
test_dir_operation_flags /tmp/pho_testdir1 True False False
$phobos dir set-access +G /tmp/pho_testdir1
test_dir_operation_flags /tmp/pho_testdir1 True True False
$phobos dir set-access -- -PD /tmp/pho_testdir1
test_dir_operation_flags /tmp/pho_testdir1 False True False
$phobos dir set-access +PD /tmp/pho_testdir1
test_dir_operation_flags /tmp/pho_testdir1 True True True

echo "**** TESTS: PUT MEDIA OPERATION TAGS ****"
# remove all dir put access
$phobos dir set-access -- -P $($phobos dir list)
# try one put without any dir put access
$phobos put --family dir /etc/hosts host1 &&
    error "Put without any medium with 'P' operation flag should fail"
# set one put access
$phobos dir set-access +P /tmp/pho_testdir3
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
$phobos dir set-access -- -G $($phobos dir list)
# try one get without any dir get access
$phobos get obj_to_get /tmp/gotten_obj && rm /tmp/gotten_obj &&
    error "Get without any medium with 'G' operation flag should fail"
# set get access on all dir
$phobos dir set-access +G $($phobos dir list)
# try to get
$phobos get obj_to_get /tmp/gotten_obj && rm /tmp/gotten_obj
