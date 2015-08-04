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

echo "**** TESTS: DSS_GET DEV ****"
test_check_get "dev" "all"
test_check_get "dev" "path LIKE /dev%"
test_check_get "dev" "family = tape"
test_check_get "dev" "family = dir"
test_check_get "dev" "id != foo"
test_check_get "dev" "host != foo"
test_check_get "dev" "adm_status = unlocked"
test_check_get "dev" "changer_idx > 0"
test_check_get "dev" "model = ULTRIUM-TD6"


echo "**** TESTS: DSS_GET MEDIA ****"
test_check_get "media" "all"
test_check_get "media" "(stats->>'phys_spc_used')::bigint > 42469425152"
test_check_get "media" "(stats->>'phys_spc_used')::bigint = 42469425152"
test_check_get "media" "(stats->>'phys_spc_used')::bigint < 42469425152"
test_check_get "media" "family = tape"
test_check_get "media" "family = dir"
test_check_get "media" "model = LTO6"
test_check_get "media" "id != foo"
test_check_get "media" "adm_status != unlocked"
test_check_get "media" "fs_type != LTFS"
test_check_get "media" "address_type != HASH1"
test_check_get "media" "fs_status != blank"

echo "**** TESTS: DSS_GET OBJECT ****"
test_check_get "object" "all"
test_check_get "object" "oid LIKE 012%"
test_check_get "object" "oid LIKE koéèê^!$£}[<>à@\\\"~²§ok"

echo "**** TESTS: DSS_GET EXTENT ****"
test_check_get "extent" "all"
test_check_get "extent" "extents_mda_idx(extent.extents) @> 073221L6"
test_check_get "extent" \
	       "extents_mda_idx(extent.extents) @> phobos1:/tmp/pho_testdir1"
test_check_get "extent" "extents_mda_idx(extent.extents) @> DONOTEXIST"
test_check_get "extent" "extents_mda_idx(extent.extents) @> phobos1:/donotexist"
test_check_get "extent" "oid = QQQ6ASQDSQD"
test_check_get "extent" "oid != QQQ6ASQDSQD"
test_check_get "extent" "oid LIKE Q%D"
test_check_get "extent" "copy_num = 0"
test_check_get "extent" "state = pending"
test_check_get "extent" "lyt_type = simple"

# Uncomment if you want the db to persist after test
# trap - EXIT ERR
