#!/bin/bash

#
#  All rights reserved (c) 2014-2022 CEA/DAM.
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
# Integration test for put commands
#

set -xe

test_dir=$(dirname $(readlink -e $0))
. $test_dir/../../test_env.sh
. $test_dir/../../setup_db.sh
. $test_dir/../../test_launch_daemon.sh
. $test_dir/../../tape_drive.sh

################################################################################
#                                    SETUP                                     #
################################################################################

function dir_setup
{
    empty_put_dir1=$(mktemp -d /tmp/test.pho.XXXX)
    empty_put_dir2=$(mktemp -d /tmp/test.pho.XXXX)
    dirs="$empty_put_dir1 $empty_put_dir2"

    echo "adding directories $dirs"
    $phobos dir add $dirs
    $phobos dir format --fs posix --unlock $dirs
    $phobos dir update --tags empty_put_dir1 $empty_put_dir1
    $phobos dir update --tags empty_put_dir2 $empty_put_dir2
}

function setup
{
    export PHOBOS_STORE_default_family="dir"

    setup_tables
    invoke_daemon
    dir_setup
}

function cleanup
{
    waive_daemon
    rm -rf $dirs
    drop_tables

    if [[ -w /dev/changer ]]; then
        drain_all_drives
    fi
}

trap cleanup EXIT
setup

################################################################################
#                               SIMPLE PUT TESTS                               #
################################################################################

function test_extent_path
{
    $valg_phobos put --family dir /etc/hosts oid1 ||
        error "Object should be put"
    $valg_phobos put --family dir --layout raid1 /etc/hosts oid2 ||
        error "Object should be put"

    # some characters are removed to get a clean extent
    # "['blablabla', 'blobloblo']" -> "blablabla" and "blobloblo"
    addr_ext1=$($phobos extent list --output address oid1 | \
                tr -d ' ' | tr -d \' | tr -d [ | tr -d ])
    addr_ext2a=$($phobos extent list --output address oid2 | cut -d, -f1 | \
                tr -d ' ' | tr -d \' | tr -d [ | tr -d ])
    addr_ext2b=$($phobos extent list --output address oid2 | cut -d, -f2 | \
                tr -d ' ' | tr -d \' | tr -d [ | tr -d ])

    uuid_ext1=$($phobos object list --output uuid oid1)
    uuid_ext2=$($phobos object list --output uuid oid2)
    ver_ext1=$($phobos object list --output version oid1)
    ver_ext2=$($phobos object list --output version oid2)

    [[ "$addr_ext1" =~ "/oid1." ]] ||
        error "oid1 extent path should contain object id"
    [[ "$addr_ext2a" =~ "/oid2." ]] ||
        error "oid2 extent path (pt1) should contain object id"
    [[ "$addr_ext2b" =~ "/oid2." ]] ||
        error "oid2 extent path (pt2) should contain object id"
    [[ "$addr_ext1" =~ ".$uuid_ext1"$ ]] ||
        error "oid1 extent path should contain object uuid"
    [[ "$addr_ext2a" =~ ".$uuid_ext2"$ ]] ||
        error "oid2 extent path (pt1) should contain object uuid"
    [[ "$addr_ext2b" =~ ".$uuid_ext2"$ ]] ||
        error "oid2 extent path (pt2) should contain object uuid"
    [[ "$addr_ext1" =~ ".${ver_ext1}." ]] ||
        error "oid1 extent path should contain object version"
    [[ "$addr_ext2a" =~ ".${ver_ext2}." ]] ||
        error "oid2 extent path (pt1) should contain object version"
    [[ "$addr_ext2b" =~ ".${ver_ext2}." ]] ||
        error "oid2 extent path (pt2) should contain object version"
}

test_extent_path

################################################################################
#                         TEST EMPTY PUT ON TAGGED DIR                         #
################################################################################

function test_empty_put
{
    touch /tmp/empty_file1 /tmp/empty_file2

    $valg_phobos put --tags empty_put_dir1 --family dir \
        /tmp/empty_file1 empty_file1
    $valg_phobos put --tags empty_put_dir2 --family dir \
        /tmp/empty_file2 empty_file2

    rm /tmp/empty_file1 /tmp/empty_file2

    output=$($phobos extent list -o oid,media_name)
    echo "$output" | grep "empty_file1" | grep "$empty_put_dir1"
    echo "$output" | grep "empty_file2" | grep "$empty_put_dir2"
}

test_empty_put

################################################################################
#                         PUT WITH --LYT-PARAMS OPTION                         #
################################################################################

function lyt_params_helper
{
    $valg_phobos put --family dir $2 /etc/hosts $1
    result=$($phobos extent list --output layout,ext_count $1 | grep "raid1")
    ext_count=$(echo "$result" | cut -d'|' -f3 | xargs)
    expect=$3
    if [ $(($ext_count)) -ne $expect ]; then
        error "Put with arguments '$2' should have used raid1 layout "
              "and $3 extent(s)"
    fi
}

function test_lyt_params
{
    lyt_params_helper "lp1" "" 1
    lyt_params_helper "lp2" "--layout raid1" 2
    lyt_params_helper "lp3" "--lyt-params repl_count=2" 2
    lyt_params_helper "lp4" "--layout raid1 --lyt-params repl_count=2" 2
    lyt_params_helper "lp5" "--layout raid1 --lyt-params repl_count=1" 1
}

test_lyt_params

################################################################################
#                         PUT WITH --OVERWRITE OPTION                          #
################################################################################

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

    $valg_phobos put $2 /etc/hosts $1 ||
        error "Object $1 should have been $action : " \
              "phobos put $2 /etc/hosts $1"
}

function test_overwrite_and_delete
{
    local output_func="output_checkout object version oid3"
    local put_func="put_checkout oid3"

    echo "**** INSERTING AND OVERWRITING OBJECT OID3 ****"
    $put_func
    $put_func "--overwrite"

    $output_func "1" "--deprecated"
    $output_func "2"

    echo "**** DELETING OBJECT OID3 ****"
    $phobos delete oid3 || error "Object oid3 should be deleted"

    local expect=$(printf "1\n2")
    $output_func "$expect" "--deprecated"
}

function test_double_overwrite
{
    local output_func="output_checkout object version oid4"
    local put_func="put_checkout oid4"

    echo "**** INSERTING BY OVERWRITING OBJECT OID4 AND DOUBLE OVERWRITE ****"
    $put_func "--overwrite"

    $output_func "1"

    $put_func "--overwrite"

    $output_func "1" "--deprecated"
    $output_func "2"

    $put_func "--overwrite"

    local expect=$(printf "1\n2")
    $output_func "$expect" "--deprecated"
    $output_func "3"
}

function test_overwrite_metadata
{
    local output_func="output_checkout object user_md oid5"
    local put_func="put_checkout oid5"

    echo "**** INSERTING OBJECT OID5 AND OVERWRITING METADATA ****"
    $put_func "--metadata a=b"

    $output_func '{"a": "b"}'

    $put_func "--overwrite"

    $output_func '{"a": "b"}' "--deprecated"
    $output_func '{"a": "b"}'

    $put_func "--metadata c=d --overwrite"

    local expect=$(printf '{"a": "b"}\n{"a": "b"}')
    $output_func "$expect" "--deprecated"
    $output_func '{"c": "d"}'
}

function test_overwrite_lyt_params
{
    local output_func="output_checkout extent ext_count oid6"
    local put_func="put_checkout oid6"

    echo "**** INSERTING OBJECT OID6 AND OVERWRITING LAYOUT ****"
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
}

function test_overwrite_family
{
    drain_all_drives

    $phobos drive add --unlock /dev/st0 ||
        error "Drive /dev/st0 should have been added"

    $phobos tape add -t lto5 P00000L5 ||
        error "Tape P00000L5 should have been added"
    $phobos tape format --unlock P00000L5 ||
        error "Tape P00000L5 should have been formated"

    local output_func="output_checkout extent family oid7"
    local put_func="put_checkout oid7"

    echo "**** INSERTING OBJECT OID7 AND OVERWRITING FAMILY ****"
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
}

test_overwrite_and_delete
test_double_overwrite
test_overwrite_metadata
test_overwrite_lyt_params

# Tape tests are available only if /dev/changer exists, which is the entry
# point for the tape library.
if [[ -w /dev/changer ]]; then
    test_overwrite_family
fi
