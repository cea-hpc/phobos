#!/bin/bash
#
#  All rights reserved (c) 2014-2021 CEA/DAM.
#
#  This file is part of Phobos.
#
#  Phobos is free software: you can redistribute it and/or modify it under
#  the terms of the GNU Lesser General Public License as published by
#  the Free Software Foundation, either version 2.1 of the Licence, or
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
# Integration test for extent list commands
#

set -xe

LOG_VALG="$LOG_COMPILER $LOG_FLAGS"

test_dir=$(dirname $(readlink -e $0))
. $test_dir/../test_env.sh
. $test_dir/../setup_db.sh
. $test_dir/../test_launch_daemon.sh

function error
{
    echo "$*"
    exit 1
}

function dir_setup
{
    export dir1="$(mktemp -d /tmp/test.pho.XXXX)"
    export dir2="$(mktemp -d /tmp/test.pho.XXXX)"
    $phobos dir add -T first_dir $dir1
    $phobos dir format --fs posix --unlock $dir1
    $phobos dir add -T second_dir $dir2
    $phobos dir format --fs posix --unlock $dir2
}

function rm_folders
{
    rm -rf /tmp/test.pho.d1 /tmp/test.pho.d2
}

function cleanup
{
    waive_daemon
    drop_tables
    rm_folders
}

function list_error
{
    echo "An error occured while listing objects: "
    echo "Matching: $1"
    echo "Returned: $2"
    echo "Expected: $3"
}

function test_extent_list_pattern
{
    $phobos put --family dir -T first_dir /etc/hosts oid1 ||
        error "Object should be put"
    $phobos put --family dir -T second_dir /etc/hosts oid2 ||
        error "Object should be put"
    $phobos put --family dir -T first_dir /etc/hosts blob ||
        error "Object should be put"
    $phobos put --family dir -T second_dir /etc/hosts lorem ||
        error "Object should be put"

    contents=("oid1;oid1"
              "--pattern oid;oid1\noid2"
              "--pattern 2 blob;oid2\nblob"
              "--pattern --name $dir2 oid;oid2"
              "--name $dir1 blob;blob"
              ";oid1\noid2\nblob\nlorem"
              "--pattern OID1;"
              "--pattern --name $dir1 o;oid1\nblob"
              "--pattern --name $dir2 b m;lorem"
              "oid1 oid2;oid1\noid2"
              "--name $dir1 lorem;")

    for id in "${contents[@]}"
    do
        match=$(echo "$id" | cut -d';' -f1)
        exp=$(echo -e $(echo "$id" | cut -d';' -f2))

        res=$($phobos extent list $match)

        if [ "$res" != "$exp" ]; then
           list_error "$match" "$res" "$exp"
        fi
    done

    return 0
}

rm_folders
setup_tables
invoke_daemon
trap cleanup EXIT
dir_setup
test_extent_list_pattern
