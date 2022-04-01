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

test_dir=$(dirname $(readlink -e $0))
. $test_dir/../../test_env.sh
. $test_dir/../../setup_db.sh
. $test_dir/../../test_launch_daemon.sh
. $test_dir/../../utils_tape.sh
controlled_store="$test_dir/controlled_store"

set -xe

function setup
{
    export PHOBOS_LRS_lock_file="$test_bin_dir/phobosd.lock"
}

function cleanup
{
    drop_tables
}

function test_invalid_lock_file()
{
    trap "waive_daemon" EXIT
    drop_tables
    setup_tables

set +e
    export PHOBOS_LRS_lock_file="/phobosd.lock"
    rm -rf "$PHOBOS_LRS_lock_file"
    invoke_daemon
    pgrep -f phobosd ||
        error "Should have succeeded with valid folder '/'"
    waive_daemon

    local folder="$test_bin_dir/a"
    export PHOBOS_LRS_lock_file="$folder/phobosd.lock"
    rm -rf "$folder"
    invoke_daemon
    pgrep -f phobosd &&
        error "Should have failed with non-existing folder '$folder'"

    mkdir -p "$folder"
    invoke_daemon
    pgrep -f phobosd ||
        error "Should have succeeded after creating valid folder '$folder'"
    waive_daemon

    rm -rf "$folder"

    # Create $folder as a simple file to fail the "is dir" condition
    touch "$folder"
    invoke_daemon
    pgrep -f phobosd &&
        error "Should have failed because '$folder' is not a directory"

    rm -rf "$folder"
set -e

    unset PHOBOS_LRS_lock_file
    drop_tables

    trap cleanup EXIT
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
    dir2=$(mktemp -d /tmp/test_recover_dir_old_locksXXX)
    dir3=$(mktemp -d /tmp/test_recover_dir_old_locksXXX)
    $phobos dir add --unlock ${dir0} ${dir1} ${dir2} ${dir3}

    host=$(hostname)
    pid=$BASHPID

    # Update media to lock them by a 'daemon instance'
    # Only one is locked by this host
    $PSQL -c \
        "insert into lock (type, id, hostname, owner) values
             ('media'::lock_type, '${dir0}', '$host', $pid),
             ('media_update'::lock_type, '${dir1}', '$host', $pid),
             ('media'::lock_type, '${dir2}', '${host}other', $pid),
             ('media_update'::lock_type, '${dir3}', '${host}other', $pid);"

    # Start and stop the lrs daemon
    PHOBOS_LRS_families="dir" timeout --preserve-status 10 $phobosd -vv -i &
    daemon_process=$!

    wait $daemon_process && true
    rc=$?
    rmdir ${dir0} ${dir1} ${dir2} ${dir3}

    # check return status
    test $rc -eq 0 ||
        error "Daemon process returns an error status : ${rc}"

    # Check only the lock of the correct hostname is released
    lock=$($phobos dir list -o lock_hostname ${dir0})
    [ "None" == "$lock" ] || error "${dir0} should be unlocked"

    lock=$($phobos dir list -o lock_hostname ${dir1})
    [ "None" == "$lock" ] || error "${dir1} should be unlocked"

    lock=$($phobos dir list -o lock_hostname ${dir2})
    [ "${host}other" == "$lock" ] || error "${dir2} should be locked"

    lock=$($PSQL -t -c "select hostname from lock where id = '${dir3}';" |
           xargs)
    [ "${host}other" == "$lock" ] || error "${dir3} should be locked"

    drop_tables
}

function test_remove_invalid_media_locks
{
    setup_tables

    dir0=$(mktemp -d /tmp/test_remove_invalid_media_locksXXX)
    dir1=$(mktemp -d /tmp/test_remove_invalid_media_locksXXX)

    host=$(hostname)
    pid=$BASHPID

    # Update media to lock them by a 'daemon instance'
    # Only one is locked by this host
    $PSQL -c \
        "insert into device (family, model, id, host, adm_status, path)
            values ('dir', NULL, 'blob:${dir0}', 'blob', 'unlocked', '${dir0}'),
                   ('dir', NULL, 'blob:${dir1}', 'blob', 'unlocked',
                    '${dir1}');"
    $PSQL -c \
        "insert into media (family, model, id, adm_status, fs_type,
                            address_type, fs_status, stats, tags)
            values ('dir', NULL, '${dir0}', 'unlocked', 'POSIX', 'HASH1',
                    'blank', '{\"nb_obj\":0, \"logc_spc_used\":0, \
                               \"phys_spc_used\":0, \"phys_spc_free\":1024, \
                               \"nb_load\":0, \"nb_errors\":0, \
                               \"last_load\":0}', '[]'),
                   ('dir', NULL, '${dir1}', 'unlocked', 'POSIX', 'HASH1',
                    'blank', '{\"nb_obj\":0, \"logc_spc_used\":0, \
                               \"phys_spc_used\":0, \"phys_spc_free\":1024, \
                               \"nb_load\":0, \"nb_errors\":0, \
                               \"last_load\":0}', '[]');"
    $PSQL -c \
        "insert into lock (type, id, hostname, owner)
            values ('media'::lock_type, '${dir0}', '$host', $pid),
                   ('media_update'::lock_type, '${dir1}', '$host', $pid);"

    # Start and stop the lrs daemon
    PHOBOS_LRS_families="dir" timeout --preserve-status 10 $phobosd -i &
    daemon_process=$!

    wait $daemon_process && true
    rc=$?
    rmdir ${dir0} ${dir1}

    # check return status
    test $rc -eq 0 ||
        error "Daemon process returns an error status : ${rc}"

    # Check only the locks of the correct hostname are released
    lock=$($phobos dir list -o lock_hostname ${dir0})
    [ "None" == "$lock" ] || error "${dir0} should be unlocked"
    lock=$($PSQL -t -c "select hostname from lock where id = '${dir1}';" |
           xargs)
    [ -z $lock ] || error "${dir1} should be unlocked"

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
    $PSQL -c \
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

    $PSQL -c \
        "insert into device (family, model, id, host, adm_status, path)
            values ('tape', '$dev_st1_model', '$dev_st1_id', '$fake_host',
                    'unlocked', '/dev/st1');"
    $PSQL -c \
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

function test_wait_end_of_IO_before_shutdown()
{
    local dir=$(mktemp -d)

    trap "waive_daemon; drop_tables; rm -rf '$dir'" EXIT
    setup_tables
    invoke_daemon

    $phobos dir add "$dir"
    $phobos dir format --unlock --fs posix "$dir"

    $controlled_store put &
    local pid=$!

    # wait for the request to reach the LRS
    sleep 1

    kill $PID_DAEMON
    sleep 1
    ps --pid $PID_DAEMON || error "Daemon should still be online"

    # send release request
    kill -s SIGUSR1 $pid

    timeout 10 tail --pid=$PID_DAEMON -f /dev/null
    if [[ $? != 0 ]]; then
        error "Daemon not stopped after 10 seconds"
    fi
    PID_DAEMON=0

    drop_tables
    rm -rf '$dir'
}

function wait_for_process_end()
{
    local pid="$1"
    local count=0

    while ps --pid "$pid"; do
        if [[ $count > 10 ]]; then
            error "Process $pid should have stopped after 10s"
        fi
        ((count++)) || true
        sleep 1
    done
}

function test_cancel_waiting_requests_before_shutdown()
{
    local dir=$(mktemp -d)
    local file=$(mktemp)
    local res_file="res_file"

    trap "waive_daemon; drop_tables; rm -rf '$dir' '$file' '$res_file'" EXIT
    setup_tables
    invoke_daemon

    $phobos dir add "$dir"
    $phobos dir format --unlock --fs posix "$dir"

    $controlled_store put &
    local controlled_pid=$!

    # this request will be waiting in the LRS as the only dir is used by
    # controlled_store
    ( set +e; $phobos put --family dir "$file" oid; echo $? > "$res_file" ) &
    local put_pid=$!

    # wait for the request to reach the LRS
    sleep 1

    kill $PID_DAEMON

    # "timeout wait $put_pid" cannot be used here as "put_pid" will not be a
    # child of 'timeout'
    wait_for_process_end $put_pid

    if [[ $(cat "$res_file") == 0 ]]; then
        error "Waiting request should have been canceled"
    fi

    # send the release request
    kill -s SIGUSR1 $controlled_pid

    timeout 10 tail --pid=$PID_DAEMON -f /dev/null
    if [[ $? != 0 ]]; then
        error "Daemon not stopped after 10 seconds"
    fi
    PID_DAEMON=0

    drop_tables
    rm -rf "$dir" "$file" "$res_file"
}

function test_refuse_new_request_during_shutdown()
{
    local dir=$(mktemp -d)
    local file=$(mktemp)

    trap "waive_daemon; drop_tables; rm -rf '$dir' '$file'" EXIT
    setup_tables
    invoke_daemon

    $phobos dir add "$dir"
    $phobos dir format --unlock --fs posix "$dir"

    $controlled_store put &
    local pid=$!

    sleep 1
    kill $PID_DAEMON

    $phobos put --family dir "$file" oid &&
        error "New put should have failed during shutdown"

    # send the release request
    kill -s SIGUSR1 $pid

    timeout 10 tail --pid=$PID_DAEMON -f /dev/null
    if [[ $? != 0 ]]; then
        error "Daemon not stopped after 10 seconds"
    fi
    PID_DAEMON=0

    drop_tables
    rm -rf "$dir" "$file"
}

function test_mount_failure_during_read_response()
{
    local file=$(mktemp)
    local tape=$(get_tapes L6 1)
    local drive=$(get_drives 1)

    trap "waive_daemon; drop_tables; rm -f '$file'; \
          unset PHOBOS_LTFS_cmd_mount" EXIT
    setup_tables
    invoke_daemon

    dd if=/dev/urandom of="$file" bs=4096 count=5

    $phobos tape add --type lto6 "$tape"
    $phobos drive add "$drive"
    $phobos drive unlock "$drive"
    $phobos tape format --unlock "$tape"

    $phobos put "$file" oid ||
        error "Put command failed"

    # Force mount to fail
    export PHOBOS_LTFS_cmd_mount="sh -c 'exit 1'"
    waive_daemon
    invoke_daemon

    $phobos get oid "${file}.out" &&
        error "Get command should have failed"

    ps --pid "$PID_DAEMON"

    unset PHO_CFG_LTFS_cmd_mount
    waive_daemon
    drop_tables

    rm -f "$file"
}

trap cleanup EXIT

test_invalid_lock_file

setup

test_multiple_instances
test_recover_dir_old_locks
test_remove_invalid_media_locks
test_wait_end_of_IO_before_shutdown
test_cancel_waiting_requests_before_shutdown
test_refuse_new_request_during_shutdown

# Tape tests are available only if /dev/changer exists, which is the entry
# point for the tape library.
if [[ -w /dev/changer ]]; then
    test_recover_drive_old_locks
    test_remove_invalid_device_locks
    test_mount_failure_during_read_response
fi
