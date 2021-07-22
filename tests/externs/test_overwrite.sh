#!/bin/bash

#
#  All rights reserved (c) 2014-2021 CEA/DAM.
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

#
# Integration test for overwrite feature
#

set -xe

test_dir=$(dirname $(readlink -e $0))
. $test_dir/../test_env.sh
. $test_dir/../setup_db.sh
. $test_dir/../test_launch_daemon.sh

function dir_setup
{
    export dirs="
        $(mktemp -d /tmp/test.pho.XXXX)
        $(mktemp -d /tmp/test.pho.XXXX)
    "
    echo "adding directories $dirs"
    $phobos dir add $dirs
    $phobos dir format --fs posix --unlock $dirs
}

function cleanup
{
    waive_daemon
    rm -rf $dirs
    drop_tables
}

function output_checkout
{
    # $1 = type of item to list
    # $2 = output to show
    # $3 = oid to show
    # $4 = expected result of the output
    # $5 = check deprecated objects or not
    # The parameter $5 is only used when checking the version numbers.
    # Otherwise it isn't specified, i.e. empty.
    local result=$($phobos $1 list $5 --output $2 $3)

    if [ "$result" != "$4" ]; then
        error "$1 $3 should have $2 : $4"
    fi
}

function put_checkout
{
    if [ "$2" == *"--overwrite"* ]; then
        local action="overwritten"
    else
        local action="created"
    fi

    $phobos put $2 /etc/hosts $1 ||
        error "Object $1 should have been $action : " \
              "phobos put $2 /etc/hosts $1"
}

function test_overwrite_and_delete
{
    local output_func="output_checkout object version oid1"
    local put_func="put_checkout oid1"

    echo "**** INSERTING AND OVERWRITING OBJECT OID1 ****"
    $put_func
    $put_func "--overwrite"

    $output_func "1" "--deprecated"
    $output_func "2"

    echo "**** DELETING OBJECT OID1 ****"
    $phobos delete oid1 ||
        error "Object oid1 should be deleted"

    local expect=$(printf "1\n2")
    $output_func "$expect" "--deprecated"

    return 0
}

function test_double_overwrite
{
    local output_func="output_checkout object version oid2"
    local put_func="put_checkout oid2"

    echo "**** INSERTING BY OVERWRITING OBJECT OID2 AND DOUBLE OVERWRITE ****"
    $put_func "--overwrite"

    $output_func "1"

    $put_func "--overwrite"

    $output_func "1" "--deprecated"
    $output_func "2"

    $put_func "--overwrite"

    local expect=$(printf "1\n2")
    $output_func "$expect" "--deprecated"
    $output_func "3"

    return 0
}

function test_overwrite_metadata
{
    local output_func="output_checkout object user_md oid3"
    local put_func="put_checkout oid3"

    echo "**** INSERTING OBJECT OID3 AND OVERWRITING METADATA ****"
    $put_func "--metadata a=b"

    $output_func '{"a": "b"}'

    $put_func "--overwrite"

    $output_func '{"a": "b"}' "--deprecated"
    $output_func '{"a": "b"}'

    $put_func "--metadata c=d --overwrite"

    local expect=$(printf '{"a": "b"}\n{"a": "b"}')
    $output_func "$expect" "--deprecated"
    $output_func '{"c": "d"}'

    return 0
}

function test_overwrite_lyt_params
{
    local output_func="output_checkout extent ext_count oid4"
    local put_func="put_checkout oid4"

    echo "**** INSERTING OBJECT OID4 AND OVERWRITING LAYOUT ****"
    $put_func "--lyt-params repl_count=1"

    $output_func "1"

    $put_func "--lyt-params repl_count=1 --overwrite"

    local expect=$(printf "1\n1")
    $output_func "$expect"

    $put_func "--lyt-params repl_count=2 --overwrite"

    local expect=$(printf "1\n1\n2")
    $output_func "$expect"

    $put_func "--lyt-params repl_count=1 --overwrite"

    local expect=$(printf "1\n1\n2\n1")
    $output_func "$expect"

    return 0
}

function test_overwrite_family
{
    $phobos drive add --unlock /dev/st0 ||
        error "Drive /dev/st0 should have been added"

    $phobos tape add -t lto5 P00000L5 ||
        error "Tape P00000L5 should have been added"
    $phobos tape format --unlock P00000L5 ||
        error "Tape P00000L5 should have been formated"

    local output_func="output_checkout extent family oid5"
    local put_func="put_checkout oid5"

    echo "**** INSERTING OBJECT OID5 AND OVERWRITING FAMILY ****"
    $put_func

    $output_func "['dir']"

    $put_func "--overwrite"

    local expect=$(printf "['dir']\n['dir']")
    $output_func "$expect"

    $put_func "--family tape --overwrite"

    local expect=$(printf "['dir']\n['dir']\n['tape']")
    $output_func "$expect"

    $put_func "--overwrite"

    local expect=$(printf "['dir']\n['dir']\n['tape']\n['dir']")
    $output_func "$expect"

    return 0
}

export PHOBOS_STORE_default_family="dir"

trap drop_tables ERR EXIT
drop_tables
setup_tables
trap cleanup ERR EXIT
invoke_daemon
dir_setup
test_overwrite_and_delete
test_double_overwrite
test_overwrite_metadata
test_overwrite_lyt_params

# Tape tests are available only if /dev/changer exists, which is the entry
# point for the tape library.
if [[ -w /dev/changer ]]; then
    test_overwrite_family
fi
