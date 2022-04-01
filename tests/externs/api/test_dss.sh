#!/bin/bash
# -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
# vim:expandtab:shiftwidth=4:tabstop=4:

#
#  All rights reserved (c) 2014-2022 CEA/DAM.
#
#  This file is part of Phobos.
#
#  Phobos is free software: you can redistribute it and/or modify it under
#  the terms of the GNU Lesser General Public License as published by
#  the Free Software Foundation, either version 2.1 of the License, or
#  (at your option) any later version.
#
#  Phobos is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU Lesser General Public License for more details.
#
#  You should have received a copy of the GNU Lesser General Public License
#  along with Phobos. If not, see <http://www.gnu.org/licenses/>.
#

test_bin_dir=$(dirname "${BASH_SOURCE[0]}")
test_bin="$test_bin_dir/test_dss"

. $test_bin_dir/../../setup_db.sh

set -xe

function setup
{
    setup_tables
    insert_examples
}

function cleanup
{
    echo "cleaning..."
    drop_tables
}

function check_rc
{
    local rc=$1
    local expect_fail=$2

    if [ -z "$expect_fail" ]
    then
        if [ $rc -ne 0 ]
        then
            echo "$test_bin failed with $rc"
            return 1
        fi
    else
        if [ $rc -eq 0 ]
        then
            echo "$test_bin succeeded against expectations"
            return 1
        fi
    fi
    return 0
}

function test_check_get
{
    local type=$1
    local crit=$2
    local nb_item=$3
    local expect_fail=$4
    local rc=0

    if [[ "$nb_item" != "" ]]
    then
        $LOG_COMPILER $test_bin get "$type" "$crit" "$nb_item" ||
            rc=$?
    elif [[ "$crit" != "" ]]
    then
        $LOG_COMPILER $test_bin get "$type" "$crit" || rc=$?
    else
        $LOG_COMPILER $test_bin get "$type"
    fi

    check_rc $rc $expect_fail
}

function test_check_set
{
    local type=$1
    local crit=$2
    local action=$3
    local expect_fail=$4
    local rc=0

    $LOG_COMPILER $test_bin set "$type" "$crit" "$action" || rc=$?
    check_rc $rc $expect_fail
}

function test_check_lock
{
    local target=$1
    local action=$2
    local lock_hostname=$3
    local lock_owner=$4
    local expect_fail=$5
    local rc=0

    $LOG_COMPILER $test_bin "$action" "$target" \
        $lock_hostname $lock_owner || rc=$?
    check_rc $rc $expect_fail
}

trap cleanup EXIT
setup

echo

echo "**** TESTS: DSS_GET DEV ****"
test_check_get "device" 'all'
test_check_get "device" '{"$LIKE": {"DSS::DEV::path": "/dev%"}}'
test_check_get "device" '{"DSS::DEV::family": "tape"}'
test_check_get "device" '{"$NOT": {"DSS::DEV::family": "tape"}}'
test_check_get "device" '{"DSS::DEV::family": "dir"}'
test_check_get "device" '{"$NOR": [{"DSS::DEV::serial": "foo"}]}'
test_check_get "device" '{"$NOR": [{"DSS::DEV::host": "foo"}]}'
test_check_get "device" '{"DSS::DEV::adm_status": "unlocked"}'
test_check_get "device" '{"DSS::DEV::model": "ULTRIUM-TD6"}'


echo "**** TESTS: DSS_GET MEDIA ****"
test_check_get "media" "all"
test_check_get "media" '{"$LT": {"DSS::MDA::vol_used": 42469425152}}'
test_check_get "media" '{"DSS::MDA::vol_used": 42469425152}'
test_check_get "media" '{"$NOT": {"DSS::MDA::vol_used": 42469425152}}'
test_check_get "media" '{"$GT": {"DSS::MDA::vol_used": 42469425152}}'
test_check_get "media" '{"DSS::MDA::family": "tape"}'
test_check_get "media" '{"DSS::MDA::family": "dir"}'
test_check_get "media" '{"DSS::MDA::model": "LTO6"}'
test_check_get "media" '{"$NOR": [{"DSS::MDA::id": "foo"}]}'
test_check_get "media" '{"$NOR": [{"DSS::MDA::adm_status": "unlocked"}]}'
test_check_get "media" '{"$NOR": [{"DSS::MDA::fs_type": "LTFS"}]}'
test_check_get "media" '{"$NOR": [{"DSS::MDA::address_type": "HASH1"}]}'
test_check_get "media" '{"$NOR": [{"DSS::MDA::fs_status": "blank"}]}'
test_check_get "media" '{"$XJSON": {"DSS::MDA::tags": "mytag"}}'

echo "**** TESTS: DSS_GET OBJECT ****"
test_check_get "object" 'all'
test_check_get "object" '{"$LIKE": {"DSS::OBJ::oid": "012%"}}'
test_check_get "object" '{"$NOT": {"$LIKE": {"DSS::OBJ::oid": "012%"}}}'
test_check_get "object" '{"$LIKE": {"DSS::OBJ::oid": "koéèê^!$£}[<>à@\\"}}'
test_check_get "object" \
   '{"$KVINJSON": {"DSS::OBJ::user_md": "test=abc"}}'

echo "**** TESTS: DSS_GET DEPRECATED_OBJECT ****"
test_check_get "deprec" 'all'
test_check_get "deprec" '{"$LIKE": {"DSS::OBJ::oid": "012%"}}'
test_check_get "deprec" '{"$NOT": {"$LIKE": {"DSS::OBJ::oid": "012%"}}}'
test_check_get "deprec" '{"$LIKE": {"DSS::OBJ::oid": "koéèê^!$£}[<>à@\\"}}'
test_check_get "deprec" '{"$KVINJSON": {"DSS::OBJ::user_md": "test=abc"}}'
test_check_get "deprec" '{"DSS::OBJ::deprec_time": "1970-01-01 12:34:56"}'

echo "**** TESTS: DSS_GET LAYOUT ****"
test_check_get "layout" 'all'
test_check_get "layout" '{"$INJSON": {"DSS::EXT::media_idx": "073221L6"}}'
test_check_get "layout" \
  '{"$NOT": {"$INJSON": {"DSS::EXT::media_idx": "073221L6"}}}'
test_check_get "layout" \
  '{"$INJSON": {"DSS::EXT::media_idx": "phobos1:/tmp/pho_testdir1"}}'
test_check_get "layout" '{"$INJSON": {"DSS::EXT::media_idx": "DOESNOTEXIST"}}'
test_check_get "layout" \
  '{"$INJSON": {"DSS::EXT::media_idx": "phobos1:/tmp/doesnotexist"}}'
test_check_get "layout" '{"DSS::EXT::oid": "QQQ6ASQDSQD"}'
test_check_get "layout" '{"$NOR": [{"DSS::EXT::oid": "QQQ6ASQDSQD"}]}'
test_check_get "layout" '{"$LIKE": {"DSS::EXT::oid": "Q%D"}}'
test_check_get "layout" '{"DSS::EXT::state": "pending"}'
test_check_get "layout" '{"DSS::EXT::layout_type": "simple"}'

echo "**** TEST: DSS_SET DEVICE ****"
test_check_set "device" "insert"
test_check_get "device" '{"$LIKE": {"DSS::DEV::serial": "%COPY%"}}'
test_check_get "device" '{"$NOT": {"$LIKE": {"DSS::DEV::serial": "%COPY%"}}}'
test_check_set "device" "update_adm_status"
test_check_get "device" '{"DSS::DEV::adm_status": "failed"}'
test_check_set "device" "delete"
test_check_get "device" '{"$LIKE": {"DSS::DEV::serial": "%COPY%"}}'
echo "**** TEST: DSS_SET MEDIA  ****"
test_check_set "media" "insert"
test_check_get "media" '{"$LIKE": {"DSS::MDA::id": "%COPY%"}}'
test_check_set "media" "update"
test_check_get "media" '{"$GT": {"DSS::MDA::nb_obj": "1002"}}'
test_check_get "media" '{"$NOT": {"$GT": {"DSS::MDA::nb_obj": "1002"}}}'
test_check_set "media" "delete"
test_check_get "media" '{"$LIKE": {"DSS::MDA::id": "%COPY%"}}'
echo "**** TEST: DSS_SET OBJECT AND LAYOUT ****"
test_check_set "object" "insert"
test_check_get "object" '{"$REGEXP": {"DSS::OBJ::oid": ".*COPY.*"}}'
test_check_get "object" '{"$NOT": {"$REGEXP": {"DSS::OBJ::oid": ".*COPY.*"}}}'
test_check_set "object" "update"
test_check_set "layout" "insert"
test_check_get "layout" '{"$LIKE": {"DSS::EXT::oid": "%COPY%"}}'
test_check_get "layout" '{"$NOT": {"$LIKE": {"DSS::EXT::oid": "%COPY%"}}}'
test_check_set "layout" "update"
test_check_get "layout" '{"$LIKE": {"DSS::EXT::oid": "%COPY%"}}'
test_check_set "object" "delete"
test_check_get "object" '{"$REGEXP": {"DSS::OBJ::oid": ".*COPY.*"}}'
test_check_get "object" '{"$NOT": {"$REGEXP": {"DSS::OBJ::oid": ".*COPY.*"}}}'
test_check_set "layout" "delete" "oidtest" "FAIL"
test_check_set "layout" "delete"
test_check_get "layout" '{"$LIKE": {"DSS::EXT::oid": "%COPY%"}}'
test_check_get "layout" '{"$NOT": {"$LIKE": {"DSS::EXT::oid": "%COPY%"}}}'

echo "**** TEST: DSS_SET DEPRECATED_OBJECT ****"
test_check_set "deprec" "insert"
test_check_get "deprec" '{"DSS::OBJ::version": "2"}'
test_check_get "deprec" '{"$NOT": {"DSS::OBJ::version": "2"}}'
test_check_set "deprec" "delete"
test_check_get "deprec" '{"DSS::OBJ::version": "2"}'

echo "**** TEST: DSS FILTER SYNTAX ERROR ****"
test_check_get "media" '{"DSS::MDA::idontexist": "foo"}' 0 'FAIL'

echo "**** TEST: DSS_DELETE OBJECT ****"
$PSQL << EOF
insert into deprecated_object (oid, uuid, version, user_md)
    values ('01230123ABC', '00112233445566778899aabbccddeeff', 1, '{}');
EOF

test_check_get "deprec" '{"DSS::OBJ::oid": "01230123ABC"}'

insert_examples

pid="$$"
echo "**** TESTS: DSS_DEVICE LOCK/UNLOCK  ****"
echo "*** TEST LOCK ***"
test_check_lock "device" "lock" "MY_LOCK" "$pid"
echo "*** TEST DOUBLE LOCK (EEXIST expected) ***"
test_check_lock "device" "lock" "MY_LOCK" "$pid" "FAIL"
echo "*** TEST UNLOCK ***"
test_check_lock "device" "unlock" "MY_LOCK" "$pid"
echo "*** TEST RELOCK ***"
test_check_lock "device" "lock" "MY_LOCK" "$pid"
echo "*** TEST UNLOCK BAD NAME ***"
test_check_lock "device" "unlock" "NOT_MY_LOCK" "$pid" "FAIL"
echo "*** TEST UNLOCK NO NAME ***"
test_check_lock "device" "unlock"

echo "**** TESTS: DSS_MEDIA LOCK/UNLOCK  ****"
echo "*** TEST LOCK ***"
test_check_lock "media" "lock" "MY_LOCK" "$pid"
echo "*** TEST DOUBLE LOCK (EEXIST expected) ***"
test_check_lock "media" "lock" "MY_LOCK" "$pid" "FAIL"
echo "*** TEST UNLOCK ***"
test_check_lock "media" "unlock" "MY_LOCK" "$pid"
echo "*** TEST DOUBLE UNLOCK (ENOLCK expected) ***"
test_check_lock "media" "unlock" "MY_LOCK" "$pid" "FAIL"
echo "*** TEST RELOCK ***"
test_check_lock "media" "lock" "MY_LOCK" "$pid"
echo "*** TEST UNLOCK BAD NAME ***"
test_check_lock "media" "unlock" "NOT_MY_LOCK" "$pid" "FAIL"
echo "*** TEST UNLOCK NO NAME ***"
test_check_lock "media" "unlock"

echo "*** TEST END ***"
# Uncomment if you want the db to persist after test
# trap - EXIT ERR
