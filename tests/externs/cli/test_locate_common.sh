#!/bin/bash

#
#  All rights reserved (c) 2014-2025 CEA/DAM.
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

test_dir=$(dirname $(readlink -e $0))
medium_locker_bin="./medium_locker"
. $test_dir/test_env.sh
. $test_dir/setup_db.sh
. $test_dir/test_launch_daemon.sh
. $test_dir/tape_drive.sh
. $test_dir/utils_generation.sh

function test_locate_cli
{
    local obj="object_tlc"
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
    self_hostname=$(hostname -s)
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

    $phobos put --copy-name blob /etc/hosts oid-copy
    $valg_phobos locate --copy-name unknown_copy oid-copy &&
        error "Locating an unknown copy must fail"

    locate_hostname=$($valg_phobos locate --copy-name blob oid-copy)
    if [ "$locate_hostname" != "$self_hostname" ]; then
        error "Cli locate with copy-name returned $locate_hostname " \
              "instead of $self_hostname"
    fi
}

function test_medium_locate
{
    local family=$1
    local media_name="${family}s"
    local self_hostname=$(hostname -s)
    local fake_hostname="fake_hostname"
    local locate_hostname=""
    local was_locked="false"

    # test error while locating an unknown medium
    $valg_phobos $family locate unknown_medium &&
        error "Locating an unknown $family must fail"

    # set medium to test
    local medium=$(echo ${!media_name} | nodeset -e | awk '{print $1;}')

    # test with relative path
    if [ "$family" == "dir" ]; then
        $valg_phobos $family locate "$medium/" ||
            error "Error while locating $medium/"
        medium_name=$(basename $medium)
        $valg_phobos $family locate "./$medium_name" ||
            error "Error while locating ./$medium_name"
    fi

    # test error on an admin locked medium
    $phobos $family lock $medium ||
        error "Error while locking before locate"
    $valg_phobos $family locate $medium &&
        error "Locating an admin locked $family must fail"
    $phobos $family unlock $medium ||
        error "Error while unlocking lock after locate"

    # locate an unlocked medium
    # check if medium is lock
    locker=$($phobos $family list -o lock_hostname $medium)
    if [ "$locker" == "$self_hostname" ]; then
        was_locked="true"
        # we artificially force an unlock
        $medium_locker_bin unlock $family $medium $self_hostname   \
                $PID_LRS ||
            error "Error while unlocking $family before locate"
    fi

    if [ "$family" == "dir" ]; then
        $valg_phobos $family locate $medium &&
            error "Locating a dir without any lock must failed"
    else
        locate_hostname=$($valg_phobos $family locate $medium)
        if [ "$locate_hostname" != "" ]; then
            error "$family locate returned $locate_hostname instead of " \
                  "an empty string on an unlocked medium"
        fi
    fi

    # locate on a locked medium
    $medium_locker_bin lock $family $medium $fake_hostname $PID_LRS ||
        error "Error while locking medium before locate"

    locate_hostname=$($valg_phobos $family locate $medium)
    if [ "$locate_hostname" != "$fake_hostname" ]; then
        error "$family locate returned $locate_hostname instead of " \
              "$fake_hostname on a locked medium"
    fi

    locate_hostname=$($valg_phobos $family locate --library legacy $medium)
    if [ "$locate_hostname" != "$fake_hostname" ]; then
        error "$family locate --library legacy returned $locate_hostname " \
              "instead of $fake_hostname on a locked medium"
    fi

    $valg_phobos $family locate --library bad_library $medium &&
        error "Locating a medium in an unexisting library must fail"

    # We remove the lock
    $medium_locker_bin unlock $family $medium $fake_hostname $PID_LRS ||
        error "Error while unlocking medium after locate"

    # we artificially restore the lock
    if [ "$was_locked" == "true" ]; then
        $medium_locker_bin lock $family $medium $self_hostname \
                $PID_LRS ||
            error "Error while restore $family lock after locate"
    fi
}

function test_get_locate_cli
{
    local other_host="blob" #taken from test_locate_common.sh
    local oid="oid_tglc"
    local family=$1
    local pid="12345"
    local media_name="${family}s"
    local media=$(echo ${!media_name} | nodeset -e)
    local self_hostname=$(hostname -s)
    declare -A was_locked
    for med in ${media}; do
        was_locked[${med}]="false"
    done

    $phobos put /etc/hosts $oid ||
        error "Error while putting $oid"

    $phobos get --best-host $oid /tmp/out1 ||
        error "Get operation should have succeeded"

    rm -f /tmp/out1

    # PUT and GET let existing locks, we artificaly clear it
    for med in ${media}; do
        locker=$($phobos $family list -o lock_hostname $med)
        if [ "$locker" == "$self_hostname" ]; then
            was_locked[${med}]="true"
            $medium_locker_bin unlock $family $med $self_hostname $PID_LRS ||
                error "Error unlocking $family before get --best-host"
        fi
    done

    # Lock the $family elements using the current hostname
    $medium_locker_bin lock $family all $self_hostname $pid ||
       error "Error while locking media before get --best-host"

    # And try to use "get --best-host" as another host, which will fail because
    # $self_hostname has locks on the object
    hostname $other_host
    get_locate_output=$($valg_phobos get --best-host $oid /tmp/out2 || true)
    hostname $self_hostname

    local output="Current host is not the best to get this object, try on"
    output="$output these other nodes, '$oid' : '$self_hostname'"
    if [ "$get_locate_output" != "$output" ]; then
        error "Object should have been located on node '$self_hostname'"
    fi

    $medium_locker_bin unlock $family all $self_hostname $pid ||
        error "Error while unlocking medium1 after get --best-host"

    # we artificially restore the locks
    for med in ${media}; do
        if [ "${was_locked[$med]}" == "true" ]; then
            $medium_locker_bin lock $family $med $self_hostname $PID_LRS ||
                error "Error while restoring $family lock after locate"
        fi
    done
}

function test_locate_update_timestamp
{
    local obj=$(generate_prefix_id)

    $phobos put /etc/hosts $obj || error "Error while putting $obj"
    med=$($phobos extent list --output media_name $obj)
    med=${med#\[\'}
    med=${med%\'\]}
    psql_cmd="SELECT last_locate FROM lock WHERE id = '${med}_legacy';"

    $valg_phobos locate $obj
    db_time1=$($PSQL -t -c "$psql_cmd")
    db_time1=$(date -d "$db_time1" +%s)

    sleep 1
    $valg_phobos locate $obj

    db_time2=$($PSQL -t -c "$psql_cmd")
    db_time2=$(date -d "$db_time2" +%s)

    if [ $db_time1 -ge $db_time2 ]; then
        error "Invalid locate timestamp"
    fi
}

function test_locate_locked_splits
{
    local oid="oid_tlfs"
    local family=$1
    local pid="12345"
    local self_host=$(hostname -s)
    local other_host="blob"
    local size="$2"
    local IN_FILE=$(mktemp /tmp/test.pho.XXXX)

    local media=($($phobos $family list))

    if [[ ${#media[@]} -ne 8 ]]; then
        error "Invalid number of media, should be 8"
    fi

    dd if=/dev/random of=$IN_FILE count=$size bs=1

    $phobos put -f $family --lyt-params "repl_count=4" $IN_FILE $oid ||
        error "Error while putting $oid"

    $phobos get --best-host $oid /tmp/out1 ||
        error "Get operation should have succeeded"

    rm -f /tmp/out1

    waive_lrs

    $PSQL << EOF
UPDATE device SET host = '$other_host' WHERE path = '${media[1]}';
UPDATE device SET host = '$other_host' WHERE path = '${media[2]}';
UPDATE device SET host = '$other_host' WHERE path = '${media[3]}';
EOF

    invoke_lrs

    # lock extents 1, 2 and 3 for $other_host
    for medium in "${media[@]:1:3}"; do
        $medium_locker_bin lock $family $medium $other_host $PID_LRS ||
            error "Error locking $medium for host $other_host"
    done

    for medium in "${media[@]:5:3}"; do
        $medium_locker_bin unlock $family $medium $self_host $PID_LRS ||
            error "Error unlocking $medium for host $other_host"
    done

    # at this point: extents 5, 6 and 7 are unlocked

    locate_hostname=$($valg_phobos locate --focus-host $self_host $oid)
    if [ "$locate_hostname" != "$self_host" ]; then
        error "locate on $oid returned $locate_hostname instead of " \
              "$self_host even though it has a lock on a replica"
    fi

    locate_hostname=$($valg_phobos locate --focus-host $other_host $oid)
    if [ "$locate_hostname" != "$self_host" ]; then
        error "locate on $oid returned $locate_hostname instead of " \
              "$self_host even though it has a lock on a replica"
    fi

    for medium in "${media[@]:1:3}"; do
        $medium_locker_bin unlock $family $medium $other_host $PID_LRS ||
            error "Error unlocking $medium for host $other_host"
    done

    waive_lrs

    $PSQL << EOF
UPDATE device SET host = '$self_host' WHERE host = '$other_host';
EOF

    invoke_lrs
}
