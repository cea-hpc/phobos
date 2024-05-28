#!/bin/bash

#
#  All rights reserved (c) 2014-2024 CEA/DAM.
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

test_dir=$(dirname $(readlink -e $0))
. $test_dir/../../test_env.sh
. $test_dir/../../setup_db.sh
. $test_dir/../../test_launch_daemon.sh
. $test_dir/../../tape_drive.sh

set -xe

if [[ ! -w /dev/changer ]]; then
    echo "Cannot access library: test skipped"
    exit 77
fi

function setup
{
    setup_tables
    invoke_tlc
}

function cleanup
{
    waive_tlc
    drop_tables
}

trap cleanup EXIT
setup

function swap_back_lib_in_cfg
{
    sed -i "s/\/blob/\/dev\/changer/g" $PHOBOS_CFG_FILE
}

function test_lib_scan
{
    dev_changer_scan=$($phobos lib scan /dev/changer)
    echo "$dev_changer_scan" | grep "arm"
    echo "$dev_changer_scan" | grep "slot"
    echo "$dev_changer_scan" | grep "import/export"
    echo "$dev_changer_scan" | grep "drive"
}

test_lib_scan
