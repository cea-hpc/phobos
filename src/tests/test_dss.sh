#!/bin/bash
set -e

test_bin_dir=$(dirname "${BASH_SOURCE[0]}")
test_bin="$test_bin_dir/test_dss"

. $test_bin_dir/setup_db.sh

function clean_test
{
    echo "cleaning..."
    #drop_tables
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

function test_check_set
{
    local type=$1
    local crit=$2
    local action=$3


    $test_bin set "$type" "$crit" "$action"
}

function test_check_lock
{
    local type=$1
    local action=$2
    local returncode=$3
    local rc=0

    $test_bin $action $type || rc=$?
    if [ $rc -ne $returncode ]
    then
        echo "$test_bin return $rc != $returncode"
        return 1
    fi
    return 0
}


trap clean_test ERR EXIT

clean_test
setup_tables
insert_examples

echo

echo "**** TESTS: DSS_GET DEV ****"
test_check_get "device" "all"
test_check_get "device" "path LIKE /dev%"
test_check_get "device" "family = tape"
test_check_get "device" "family = dir"
test_check_get "device" "id != foo"
test_check_get "device" "host != foo"
test_check_get "device" "adm_status = unlocked"
test_check_get "device" "changer_idx > 0"
test_check_get "device" "model = ULTRIUM-TD6"


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

echo "**** TEST: DSS_SET DEVICE ****"
test_check_set "device" "insert"
test_check_get "device" "id LIKE %COPY%"
test_check_set "device" "update"
test_check_get "device" "host LIKE %UPDATE%"
test_check_set "device" "delete"
test_check_get "device" "id LIKE %COPY%"
echo "**** TEST: DSS_SET MEDIA  ****"
test_check_set "media" "insert"
test_check_get "media" "id LIKE %COPY%"
test_check_set "media" "update"
test_check_get "media" "stats::json->>'nb_obj' > 1002"
test_check_set "media" "delete"
test_check_get "media" "id LIKE %COPY%"
echo "**** TEST: DSS_SET OBJECT  ****"
test_check_set "object" "insert"
test_check_get "object" "oid LIKE %COPY%"
#test_check_set "object" "update"
test_check_set "object" "delete"
test_check_get "object" "oid LIKE %COPY%"
echo "**** TEST: DSS_SET EXTENT  ****"
test_check_set "extent" "insert"
test_check_get "extent" "oid LIKE %COPY%"
test_check_set "extent" "update"
test_check_get "extent" "oid LIKE %COPY%"
test_check_set "extent" "delete"
test_check_get "extent" "oid LIKE %COPY%"

insert_examples

echo "**** TESTS: DSS_DEVICE LOCK/UNLOCK  ****"
echo "*** TEST LOCK ***"
test_check_lock "device" "lock" "0"
echo "*** TEST DOUBLE LOCK (EEXIST expected) ***"
test_check_lock "device" "lock" 239
echo "*** TEST UNLOCK ***"
test_check_lock "device" "unlock" 0
echo "*** TEST RELOCK ***"
test_check_lock "device" "lock" 0

echo "**** TESTS: DSS_MEDIA LOCK/UNLOCK  ****"
echo "*** TEST LOCK ***"
test_check_lock "media" "lock" "0"
echo "*** TEST DOUBLE LOCK (EEXIST expected) ***"
test_check_lock "media" "lock" 239
echo "*** TEST UNLOCK ***"
test_check_lock "media" "unlock" 0
echo "*** TEST DOUBLE UNLOCK (EEXIST expected) ***"
test_check_lock "media" "unlock" 239
echo "*** TEST RELOCK ***"
test_check_lock "media" "lock" 0

# Uncomment if you want the db to persist after test
# trap - EXIT ERR
