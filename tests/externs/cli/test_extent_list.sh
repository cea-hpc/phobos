#!/bin/bash
#
#  All rights reserved (c) 2014-2022 CEA/DAM.
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

test_dir=$(dirname $(readlink -e $0))
. $test_dir/../../test_env.sh
. $test_dir/../../setup_db.sh
. $test_dir/../../test_launch_daemon.sh

set -xe

function dir_setup
{
    dir1="$(mktemp -d /tmp/test.pho.XXXX)"
    dir2="$(mktemp -d /tmp/test.pho.XXXX)"
    $phobos dir add -T first_dir $dir1
    $phobos dir format --fs posix --unlock $dir1
    $phobos dir add -T second_dir $dir2
    $phobos dir format --fs posix --unlock $dir2
}

function setup
{
    setup_tables
    invoke_daemon

    dir_setup
}

function cleanup
{
    waive_daemon
    drop_tables
    rm -rf $dir1 $dir2
}

function list_error
{
    echo "An error occured while listing objects: "
    echo "Matching: $1"
    echo "Returned: $2"
    echo "Expected: $3"
}

function test_extent_list_degroup
{
    $phobos put --family dir --lyt-params repl_count=2 /etc/hosts degroup-oid ||
        error "Object should be put"

    cmd_list="$valg_phobos extent list --degroup -o address,media_name"

    # First check when specifying the medium name
    res1=$($cmd_list --name $dir1 degroup-oid | grep $dir1)
    res2=$($cmd_list --name $dir2 degroup-oid | grep $dir2)

    if [ "$res1" == "$res2" ]; then
        error "Outputs should be different"
    fi

    # Then check without specifying the medium name
    if [ "$res1" != "$($cmd_list degroup-oid | grep $dir1)" ]; then
        error "Outputs should be identical"
    fi

    if [ "$res2" != "$($cmd_list degroup-oid | grep $dir2)" ]; then
        error "Outputs should be identical"
    fi
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

        res=$($valg_phobos extent list $match)

        if [ "$res" != "$exp" ]; then
           list_error "$match" "$res" "$exp"
        fi
    done

    export PHOBOS_LRS_server_socket="regular_file"
    touch $PHOBOS_LRS_server_socket
    $valg_phobos extent list ||
        error "Extent list should have succeeded"
    unset PHOBOS_LRS_server_socket

    return 0
}

trap cleanup EXIT
setup

test_extent_list_degroup
test_extent_list_pattern
