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
    invoke_tlc_bis
}

function cleanup
{
    waive_tlc
    waive_tlc_bis
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
    default_lib_scan=$($phobos lib scan)
    echo "$default_lib_scan" | grep "arm"
    echo "$default_lib_scan" | grep "slot"
    echo "$default_lib_scan" | grep "import/export"
    echo "$default_lib_scan" | grep "drive"
    default_lib_scan_nb_drive=$(echo "${default_lib_scan}" | grep -c drive)
    if (( 8 != default_lib_scan_nb_drive )); then
        error "Lib scan must return 8 drives for default legacy library and" \
              "not ${default_lib_scan_nb_drive}"
    fi

    legacy_scan=$($phobos lib scan legacy)
    legacy_scan_nb_drive=$(echo "${legacy_scan}" | grep -c drive)
    if (( 8 != legacy_scan_nb_drive )); then
        error "Lib scan must return 8 drives for legacy and not" \
              "${legacy_scan_nb_drive}"
    fi

    library_bis_scan=$($phobos lib scan library_bis)
    library_bis_scan_nb_drive=$(echo "${library_bis_scan}" | grep -c drive)
    if (( 1 != library_bis_scan_nb_drive )); then
        error "Lib scan must return 1 drives for library bis and not" \
              "${library_bis_scan_nb_drive}"
    fi
}

test_lib_scan
