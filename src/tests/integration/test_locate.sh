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
test_bin="$test_dir/test_locate"
. $test_dir/../test_env.sh
. $test_dir/../setup_db.sh
. $test_dir/../test_launch_daemon.sh
. $test_dir/../tape_drive.sh

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
}

function test_locate_cli
{
    # test error on locating an unknown object
    $phobos locate unknown_object &&
        error "Locating an unknown object must fail"

    # put a new object from localhost and locate it on localhost
    $phobos put /etc/hosts object_to_locate_by_cli ||
        error "Error on putting object_to_locate_by_cli"
    locate_hostname=$($phobos locate object_to_locate_by_cli)
    self_hostname=$(uname -n)
    if [ "$locate_hostname" != "$self_hostname" ]; then
        error "Cli locate returned $locate_hostname instead of $self_hostname"
    fi
}

setup_tables
invoke_daemon
trap cleanup EXIT
dir_setup

# test locate on disk
export PHOBOS_STORE_default_family="dir"
test_locate_cli
$test_bin dir || exit 1

if [[ -w /dev/changer ]]; then
    cleanup
    echo "Tape test mode"
    setup_tables
    invoke_daemon
    export PHOBOS_STORE_default_family="tape"
    tape_setup
    test_locate_cli
    $test_bin tape || exit 1
fi

