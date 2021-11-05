#!/bin/bash

#
#  All rights reserved (c) 2014-2021 CEA/DAM.
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
# Integration test for phobos_locate call
#

set -xe

test_dir=$(dirname $(readlink -e $0))
medium_locker_bin="$test_dir/medium_locker"
. $test_dir/../../test_env.sh
. $test_dir/../../setup_db.sh
. $test_dir/../../test_launch_daemon.sh
. $test_dir/../../tape_drive.sh

function dir_setup
{
    export dirs="
        $(mktemp -d /tmp/test.pho.XXXX)
        $(mktemp -d /tmp/test.pho.XXXX)
    "
    echo "adding directories $dirs"
    $phobos dir add $dirs
    $phobos dir format --fs posix --unlock $dirs
}

function setup
{
    setup_tables
    invoke_daemon
    dir_setup
}

function tape_setup
{
    local N_TAPES=2
    local N_DRIVES=8
    local LTO5_TAGS=$TAGS,lto5
    local LTO6_TAGS=$TAGS,lto6

    # get LTO5 tapes
    local lto5_tapes="$(get_tapes L5 $N_TAPES)"
    echo "adding tapes $lto5_tapes with tags $LTO5_TAGS..."
    $phobos tape add --tags $LTO5_TAGS --type lto5 "$lto5_tapes"

    # get LTO6 tapes
    local lto6_tapes="$(get_tapes L6 $N_TAPES)"
    echo "adding tapes $lto6_tapes with tags $LTO6_TAGS..."
    $phobos tape add --tags $LTO6_TAGS --type lto6 "$lto6_tapes"

    # set tapes
    export tapes="$lto5_tapes,$lto6_tapes"

    # comparing with original list
    diff <($phobos tape list | sort | xargs) \
         <(echo "$tapes" | nodeset -e | sort)

    # show a tape info
    local tp1=$(echo $tapes | nodeset -e | awk '{print $1}')
    $phobos tape list --output all $tp1

    # unlock all tapes
    for t in $tapes; do
        $phobos tape unlock $t
    done

    # get drives
    local drives=$(get_drives $N_DRIVES)
    echo "adding drives $drives..."
    $phobos drive add $drives

    # show a drive info
    local dr1=$(echo $drives | awk '{print $1}')
    echo "$dr1"
    # check drive status
    $phobos drive list --output adm_status $dr1 --format=csv |
        grep "^locked" || error "Drive should be added with locked state"

    # unlock all drives
    for d in $($phobos drive list); do
        echo $d
        $phobos drive unlock $d
    done

    # format lto5 tapes
    $phobos --verbose tape format $lto5_tapes --unlock
    # format lto6 tapes
    $phobos --verbose tape format $lto6_tapes --unlock
}

function cleanup
{
    waive_daemon
    drop_tables
    drain_all_drives
    rm -rf $dirs
    rm -rf /tmp/out*
}

function test_locate_cli
{
    local obj="object_to_locate"
    local dir_or_tape=$1
    local pid="12345"

    # test error on locating an unknown object
    $valg_phobos locate unknown_object &&
        error "Locating an unknown object must fail"

    # put a new object from localhost and locate it on localhost
    $phobos put /etc/hosts $obj || error "Error while putting $obj"
    local obj_uuid=$($phobos object list --output uuid $obj) ||
        error "Failed to retrieve uuid of $obj"
    $phobos put --overwrite /etc/hosts $obj ||
        error "Error while overwriting $obj"

    locate_hostname=$($valg_phobos locate $obj)
    self_hostname=$(uname -n)
    if [ "$locate_hostname" != "$self_hostname" ]; then
        error "Cli locate returned $locate_hostname instead of $self_hostname"
    fi

    $phobos delete $obj || error "$obj should be deleted"
    $valg_phobos locate $obj &&
        error "Locating a deleted object without uuid or version must fail"

    locate_hostname=$($valg_phobos locate --uuid $obj_uuid $obj)
    if [ "$locate_hostname" != "$self_hostname" ]; then
        error "Cli locate with uuid returned $locate_hostname instead of " \
              "$self_hostname"
    fi

    locate_hostname=$($valg_phobos locate --version 1 $obj)
    if [ "$locate_hostname" != "$self_hostname" ]; then
        error "Cli locate with version returned $locate_hostname " \
              "instead of $self_hostname"
    fi

    locate_hostname=$($valg_phobos locate --uuid $obj_uuid --version 1 $obj)
    if [ "$locate_hostname" != "$self_hostname" ]; then
        error "Cli locate with uuid and version returned $locate_hostname " \
              "instead of $self_hostname"
    fi
}

function test_medium_locate
{
    local dir_or_tape=$1
    local media_name="${dir_or_tape}s"
    local self_hostname=$(uname -n)
    local fake_hostname="fake_hostname"
    local locate_hostname=""
    local was_locked="false"

    # test error while locating an unknown medium
    $valg_phobos $dir_or_tape locate unknown_medium &&
        error "Locating an unknown $dir_or_tape must fail"

    # set medium to test
    local medium=$(echo ${!media_name} | nodeset -e | awk '{print $1;}')

    # test error on an admin locked medium
    $phobos $dir_or_tape lock $medium ||
        error "Error while locking before locate"
    $valg_phobos $dir_or_tape locate $medium &&
        error "Locating an admin locked $dir_or_tape must fail"
    $phobos $dir_or_tape unlock $medium ||
        error "Error while unlocking lock after locate"

    # locate an unlocked medium
    # check if medium is lock
    locker=$($phobos $dir_or_tape list -o lock_hostname $medium)
    if [ "$locker" == "$self_hostname" ]; then
        was_locked="true"
        # we artificially force an unlock
        $medium_locker_bin unlock $dir_or_tape $medium $self_hostname   \
                $PID_DAEMON ||
            error "Error while unlocking $dir_or_tape before locate"
    fi

    locate_hostname=$($valg_phobos $dir_or_tape locate $medium)
    if [ "$locate_hostname" != "" ]; then
        error "$dir_or_tape locate returned $locate_hostname instead of " \
              "an empty string on an unlocked medium"
    fi

    # locate on a locked medium
    $medium_locker_bin lock $dir_or_tape $medium $fake_hostname $PID_DAEMON ||
        error "Error while locking medium before locate"

    locate_hostname=$($valg_phobos $dir_or_tape locate $medium)
    if [ "$locate_hostname" != "$fake_hostname" ]; then
        error "$dir_or_tape locate returned $locate_hostname instead of " \
              "$fake_hostname on a locked medium"
    fi

    # We remove the lock
    $medium_locker_bin unlock $dir_or_tape $medium $fake_hostname $PID_DAEMON ||
        error "Error while unlocking medium after locate"

    # we artificially restore the lock
    if [ "$was_locked" == "true" ]; then
        $medium_locker_bin lock $dir_or_tape $medium $self_hostname \
                $PID_DAEMON ||
            error "Error while restore $dir_or_tape lock after locate"
    fi
}

function test_get_locate_cli
{
    local oid="oid_get_locate"
    local dir_or_tape=$1
    local pid="12345"
    local media_name="${dir_or_tape}s"
    local media=$(echo ${!media_name} | nodeset -e)
    declare -A was_locked
    for med in ${media}; do
        was_locked[${med}]="false"
    done

    $phobos put /etc/hosts $oid ||
        error "Error while putting $oid"

    $phobos get --best-host $oid /tmp/out1 \
        || error "Get operation should have succeeded"

    # PUT and GET let existing locks, we artificaly clear it
    for med in ${media}; do
        locker=$($phobos $dir_or_tape list -o lock_hostname $med)
        if [ "$locker" == "$self_hostname" ]; then
            was_locked[${med}]="true"
            $medium_locker_bin unlock $dir_or_tape $med $self_hostname  \
                    $PID_DAEMON ||
                error "Error unlocking $dir_or_tape before get --best-host"
        fi
    done

    # We force an "exotic" locker hostname : "blob"
    $medium_locker_bin lock $dir_or_tape all blob $pid ||
        error "Error while locking media before get --best-host"

    get_locate_output=$($valg_phobos get --best-host $oid /tmp/out2 || true)
    local output="Current host is not the best to get this object, try on"
    output="$output this other node, '$oid' : 'blob'"
    if [ "$get_locate_output" != "$output" ]; then
        error "Object should have been located on node 'blob'"
    fi

    $medium_locker_bin unlock $dir_or_tape all blob $pid ||
        error "Error while unlocking medium1 after get --best-host"

    # we artificially restore the locks
    for med in ${media}; do
        if [ "${was_locked[$med]}" == "true" ]; then
            $medium_locker_bin lock $dir_or_tape $med $self_hostname \
                    $PID_DAEMON ||
                error "Error while restoring $dir_or_tape lock after locate"
        fi
    done
}

trap cleanup EXIT
setup

# test locate on disk
export PHOBOS_STORE_default_family="dir"
test_medium_locate dir
test_locate_cli dir
test_get_locate_cli dir

if [[ -w /dev/changer ]]; then
    cleanup
    echo "Tape test mode"
    setup_tables
    invoke_daemon
    export PHOBOS_STORE_default_family="tape"
    tape_setup
    test_medium_locate tape
    test_locate_cli tape
    test_get_locate_cli tape
fi
