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
set -e

test_bin_dir=$(dirname "${BASH_SOURCE[0]}")

. $test_bin_dir/test_env.sh
. $test_bin_dir/setup_db.sh
. $test_bin_dir/test_launch_daemon.sh


function cleanup() {
    waive_daemon
    drop_tables
}

trap drop_tables ERR EXIT

drop_tables
setup_tables
insert_examples

function test_dir_operation_flags() {
    local dir=$1
    local expected_put=$2
    local expected_get=$3
    local expected_delete=$4

    if [[ $($phobos dir list --output put_access $dir) != $expected_put ]]; then
        echo "Error: $expected_put was expected for put operation flag for $dir"
        return 1
    fi

    if [[ $($phobos dir list --output get_access $dir) != $expected_get ]]; then
        echo "Error: $expected_get was expected for get operation flag for $dir"
        return 1
    fi

    if [[ $($phobos dir list --output delete_access $dir) != \
        $expected_delete ]]; then
        echo "Error: $expected_delete was expected for delete operation flag"\
             "for $dir"
        return 1
    fi

    return 0
}

echo

echo "**** TESTS: CLI LIST MEDIA OPERATION TAGS ****"
$phobos dir list --output put_access,get_access,delete_access

echo "**** TESTS: CLI \"set-access\" bad syntax detection ****"
$phobos dir set-access p /tmp/pho_testdir1 &&
    echo "p should be a bad FLAG" && exit 1
$phobos dir set-access -- -PGJ /tmp/pho_testdir1 &&
    echo "PGJ- should be a bad FLAG" && exit 1

echo "**** TESTS: CLI SET MEDIA OPERATION TAGS ****"
$phobos dir set-access P /tmp/pho_testdir1
test_dir_operation_flags /tmp/pho_testdir1 True False False
$phobos dir set-access +G /tmp/pho_testdir1
test_dir_operation_flags /tmp/pho_testdir1 True True False
$phobos dir set-access -- -PD /tmp/pho_testdir1
test_dir_operation_flags /tmp/pho_testdir1 False True False
$phobos dir set-access +PD /tmp/pho_testdir1
test_dir_operation_flags /tmp/pho_testdir1 True True True

trap cleanup ERR EXIT
invoke_daemon

echo "**** TESTS: PUT MEDIA OPERATION TAGS ****"
# remove all dir put access
$phobos dir set-access -- -P $($phobos dir list)
# try one put without any dir put access
$phobos put --family dir /etc/hosts host1 &&
    echo "Put without any medium with 'P' operation flag should fail" &&
    exit 1
# set one put access
$phobos dir set-access +P /tmp/pho_testdir3
# try to put with this new dir put access
$phobos put --family dir /etc/hosts host2
# check the used dir corresponds to the one with the put access
if [[ $($phobos extent list --output media_name host2) != \
    "['/tmp/pho_testdir3']" ]]
then
    echo "Extent should be on the only medium with put access"
    exit 1
fi

echo "**** TESTS: GET MEDIA OPERATION TAGS ****"
# put a new object to get
$phobos put --family dir /etc/hosts obj_to_get
# remove all dir get access
$phobos dir set-access -- -G $($phobos dir list)
# try one get without any dir get access
$phobos get obj_to_get /tmp/gotten_obj &&
    echo "Get without any medium with 'G' operation flag should fail" &&
    rm /tmp/gotten_obj &&
    exit 1
# set get access on all dir
$phobos dir set-access +G $($phobos dir list)
# try to get
$phobos get obj_to_get /tmp/gotten_obj
rm /tmp/gotten_obj

echo "*** TEST END ***"
# Uncomment if you want the db to persist after test
# trap - EXIT ERR
