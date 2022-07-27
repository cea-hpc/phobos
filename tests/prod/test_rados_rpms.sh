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
# Test for RADOS RPMs
#

set -ex

test_dir=$(dirname $(readlink -e $0))

start_phobosd="systemctl start phobosd"
stop_phobosd="systemctl stop phobosd"

start_phobosdb="phobos_db setup_tables"
stop_phobosdb="phobos_db drop_tables"
rados_rpm=$test_dir/../../rpms/RPMS/x86_64/phobos-rados-*
rpm_install_rados="yum install -y $rados_rpm"
remove_rados_adapters="yum remove -y phobos-rados-adapters"

function setup_test {
    yum install -y $test_dir/../../rpms/RPMS/x86_64/phobos-1*
    cp $test_dir/../phobos.conf /etc/phobos.conf
    # Make sure Libraries can be found
    export LD_LIBRARY_PATH="LD_LIBRARY_PATH:/usr/lib64:/usr/lib64/phobos"
    $start_phobosdb
    ceph osd pool create pho_test
}

function cleanup_test {
    ceph osd pool rm pho_test pho_test --yes-i-really-really-mean-it
    $stop_phobosdb
    yum remove -y phobos
    rm -f /etc/phobos.conf
    rm -f /tmp/out
}

function test_without_rados_rpm {
    $start_phobosd
    $remove_rados_adapters
    phobos rados_pool add pho_test &&
            error "Add operation should have failed" || true
    $stop_phobosd
}

function test_with_rados_rpm {
    $start_phobosd
    $rpm_install_rados
    phobos rados_pool add pho_test || error "Add operation failed"
    phobos rados_pool format --unlock pho_test ||
            error "Format operation failed"
    phobos put --family rados_pool /etc/hosts oid ||
            error "Put operation failed"
    phobos get oid /tmp/out || error "Get operation failed"

    diff /tmp/out /etc/hosts || error "Files should have the same content"
    $stop_phobosd
}

trap cleanup_test EXIT

setup_test
test_without_rados_rpm
test_with_rados_rpm
