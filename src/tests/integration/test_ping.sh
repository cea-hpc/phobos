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

#
# Integration test for ping feature
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

function cleanup
{
    waive_daemon
    drop_tables
}

function test_ping
{
    invoke_daemon
    $phobos ping || error "Ping should be successful"
    waive_daemon
    $phobos ping && error "Ping should fail"
    return 0
}

trap cleanup ERR EXIT
setup_tables # necessary for the daemon's initialization
test_ping
