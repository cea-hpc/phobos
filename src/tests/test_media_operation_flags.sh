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

trap drop_tables ERR EXIT

drop_tables
setup_tables
insert_examples

function test_dir_operation_flags() {
    local dir=$1
    local expected_put=$2
    local expected_get=$3
    local expected_delete=$4

    if [[ $($phobos dir list -o put_access $dir) != $expected_put ]]; then
        echo "Error: $expected_put was expected for put operation flag for $dir"
        return 1
    fi

    if [[ $($phobos dir list -o get_access $dir) != $expected_get ]]; then
        echo "Error: $expected_get was expected for get operation flag for $dir"
        return 1
    fi

    if [[ $($phobos dir list -o delete_access $dir) != $expected_delete ]]; then
        echo "Error: $expected_delete was expected for delete operation flag"\
             "for $dir"
        return 1
    fi

    return 0
}

echo

echo "**** TESTS: CLI LIST MEDIA OPERATION TAGS ****"
$phobos dir list -o put_access,get_access,delete_access

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

echo "*** TEST END ***"
# Uncomment if you want the db to persist after test
# trap - EXIT ERR
