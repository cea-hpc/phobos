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

DRIVE="/dev/st0"

function setup
{
    if [ ! -w /dev/changer ]; then
        exit 0
    fi

    export PHOBOS_LRS_families="dir,tape"

    setup_tables
    invoke_daemon
}

function cleanup
{
    waive_daemon
    drop_tables
}

function delete_tape_drive
{
    $valg_phobos drive add --unlock ${DRIVE}
    output=$($valg_phobos drive list)
    $valg_phobos drive delete ${DRIVE} &&
        error "Should not be able to delete a daemon-locked drive"
    [ $output -ne $($valg_phobos drive list) ] &&
        error "Drive list should return the not removed drive"
    $valg_phobos drive lock --force ${DRIVE}
    # XXX: For now, we need to stop the daemon to force daemon locks to be
    #      released.
    waive_daemon
    invoke_daemon
    $valg_phobos drive delete ${DRIVE} ||
        error "Delete should have worked"
    [ -z $($valg_phobos drive list) ] ||
        error "Drive list should return empty list"

    return 0
}

trap cleanup EXIT
setup

delete_tape_drive
