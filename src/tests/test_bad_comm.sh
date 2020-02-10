#!/bin/sh -e
# -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
# vim:expandtab:shiftwidth=4:tabstop=4:

#
#  All rights reserved (c) 2014-2020 CEA/DAM.
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

function error {
    echo "$*" >&2
    exit 1
}

function dir_setup {
    export dir="/tmp/test.pho.1"
    export dir_not_formatted="/tmp/test.pho.3"
    mkdir -p $dir $dir_not_formatted

    $phobos dir add $dir $dir_not_formatted
    $phobos dir format --fs posix --unlock $dir
}

function cleanup {
    waive_daemon
    rm -rf $dir $dir_not_formatted
    drop_tables
}

test_bin_dir=$(dirname $(readlink -e $0))

. $test_bin_dir/test_env.sh
. $test_bin_dir/setup_db.sh
. $test_bin_dir/test_launch_daemon.sh
export PHOBOS_LRS_families="dir"

drop_tables
setup_tables

invoke_daemon
trap cleanup EXIT
dir_setup
$LOG_COMPILER $LOG_FLAGS ./test_bad_comm
kill -0 $PID_DAEMON &> /dev/null || error "phobosd exited prematurely"
