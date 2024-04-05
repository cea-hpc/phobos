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

if [[ ! -w /dev/changer ]]; then
    exit 77 # special value to mark test as 'skipped'
fi

test_dir=$(dirname $(readlink -e $0))
. $test_dir/../../test_env.sh
. $test_dir/../../setup_db.sh
. $test_dir/../../tape_drive.sh
. $test_dir/../../test_launch_daemon.sh
lrs_simple_client="$test_dir/lrs_simple_client"

function test_drive_status_no_daemon()
{
    $phobos drive status &&
        error "LRS is required for drive status" ||
        true
}

function setup()
{
    setup_tables
    invoke_daemons
}

function cleanup()
{
    waive_daemons
    drop_tables
    drain_all_drives
}

function st2sg()
{
    local stdev="$1"

    lsscsi -g | grep "$stdev" | tr -s ' ' ' ' | cut -d' ' -f7
}

function test_drive_status()
{
    local drives=($(get_drives 2))

    $phobos drive add ${drives[@]}
    $phobos drive unlock ${drives[@]}

    for i in ${!drives[@]}; do
        $phobos drive status | grep $(st2sg "${drives[$i]}") ||
            error "$(st2sg "${drives[$i]}") not found"
    done
}

function test_drive_status_with_ongoing_io()
{
    local tape=$(get_tapes L6 1)
    local drive=$(get_lto_drives 6 1)

    $phobos tape add --type lto6 "$tape"
    $phobos drive add "$drive"
    $phobos drive unlock "$drive"
    $phobos tape format --unlock "$tape"

    local release_medium_name=$($lrs_simple_client put tape)

    $phobos drive status |
        grep True |
        grep "/mnt/phobos-$(basename $(st2sg "$drive"))" |
        grep RWF |
        grep "$tape"

    # send release request
    $lrs_simple_client release 1 $release_medium_name tape
}

trap cleanup EXIT

test_drive_status_no_daemon

drain_all_drives
setup

test_drive_status

cleanup
setup

test_drive_status_with_ongoing_io
