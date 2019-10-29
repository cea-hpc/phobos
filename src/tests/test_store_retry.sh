#!/bin/sh -e

. ./test_env.sh
export PHOBOS_LRS_default_family=dir
$LOG_COMPILER $LOG_FLAGS ./test_store_retry
if [ -w /dev/changer ]; then
    export PHOBOS_LRS_default_family=tape
    $LOG_COMPILER $LOG_FLAGS ./test_store_retry
fi
