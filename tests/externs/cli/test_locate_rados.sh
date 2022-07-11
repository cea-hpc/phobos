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
# Integration test for phobos_locate call
#

test_dir=$(dirname $(readlink -e $0))
medium_locker_bin="$test_dir/medium_locker"
. $test_dir/../../test_env.sh
. $test_dir/../../setup_db.sh
. $test_dir/../../test_launch_daemon.sh
. $test_dir/../../tape_drive.sh
. test_locate_common.sh

set -xe

function setup
{
    setup_tables
    invoke_daemon
    export rados_pools="pho_test_locate1 pho_test_locate2"
    sudo ceph osd pool create pho_test_locate1
    sudo ceph osd pool create pho_test_locate2

    echo "adding RADOS pools $pools"
    $phobos rados_pool add $rados_pools
    $phobos rados_pool format --fs RADOS --unlock $rados_pools
}

function cleanup
{
    waive_daemon
    drop_tables

    sudo ceph osd pool rm pho_test_locate1 pho_test_locate1 \
        --yes-i-really-really-mean-it
    sudo ceph osd pool rm pho_test_locate2 pho_test_locate2 \
        --yes-i-really-really-mean-it
    rm -rf /tmp/out*
}

trap cleanup EXIT
setup

# test locate on disk
export PHOBOS_STORE_default_family="rados_pool"
test_medium_locate rados_pool
test_locate_cli rados_pool
test_get_locate_cli rados_pool
