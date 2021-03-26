#!/bin/bash

#
#  All rights reserved (c) 2014-2020 CEA/DAM.
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
. $test_dir/test_env.sh
. $test_dir/setup_db.sh

export PHOBOS_LRS_lock_file="$test_bin_dir/phobosd.lock"

function error
{
    echo "$*"
    drop_tables
    exit 1
}

function test_multiple_instances
{
    setup_tables

    pidfile="/tmp/pidfile"

    $phobosd -i &
    first_process=$!

    sleep 1

    timeout 60 $phobosd -i &
    second_process=$!

    wait $second_process && true
    rc=$?
    kill $first_process

    # Second daemon error code should be -EEXIST, which is -17
    test $rc -eq $((256 - 17)) ||
        error "Second daemon instance does not get the right error code"

    drop_tables
}

function test_release_medium_old_locks
{
    setup_tables

    mkdir /tmp/dir0 /tmp/dir1
    $phobos dir add --unlock /tmp/dir[0-1]
    rmdir /tmp/dir0 /tmp/dir1

    host=`hostname`
    pid=$BASHPID

    # Update media to lock them by a 'daemon instance'
    # Only one is locked by this host
    psql phobos phobos -c \
        "insert into lock (type, id, hostname, owner) values
             ('media'::lock_type, '/tmp/dir0', '$host', $pid),
             ('media'::lock_type, '/tmp/dir1', '${host}0', $pid);"

    # Use gdb to stop the daemon once the locks are released
    (
    trap -- "rm '$PHOBOS_LRS_lock_file'" EXIT
    PHOBOS_LRS_families="dir" gdb $phobosd <<< "break sched_check_medium_locks
                                                run -i
                                                finish
                                                quit"
    )

    # Check that only the correct medium is unlocked
    lock=$($phobos dir list -o lock_hostname /tmp/dir0)
    [ "None" == "$lock" ] || error "Medium should be unlocked"

    lock=$($phobos dir list -o lock_hostname /tmp/dir1)
    [ "${host}0" == "$lock" ] || error "Medium should be locked"

    drop_tables
}

function test_release_device_old_locks
{
    setup_tables

    $phobos drive add --unlock /dev/st[0-1]
    host=`hostname`
    pid=$BASHPID

    # Inserting directly into the lock table requires the
    # actual names of each drive, so we fetch them
    dev_st0_id=$($phobos drive list -o name /dev/st0)
    dev_st1_id=$($phobos drive list -o name /dev/st1)

    # Update devices to lock them by a 'daemon instance'
    # Only one is locked by this host
    psql phobos phobos -c \
        "insert into lock (type, id, hostname, owner) values
             ('device'::lock_type, '$dev_st0_id', '$host', $pid),
             ('device'::lock_type, '$dev_st1_id', '${host}0', $pid);"

    # Use gdb to stop the daemon once the locks are released
    (
    trap -- "rm '$PHOBOS_LRS_lock_file'" EXIT
    PHOBOS_LRS_families="tape" gdb $phobosd <<< "break sched_check_device_locks
                                                 run -i
                                                 finish
                                                 quit"
    )

    # Check that only the correct device is unlocked
    lock=$($phobos drive list -o lock_hostname /dev/st0)
    [ "None" == "$lock" ] || error "Device should be unlocked"

    lock=$($phobos drive list -o lock_hostname /dev/st1)
    [ "${host}0" == "$lock" ] ||
        error "Device should be locked"

    drop_tables
}

drop_tables
test_multiple_instances
test_release_medium_old_locks

# Tape tests are available only if /dev/changer exists, which is the entry
# point for the tape library.
if [[ -w /dev/changer ]]; then
    test_release_device_old_locks
fi
