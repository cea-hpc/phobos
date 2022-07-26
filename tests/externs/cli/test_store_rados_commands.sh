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

#
# Integration tests for object store commands with RADOS backend
#

test_dir=$(dirname $(readlink -e $0))
. $test_dir/../../test_env.sh
. $test_dir/../../setup_db.sh
. $test_dir/../../test_launch_daemon.sh

set -xe

function setup
{
    setup_tables # necessary for the daemon's initialization
    sudo ceph osd pool create pho_test
    invoke_daemon
    $phobos rados_pool add pho_test
    $phobos rados_pool format --unlock pho_test
    rm -f /tmp/rados_out
}

function cleanup
{
    waive_daemon
    sudo ceph osd pool rm pho_test pho_test --yes-i-really-really-mean-it
    drop_tables
}

function test_put_get_del
{
    $valg_phobos put --family rados_pool /etc/hosts oid ||
        error "Object should be put"

    local uuid=$($phobos object list --output uuid oid)

    $valg_phobos get oid /tmp/rados_out || error "Get operation failed"
    cmp --silent /etc/hosts /tmp/rados_out ||
        error "Input and output should have the same content"
    rm /tmp/rados_out

    $valg_phobos get --uuid $uuid oid /tmp/rados_out ||
        error "Get operation failed"
    cmp --silent /etc/hosts /tmp/rados_out ||
        error "Input and output should have the same content"
    rm /tmp/rados_out

    $valg_phobos get --version 1 --uuid $uuid oid /tmp/rados_out \
        || error "Get operation failed"
    cmp --silent /etc/hosts /tmp/rados_out ||
        error "Input and output should have the same content"
    rm /tmp/rados_out

    $valg_phobos delete oid || error "Delete operation failed"

    $valg_phobos get oid /tmp/rados_out \
        && error "Get operation should have failed" || true
}

function test_metadata_put_get
{
    $valg_phobos put --family rados_pool /etc/hosts oid --metadata m1=a,m2=b ||
        error "Object should be put"

    output=$($valg_phobos getmd oid || error "Getmd operation failed")

    if [[ ! "$output" == *"m1=a"* ]]; then
        error "'m1=a' should be in object oid's metadata"
    fi

    if [[ ! "$output" == *"m2=b"* ]]; then
        error "'m2=b' should be in object oid's metadata"
    fi

    $valg_phobos put --family rados_pool /etc/hosts oid --overwrite \
            --metadata m1=c || error "Object should be put"

    output=$($valg_phobos getmd oid || error "Getmd operation failed")

    if [[ ! "$output" == *"m1=c"* ]]; then
        error "'m1=c' should be in object oid's metadata"
    fi

    if [[ "$output" == *"m2=b"* ]]; then
        error "'m2=b' shoud not be in object oid's metadata"
    fi

    $valg_phobos delete oid || error "Delete operation failed"
}

function test_put_overwrite
{
    $valg_phobos put --family rados_pool /etc/hosts oid --metadata m1=a,m2=b ||
        error "Object should be put"

    echo test_content > /tmp/testin
    $valg_phobos put --family rados_pool /tmp/testin oid --overwrite \
        --metadata m1=a,m2=b || error "Object should be put"

    $valg_phobos get --version 1 oid /tmp/out || error "Get operation failed"
    cmp --silent /etc/hosts /tmp/out ||
        error "Input and output should have the same content"
    rm /tmp/out

    $valg_phobos get --version 2 oid /tmp/out || error "Get operation failed"
    cmp --silent /tmp/testin /tmp/out ||
        error "Input and output should have the same content"
    rm /tmp/out
    rm /tmp/testin

    $valg_phobos delete oid || error "Delete operation failed"
}

trap cleanup EXIT
setup

test_put_get_del
test_metadata_put_get
test_put_overwrite
