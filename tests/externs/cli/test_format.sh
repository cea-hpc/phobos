#!/bin/bash
# -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
# vim:expandtab:shiftwidth=4:tabstop=4:

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

# set python and phobos environment
test_dir=$(dirname $(readlink -e $0))
. $test_dir/../../test_env.sh
. $test_dir/../../setup_db.sh
. $test_dir/../../test_launch_daemon.sh
. $test_dir/../../tape_drive.sh

function setup
{
    setup_tables
    invoke_daemon
}

function cleanup
{
    waive_daemon
    drop_tables
}

function execute_test_double_format
{
    local dir_or_tape=$1
    local medium=$2

    if [[ ${dir_or_tape} == "dir" ]]; then
        local phobos_format_options="dir format --fs posix --unlock"
    else
        local phobos_format_options="tape format --unlock"
    fi

    set +e
    $valg_phobos ${phobos_format_options} ${medium} &
    local format_pid_1=$!
    $valg_phobos ${phobos_format_options} ${medium}
    local format_status_2=$?
    wait ${format_pid_1}
    local format_status_1=$?
    set -e
    if (( ! ( ${format_status_1} == 0 ^ ${format_status_2} == 0 ) )); then
        error "One of two concurrent ${dir_or_tape} formats should" \
              "have succeeded and the other should have failed, got" \
              " ${format_status_1} and ${format_status_2}."
    fi
}

function test_double_format
{
    trap cleanup EXIT
    setup

    # Trying to concurrently format the same dir
    local dir=$(mktemp -d /tmp/test_double_format.XXXX)
    $valg_phobos dir add ${dir}
    set +e
    execute_test_double_format dir ${dir}

    if [[ -w /dev/changer ]]; then
        # Trying to concurrently format the same tape
        local tape=$(get_tapes L6 1)
        $valg_phobos tape add --type lto6 ${tape}
        local drive=$(get_lto_drives 6 1)
        $valg_phobos drive add --unlock ${drive}
        execute_test_double_format tape ${tape}
    fi

    cleanup
    trap EXIT
}

test_double_format
