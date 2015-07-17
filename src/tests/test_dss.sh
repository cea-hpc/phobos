#!/bin/bash
set -e

test_bin_dir=$(dirname "${BASH_SOURCE[0]}")
test_bin="$test_bin_dir/test_dss"

. $test_bin_dir/setup_db.sh

function clean_test
{
    echo "cleaning..."
    drop_tables
}

function error
{
    echo "ERROR: $*" >&2
    clean_test
    exit 1
}

function test_check_get
{
    local type=$1
    local crit=$2


    $test_bin get "$type" "$crit"
}

trap clean_test ERR EXIT

clean_test
setup_tables
insert_examples

echo
echo "**** TESTS: DSS_GET ALL ****"

test_check_get "dev" "all"

echo "**** TESTS: DSS_GET DEV TAPE ****"
test_check_get "dev" "tape"
echo "**** TESTS: DSS_GET DEV DISK ****"
test_check_get "dev" "disk"
echo "**** TESTS: DSS_GET MEDIA USED_SPACE > 16000000000 ****"
test_check_get "media" "used_space"
echo "**** TESTS: DSS_GET MEDIA CHARSET ****"
test_check_get "media" "charset"
# Uncomment if you want the db to persist after test
# trap - EXIT ERR
