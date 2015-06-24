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
echo "**** TESTS: DSS_GET ****"

test_check_get "dev" "ALL"

# Uncomment if you want the db to persist after test
# trap - EXIT ERR
