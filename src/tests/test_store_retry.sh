#!/bin/sh -e

test_bin_dir=$(dirname $(readlink -e $0))

. $test_bin_dir/test_env.sh
. $test_bin_dir/test_launch_daemon.sh
export PHOBOS_LRS_default_family=dir
invoke_daemon
$LOG_COMPILER $LOG_FLAGS ./test_store_retry
waive_daemon
if [ -w /dev/changer ]; then
    export PHOBOS_LRS_default_family=tape
    invoke_daemon
    $LOG_COMPILER $LOG_FLAGS ./test_store_retry
    waive_daemon
fi
