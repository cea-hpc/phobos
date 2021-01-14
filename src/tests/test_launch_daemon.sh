#!/bin/sh

PID_DAEMON=0

export PHOBOSD_PID_FILEPATH="$test_bin_dir/phobosd.pid"
export PHOBOS_LRS_lock_file="$test_bin_dir/phobosd.lock"

function invoke_daemon()
{
    $LOG_COMPILER $LOG_FLAGS $phobosd &
    wait $!
    PID_DAEMON=`cat $PHOBOSD_PID_FILEPATH`
    rm $PHOBOSD_PID_FILEPATH
}

function waive_daemon()
{
    kill $PID_DAEMON &>/dev/null || echo "Daemon was not running"
    # wait would not work here because PID_DAEMON is not an actual child
    # of this shell (created by phobosd in invoke_daemon)
    tail --pid=$PID_DAEMON -f /dev/null
}
