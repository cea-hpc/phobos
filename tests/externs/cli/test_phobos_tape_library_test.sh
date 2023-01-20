#!/bin/bash

#
#  All rights reserved (c) 2014-2023 CEA/DAM.
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
# Integration test for put commands
#

test_dir=$(dirname $(readlink -e $0))
. $test_dir/../../test_env.sh
. $test_dir/../../setup_db.sh
. $test_dir/../../test_launch_daemon.sh
. $test_dir/../../tape_drive.sh

set -xe

drives_set="$(get_lto_drives 5 3 | nodeset -f)"
tapes_set="$(get_tapes L5 6)"

tape_lib_script="${tape_lib_certif_dir}/phobos_tape_library_test.bash"

function setup
{
    setup_tables
    invoke_daemon
    drain_all_drives

    for drive in $(nodeset -e "${drives_set}"); do
        $phobos drive add --unlock "${drive}" ||
            error "Drive ${drive} should have been added"
    done

    for tape in $(nodeset -e "${tapes_set}"); do
        $phobos tape add -t lto5 "${tape}" ||
            error "Tape ${tape} should have been added"
    done
}

function cleanup
{
    waive_daemon
    drop_tables
    drain_all_drives
}

# This tape library certification test needs an available tape library.
if [[ -w /dev/changer ]]; then
    trap cleanup EXIT
    setup

    # use the phobos client binary of the test tree
    export phobos

    ${tape_lib_script} "${drives_set}" "${tapes_set}" ||
        error "Concurrent load/unload fails for '${drives_set}'" \
              "and '${tapes_set}'"
fi
