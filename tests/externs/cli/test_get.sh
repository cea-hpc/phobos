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
# Integration test for get commands
#

set -xe

test_dir=$(dirname $(readlink -e $0))
. $test_dir/../../test_env.sh
. $test_dir/../../setup_db.sh
. $test_dir/../../test_launch_daemon.sh

function dir_setup
{
    dir="$(mktemp -d /tmp/test.pho.XXXX)"
    echo "adding directory $dir"
    $phobos dir add $dir
    $phobos dir format --fs posix --unlock $dir
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
    rm -rf $dir
    rm -f /tmp/out
}

function test_get
{
    $phobos put --family dir /etc/hosts oid1

    local uuid=$($phobos object list --output uuid oid1)

    $valg_phobos get oid1 /tmp/out || error "Get operation failed"
    rm /tmp/out

    $valg_phobos get --version 1 oid1 /tmp/out || error "Get operation failed"
    rm /tmp/out

    $valg_phobos get --uuid $uuid oid1 /tmp/out || error "Get operation failed"
    rm /tmp/out

    $valg_phobos get --version 1 --uuid $uuid oid1 /tmp/out \
        || error "Get operation failed"
    rm /tmp/out

    $phobos delete oid1

    $valg_phobos get oid1 /tmp/out \
        && error "Get operation should have failed" || true
    $valg_phobos get --version 1 oid1 /tmp/out || error "Get operation failed"
    rm /tmp/out

    $valg_phobos get --uuid $uuid oid1 /tmp/out || error "Get operation failed"
    rm /tmp/out
}

function test_errors
{
    $valg_phobos get oid2 /tmp/out \
        && error "Get operation should fail on invalid oid" || true

    $valg_phobos get --uuid fake_uuid oid1 /tmp/out \
        && error "Get operation should fail on invalid uuid" || true

    $phobos put --family dir /etc/hosts oid1
    $valg_phobos get --version 5 oid1 /tmp/out \
        && error "Get operation should fail on invalid version" || true

    $phobos delete oid1
    $valg_phobos get --version 5 oid1 /tmp/out \
        && error "Get operation should fail on invalid version" || true
}

trap cleanup EXIT
setup

test_get
test_errors
