#!/bin/sh -e

. ./test_env.sh
export PHOBOS_LRS_default_family=dir
./test_store_retry
if [ -w /dev/changer ]; then
    export PHOBOS_LRS_default_family=tape
    ./test_store_retry
fi
