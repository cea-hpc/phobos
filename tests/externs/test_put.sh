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
# Integration test for put commands
#

set -xe

LOG_VALG="$LOG_COMPILER $LOG_FLAGS"

test_dir=$(dirname $(readlink -e $0))
. $test_dir/../test_env.sh
. $test_dir/../setup_db.sh
. $test_dir/../test_launch_daemon.sh

function dir_setup
{
    export dirs="
        $(mktemp -d /tmp/test.pho.XXXX)
        $(mktemp -d /tmp/test.pho.XXXX)
    "
    echo "adding directories $dirs"
    $phobos dir add $dirs
    $phobos dir format --fs posix --unlock $dirs
}

function cleanup
{
    waive_daemon
    drop_tables
    rm -rf $dirs
}

function test_extent_path
{
    $phobos put --family dir /etc/hosts oid1 ||
        error "Object should be put"
    $phobos put --family dir --layout raid1 /etc/hosts oid2 ||
        error "Object should be put"

    # some characters are removed to get a clean extent
    # "['blablabla', 'blobloblo']" -> "blablabla" and "blobloblo"
    addr_ext1=$($phobos extent list --output address oid1 | \
                tr -d ' ' | tr -d \' | tr -d [ | tr -d ])
    addr_ext2a=$($phobos extent list --output address oid2 | cut -d, -f1 | \
                tr -d ' ' | tr -d \' | tr -d [ | tr -d ])
    addr_ext2b=$($phobos extent list --output address oid2 | cut -d, -f2 | \
                tr -d ' ' | tr -d \' | tr -d [ | tr -d ])

    uuid_ext1=$($phobos object list --output uuid oid1)
    uuid_ext2=$($phobos object list --output uuid oid2)
    ver_ext1=$($phobos object list --output version oid1)
    ver_ext2=$($phobos object list --output version oid2)

    [[ "$addr_ext1" =~ "/oid1." ]] ||
        error "oid1 extent path should contain object id"
    [[ "$addr_ext2a" =~ "/oid2." ]] ||
        error "oid2 extent path (pt1) should contain object id"
    [[ "$addr_ext2b" =~ "/oid2." ]] ||
        error "oid2 extent path (pt2) should contain object id"
    [[ "$addr_ext1" =~ ".$uuid_ext1"$ ]] ||
        error "oid1 extent path should contain object uuid"
    [[ "$addr_ext2a" =~ ".$uuid_ext2"$ ]] ||
        error "oid2 extent path (pt1) should contain object uuid"
    [[ "$addr_ext2b" =~ ".$uuid_ext2"$ ]] ||
        error "oid2 extent path (pt2) should contain object uuid"
    [[ "$addr_ext1" =~ ".${ver_ext1}." ]] ||
        error "oid1 extent path should contain object version"
    [[ "$addr_ext2a" =~ ".${ver_ext2}." ]] ||
        error "oid2 extent path (pt1) should contain object version"
    [[ "$addr_ext2b" =~ ".${ver_ext2}." ]] ||
        error "oid2 extent path (pt2) should contain object version"
}

setup_tables
invoke_daemon
trap cleanup EXIT
dir_setup
test_extent_path
