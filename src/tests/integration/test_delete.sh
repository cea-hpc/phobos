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
# Integration test for deletion feature
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
    export dirs="
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

function test_delete
{
    $phobos put --family dir /etc/hosts oid1 ||
        error "Object should be put"
    $phobos object delete oid1 ||
        error "Object should be deleted"
    $phobos get oid1 test_tmp &&
        error "Object should not be got"

    return 0
}

setup_tables
invoke_daemon
trap cleanup EXIT
dir_setup
test_delete
