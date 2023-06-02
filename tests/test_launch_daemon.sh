#!/bin/sh

PID_DAEMON=0

export DAEMON_PID_FILEPATH="$test_bin_dir/phobosd.pid"
export PHOBOS_LRS_lock_file="$test_bin_dir/phobosd.lock"

function invoke_daemon()
{
    $LOG_COMPILER $LOG_FLAGS $phobosd "$*" &
    wait $! || { echo "failed to start daemon"; return 1; }
    PID_DAEMON=`cat $DAEMON_PID_FILEPATH`
}

function waive_daemon()
{
    if [ ! -f "$DAEMON_PID_FILEPATH" ]; then
        return 0
    fi

    PID_DAEMON=`cat $DAEMON_PID_FILEPATH`
    rm $DAEMON_PID_FILEPATH

    if [ $PID_DAEMON -eq 0 ]; then
        return 0
    fi

    kill $PID_DAEMON &>/dev/null || echo "Daemon was not running"
    # wait would not work here because PID_DAEMON is not an actual child
    # of this shell (created by phobosd in invoke_daemon)
    timeout 10 tail --pid=$PID_DAEMON -f /dev/null
    PID_DAEMON=0
}
