#!/bin/bash

#
#  All rights reserved (c) 2014-2024 CEA/DAM.
#
#  This file is part of Phobos.
#
#  Phobos is free software: you can redistribute it and/or modify it under
#  the terms of the GNU Lesser General Public License as published by
#  the Free Software Foundation, either version 2.1 of the Licence, or
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

#
# Integration test for get commands
#

test_dir=$(dirname $(readlink -e $0))
. $test_dir/../../test_env.sh
. $test_dir/../../setup_db.sh
. $test_dir/../../test_launch_daemon.sh

set -xe

function dir_setup
{
    dir="$(mktemp -d /tmp/test.pho.XXXX)"
    echo "adding directory $dir"
    $phobos dir add $dir
    $phobos dir format --fs posix --unlock $dir
}

function setup
{
    setup_tables
    invoke_lrs
    dir_setup
}

function cleanup
{
    waive_lrs
    drop_tables
    rm -rf $dir
    rm -f /tmp/out
}

function test_get
{
    $phobos put --family dir /etc/hosts oid1

    local uuid=$($phobos object list --output uuid oid1)

    $valg_phobos get oid1 /tmp/out || error "Get operation failed"
    rm /tmp/out

    $valg_phobos get --version 1 oid1 /tmp/out || error "Get operation failed"
    rm /tmp/out

    $valg_phobos get --uuid $uuid oid1 /tmp/out || error "Get operation failed"
    rm /tmp/out

    $valg_phobos get --version 1 --uuid $uuid oid1 /tmp/out \
        || error "Get operation failed"
    rm /tmp/out

    $phobos delete oid1

    $valg_phobos get oid1 /tmp/out \
        && error "Get operation should have failed" || true
    $valg_phobos get --version 1 oid1 /tmp/out || error "Get operation failed"
    rm /tmp/out

    $valg_phobos get --uuid $uuid oid1 /tmp/out || error "Get operation failed"
    rm /tmp/out
}

function test_creation_and_access_times
{
    $phobos put --family dir /etc/hosts oid-time
    $phobos put --family dir --overwrite /etc/hosts oid-time
    $phobos put --family dir --overwrite /etc/hosts oid-time

    local uuid=$($phobos object list --output uuid oid-time)

    local ctm3=$($phobos object list --output creation_time oid-time)
    ctm3=$(date -d "$ctm3" +"%s")
    local ctm2=$($phobos object list --format csv --deprecated --output \
                 version,creation_time oid-time | grep "2," | cut -d',' -f2)
    ctm2=$(date -d "$ctm2" +"%s")
    local ctm1=$($phobos object list --format csv --deprecated --output \
                 version,creation_time oid-time | grep "1," | cut -d',' -f2)
    ctm1=$(date -d "$ctm1" +"%s")

    local atm3=$($phobos object list --output access_time oid-time)
    atm3=$(date -d "$atm3" +"%s")
    local atm2=$($phobos object list --format csv --deprecated --output \
                 version,access_time oid-time | grep "2," | cut -d',' -f2)
    atm2=$(date -d "$atm2" +"%s")
    local atm1=$($phobos object list --format csv --deprecated --output \
                 version,access_time oid-time | grep "1," | cut -d',' -f2)
    atm1=$(date -d "$atm1" +"%s")

    [ $ctm1 -le $ctm2 ] \
        || error "Creation time of 1st version is greater than 2nd version one"
    [ $ctm2 -le $ctm3 ] \
        || error "Creation time of 2nd version is greater than 3rd version one"
    [ $atm1 -le $atm2 ] \
        || error "Access time of 1st version is greater than 2nd version one"
    [ $atm2 -le $atm3 ] \
        || error "Access time of 2nd version is greater than 3rd version one"

    sleep 1
    $valg_phobos get --uuid $uuid --version 1 oid-time /tmp/out
    rm /tmp/out

    local atm=$($phobos object list --format csv --deprecated --output \
                version,access_time oid-time | grep "1," | cut -d',' -f2)
    atm=$(date -d "$atm" +"%s")
    [ $atm1 -lt $atm ] \
        || error "Access time of 1st version was not updated"
    atm1=$atm

    atm=$($phobos object list --format csv --deprecated --output \
          version,access_time oid-time | grep "2," | cut -d',' -f2)
    atm=$(date -d "$atm" +"%s")
    [ $atm2 -eq $atm ] \
        || error "Access time of 2nd version was updated, only 1st should be"

    atm=$($phobos object list --output access_time oid-time)
    atm=$(date -d "$atm" +"%s")
    [ $atm3 -eq $atm ] \
        || error "Access time of 3rd version was updated, only 1st should be"

    $valg_phobos get oid-time /tmp/out
    rm /tmp/out

    atm=$($phobos object list --format csv --deprecated --output \
                version,access_time oid-time | grep "1," | cut -d',' -f2)
    atm=$(date -d "$atm" +"%s")
    [ $atm1 -eq $atm ] \
        || error "Access time of 1st version was updated, only 3rd should be"

    atm=$($phobos object list --format csv --deprecated --output \
          version,access_time oid-time | grep "2," | cut -d',' -f2)
    atm=$(date -d "$atm" +"%s")
    [ $atm2 -eq $atm ] \
        || error "Access time of 2nd version was updated, only 3rd should be"

    atm=$($phobos object list --output access_time oid-time)
    atm=$(date -d "$atm" +"%s")
    [ $atm3 -lt $atm ] \
        || error "Access time of 3rd version was not updated"
    atm3=$atm

    $valg_phobos get --uuid $uuid --version 2 oid-time /tmp/out
    rm /tmp/out

    atm=$($phobos object list --format csv --deprecated --output \
                version,access_time oid-time | grep "1," | cut -d',' -f2)
    atm=$(date -d "$atm" +"%s")
    [ $atm1 -eq $atm ] \
        || error "Access time of 1st version was updated, only 2nd should be"

    atm=$($phobos object list --format csv --deprecated --output \
          version,access_time oid-time | grep "2," | cut -d',' -f2)
    atm=$(date -d "$atm" +"%s")
    [ $atm2 -lt $atm ] \
        || error "Access time of 2nd version was not updated"
    atm2=$atm

    atm=$($phobos object list --output access_time oid-time)
    atm=$(date -d "$atm" +"%s")
    [ $atm3 -eq $atm ] \
        || error "Access time of 3rd version was updated, only 2nd should be"

    sleep 1
    $phobos del oid-time
    atm=$($phobos object list --format csv --deprecated --output \
          version,access_time oid-time | grep "3," | cut -d',' -f2)
    atm=$(date -d "$atm" +"%s")
    [ $atm3 -eq $atm ] \
        || error "Access time should not be updated on delete operation"

    $phobos undel oid oid-time
    atm=$($phobos object list --output access_time oid-time)
    atm=$(date -d "$atm" +"%s")
    [ $atm3 -eq $atm ] \
        || error "Access time should not be updated on undelete operation"
}

function test_errors
{
    $valg_phobos get oid2 /tmp/out \
        && error "Get operation should fail on invalid oid" || true

    $valg_phobos get --uuid fake_uuid oid1 /tmp/out \
        && error "Get operation should fail on invalid uuid" || true

    $phobos put --family dir /etc/hosts oid1
    $valg_phobos get --version 5 oid1 /tmp/out \
        && error "Get operation should fail on invalid version" || true

    $phobos delete oid1
    $valg_phobos get --version 5 oid1 /tmp/out \
        && error "Get operation should fail on invalid version" || true

    $PSQL << EOF
UPDATE object SET obj_status = 'incomplete' WHERE oid = 'oid1';
EOF
    $valg_phobos get oid1 /tmp/out &&
        error "Incomplete status of object should have failed the test" || true
}

trap cleanup EXIT
setup

test_get
test_creation_and_access_times
test_errors
