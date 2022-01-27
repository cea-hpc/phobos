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

test_dir=$(dirname $(readlink -e $0))
. $test_dir/../../test_env.sh
. $test_dir/../../setup_db.sh

function setup
{
    export PHOBOS_LRS_lock_file="$test_bin_dir/phobosd.lock"
}

function cleanup
{
    drop_tables
}

function test_multiple_instances
{
    setup_tables

    pidfile="/tmp/pidfile"

    $phobosd -i &
    first_process=$!

    sleep 1

    timeout 60 $LOG_COMPILER $phobosd -i &
    second_process=$!

    wait $second_process && true
    rc=$?
    kill $first_process

    # Second daemon error code should be -EEXIST, which is -17
    test $rc -eq $((256 - 17)) ||
        error "Second daemon instance does not get the right error code"

    drop_tables
}

function test_recover_dir_old_locks
{
    setup_tables

    dir0=$(mktemp -d /tmp/test_recover_dir_old_locksXXX)
    dir1=$(mktemp -d /tmp/test_recover_dir_old_locksXXX)
    $phobos dir add --unlock ${dir0}
    $phobos dir add --unlock ${dir1}

    host=`hostname`
    pid=$BASHPID

    # Update media to lock them by a 'daemon instance'
    # Only one is locked by this host
    psql phobos phobos -c \
        "insert into lock (type, id, hostname, owner) values
             ('media'::lock_type, '${dir0}', '$host', $pid),
             ('media'::lock_type, '${dir1}', '${host}other', $pid);"

    # Start and stop the lrs daemon
    PHOBOS_LRS_families="dir" timeout --preserve-status 10 $phobosd -i &
    daemon_process=$!

    wait $daemon_process && true
    rc=$?
    rmdir ${dir0} ${dir1}

    # check return status
    test $rc -eq 0 ||
        error "Daemon process returns an error status : ${rc}"

    # Check only the lock of the correct hostname is released
    lock=$($phobos dir list -o lock_hostname ${dir0})
    [ "None" == "$lock" ] || error "${dir0} should be unlocked"

    lock=$($phobos dir list -o lock_hostname ${dir1})
    [ "${host}other" == "$lock" ] || error "${dir1} should be locked"

    drop_tables
}

function test_remove_invalid_media_locks
{
    setup_tables

    dir0=$(mktemp -d /tmp/test_remove_invalid_media_locksXXX)

    host=`hostname`
    pid=$BASHPID

    # Update media to lock them by a 'daemon instance'
    # Only one is locked by this host
    psql phobos phobos -c \
        "insert into device (family, model, id, host, adm_status, path)
            values ('dir', NULL, 'blob:${dir0}', 'blob', 'unlocked',
                    '${dir0}');"
    psql phobos phobos -c \
        "insert into media (family, model, id, adm_status, fs_type,
                            address_type, fs_status, stats, tags)
            values ('dir', NULL, '${dir0}', 'unlocked', 'POSIX', 'HASH1',
                    'blank', '{\"nb_obj\":0, \"logc_spc_used\":0, \
                               \"phys_spc_used\":0, \"phys_spc_free\":1024, \
                               \"nb_load\":0, \"nb_errors\":0, \
                               \"last_load\":0}', '[]');"
    psql phobos phobos -c \
        "insert into lock (type, id, hostname, owner)
            values ('media'::lock_type, '${dir0}', '$host', $pid);"

    # Start and stop the lrs daemon
    PHOBOS_LRS_families="dir" timeout --preserve-status 10 $phobosd -i &
    daemon_process=$!

    wait $daemon_process && true
    rc=$?
    rmdir ${dir0}

    # check return status
    test $rc -eq 0 ||
        error "Daemon process returns an error status : ${rc}"

    # Check only the lock of the correct hostname is released
    lock=$($phobos dir list -o lock_hostname ${dir0})
    [ "None" == "$lock" ] || error "${dir0} should be unlocked"

    drop_tables
}

function test_recover_drive_old_locks
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
             ('device'::lock_type, '$dev_st1_id', '${host}other', $pid);"

    # Start and stop the lrs daemon
    PHOBOS_LRS_families="tape" timeout --preserve-status 10 $phobosd -i &
    daemon_process=$!

    wait $daemon_process && true
    rc=$?

    # check return status
    test $rc -eq 0 ||
        error "Daemon process returns an error status : ${rc}"

    # Check that only the correct device is unlocked
    lock=$($phobos drive list -o lock_hostname /dev/st0)
    [ "None" == "$lock" ] || error "Device should be unlocked"

    lock=$($phobos drive list -o lock_hostname /dev/st1)
    [ "${host}other" == "$lock" ] || error "Device should be locked"

    drop_tables
}

function test_remove_invalid_device_locks
{
    setup_tables

    $phobos drive add --unlock /dev/st0

    host=`hostname`
    pid=$BASHPID
    fake_host="blob"

    dev_st0_id=$($phobos drive list -o name /dev/st0)
    dev_st1_model=$($phobos drive list -o model /dev/st0)
    dev_st1_id="fake_id_remove_invalid_device_locks"

    psql phobos phobos -c \
        "insert into device (family, model, id, host, adm_status, path)
            values ('tape', '$dev_st1_model', '$dev_st1_id', '$fake_host',
                    'unlocked', '/dev/st1');"
    psql phobos phobos -c \
        "insert into lock (type, id, hostname, owner)
            values ('device'::lock_type, '$dev_st1_id', '$host', $pid);"

    # Start and stop the lrs daemon
    PHOBOS_LRS_families="tape" timeout --preserve-status 10 $phobosd -i &
    daemon_process=$!

    wait $daemon_process && true
    rc=$?

    # check return status
    test $rc -eq 0 ||
        error "Daemon process returns an error status : ${rc}"

    # Check only the lock of the correct hostname is released
    lock=$($phobos drive list -o lock_hostname /dev/st0)
    [ "None" == "$lock" ] || error "Dir should be unlocked"

    lock=$($phobos drive list -o lock_hostname /dev/st1)
    [ "None" == "$lock" ] || error "Dir should be unlocked"

    drop_tables
}

trap cleanup EXIT
setup

test_multiple_instances
test_recover_dir_old_locks
test_remove_invalid_media_locks

# Tape tests are available only if /dev/changer exists, which is the entry
# point for the tape library.
if [[ -w /dev/changer ]]; then
    test_recover_drive_old_locks
    test_remove_invalid_device_locks
fi
