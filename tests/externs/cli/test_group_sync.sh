#!/bin/bash

#
#  All rights reserved (c) 2014-2022 CEA/DAM.
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
# Integration test for grouping sync
#

set -xe

test_dir=$(dirname $(readlink -e $0))
. $test_dir/../../test_env.sh
. $test_dir/../../setup_db.sh
. $test_dir/../../test_launch_daemon.sh
. $test_dir/../../tape_drive.sh

function dir_setup
{
    dir=$(mktemp -d /tmp/test.pho.XXXX)
    $phobos dir add ${dir}
    $phobos dir format --fs posix --unlock ${dir}
}

function tape_setup
{
    $phobos drive add --unlock /dev/st0
    $phobos tape add -t lto5 P00000L5
    $phobos tape format --unlock P00000L5
}

function setup
{
    setup_tables
    invoke_daemon
    dir_setup
    if [[ -w /dev/changer ]]; then
        tape_setup
    fi
}

function cleanup
{
    waive_daemon
    rm -rf $dir
    drop_tables

    if [[ -w /dev/changer ]]; then
        drain_all_drives
    fi
}

trap cleanup EXIT

WAIT_PUT_S=2

function test_sync_no_group
{
    local families=$1

    export PHOBOS_LRS_sync_time_threshold="dir=0,tape=0"
    export PHOBOS_LRS_sync_nb_req_threshold="dir=1,tape=1"
    export PHOBOS_LRS_sync_written_size_threshold="dir=1,tape=1"
    setup

    for family in ${families}; do
        ${phobos} put --family ${family} /etc/hosts \
                                         test_sync_no_group_${family} &
        pid=$!
        sleep ${WAIT_PUT_S}
        if (ps --pid $pid); then
            error "Without any grouped sync ${family} put should be ended."
        fi
    done

    cleanup
}

function test_sync_group_nb_req
{
    local families=$1

    export PHOBOS_LRS_sync_time_threshold="dir=60000,tape=60000"
    export PHOBOS_LRS_sync_nb_req_threshold="dir=2,tape=2"
    export PHOBOS_LRS_sync_written_size_threshold=\
"dir=1000000000,tape=1000000000"
    setup

    for family in ${families}; do
        ${phobos} put --family ${family} /etc/hosts \
                                         test_sync_group_nb_req_${family} &
        pid=$!
        sleep ${WAIT_PUT_S}
        if (! ps --pid $pid); then
            error "With a sync nb req of 2, first ${family} put should be" \
                  "stuck."
        fi

        ${phobos} put --family ${family} /etc/hosts \
                                         test_sync_group_nb_req_${family}_bis
        if (ps --pid $pid); then
            error "With a sync nb req of 2, first background ${family} put" \
                  "should be ended when second one is ended."
        fi
    done

    cleanup
}

function test_sync_group_written_size
{
    local families=$1

    export PHOBOS_LRS_sync_time_threshold="dir=60000,tape=60000"
    export PHOBOS_LRS_sync_nb_req_threshold="dir=1024,tape=1024"
    local file_byte_size=$((1024 * 2))
    local file=$(mktemp /tmp/test.pho.XXXX)
    dd if=/dev/random of=${file} count=${file_byte_size} bs=1
    local written_size_threshold=$((file_byte_size / 1024 + 1))
    export PHOBOS_LRS_sync_written_size_threshold=\
"dir=${written_size_threshold},tape=${written_size_threshold}"
    setup

    for family in ${families}; do
        ${phobos} put --family ${family} ${file} \
                                     test_sync_group_written_size_${family} &
        pid=$!
        sleep ${WAIT_PUT_S}
        if (! ps --pid $pid); then
            error "With a written_size of one kiB more than the object size," \
                  "first ${family} put should be stuck."
        fi

        ${phobos} put --family ${family} ${file} \
                                     test_sync_group_written_size_${family}_bis
        if (ps --pid $pid); then
            error "With a written_size of one kiB more than the object size," \
                  "first background ${family} put should be ended when" \
                  "second one is ended."
        fi
    done

    rm ${file}

    cleanup
}

function test_sync_group_time
{
    local families=$1

    local time_threshold=$((WAIT_PUT_S * 1000 + 2000))
    export PHOBOS_LRS_sync_time_threshold=\
"dir=${time_threshold},tape=${time_threshold}"
    export PHOBOS_LRS_sync_nb_req_threshold="dir=1024,tape=1024"
    export PHOBOS_LRS_sync_written_size_threshold=\
"dir=1000000000,tape=1000000000"
    setup

    for family in ${families}; do
        ${phobos} put --family ${family} /etc/hosts \
                                         test_sync_group_time_${family} &
        pid=$!
        sleep ${WAIT_PUT_S}
        if (! ps --pid $pid); then
            error "With a time 2s more than previous sleep, first ${family}" \
                  "put should be stuck."
        fi

        ${phobos} put --family ${family} /etc/hosts \
                                         test_sync_group_time_${family}_bis
        if (ps --pid $pid); then
            error "With a time 2s more than previous sleep, first background" \
                  "${family} put should be ended when second one is ended."
        fi
    done

    cleanup
}

if [[ -w /dev/changer ]]; then
    families="dir tape"
else
    families="dir"
fi

test_sync_no_group ${families}
test_sync_group_nb_req ${families}
test_sync_group_written_size ${families}
test_sync_group_time ${families}

trap - EXIT
