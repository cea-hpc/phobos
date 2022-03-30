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

set -xe

test_dir=$(dirname $(readlink -e $0))
. $test_dir/../../test_env.sh
. $test_dir/../../setup_db.sh
. $test_dir/../../test_launch_daemon.sh
. $test_dir/../../tape_drive.sh

if [[ ! -w /dev/changer ]]; then
    echo "Cannot access library: test skipped"
    exit 77
fi

function swap_back_lib_in_cfg
{
    sed -i "s/\/blob/\/dev\/changer/g" $PHOBOS_CFG_FILE
}

function test_lib_scan
{
    local no_dir="/blob"
    $phobos lib scan "$no_dir" &&
        error "Should have failed, '$no_dir' doesn't exists"

    dev_changer_scan=$($phobos lib scan /dev/changer)
    echo "$dev_changer_scan" | grep "arm"
    echo "$dev_changer_scan" | grep "slot"
    echo "$dev_changer_scan" | grep "import/export"
    echo "$dev_changer_scan" | grep "drive"

    old_cfg=$(cat $PHOBOS_CFG_FILE)
    if grep "lib_device" $PHOBOS_CFG_FILE; then
        sed -i "s/$old_lib_device/\/blob/g" $PHOBOS_CFG_FILE
    else
        sed -i "s/\[lrs\]/\[lrs\]\nlib_device = \/blob/g" $PHOBOS_CFG_FILE
    fi

    trap "echo '$old_cfg' > $PHOBOS_CFG_FILE" EXIT

    $phobos lib scan &&
        error "Should have failed, '/blob' specified in cfg file doesn't exist"

    sed -i "s/\/blob/\/dev\/changer/g" $PHOBOS_CFG_FILE

    no_arg_scan=$($phobos lib scan)
    if [[ "$dev_changer_scan" != "$no_arg_scan" ]]; then
        error "Lib scan with no arg returned different result than lib scan " \
              "with arg '/dev/changer'"
    fi

    sed -i "/lib_device/d" $PHOBOS_CFG_FILE

    $phobos lib scan &&
        error "Should have failed, no 'lib_device' specified in cfg file"

    echo "$old_cfg" > $PHOBOS_CFG_FILE
    trap - EXIT
}

test_lib_scan
