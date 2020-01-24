#!/bin/sh

PID_DAEMON=0

export PHOBOSD_PID_FILEPATH="$test_bin_dir/phobosd.pid"

function invoke_daemon()
{
    $LOG_COMPILER $LOG_FLAGS $test_bin_dir/../lrs/phobosd &
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
