#!/bin/bash

#
#  All rights reserved (c) 2014-2017 CEA/DAM.
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

# test posix mode

set -e

USE_LAYOUT=${1:-simple}

TEST_RAND=/tmp/RAND_$$
TEST_RECOV_DIR=/tmp/phobos_recov.$$

TEST_IN="/etc/redhat-release /etc/passwd /etc/group /etc/hosts"
TEST_FILES=""
for f in $TEST_IN; do
    new=/tmp/$(basename $f).$$
    /bin/cp -p $f $new
    TEST_FILES="$TEST_FILES $new"
done
TEST_FILES="$TEST_FILES $TEST_RAND"

test_bin_dir=$(dirname $(readlink -e $0))
test_bin="$test_bin_dir/test_store"

. $test_bin_dir/test_env.sh
. $test_bin_dir/setup_db.sh

drop_tables
setup_tables
insert_examples

echo "**** POSIX TEST MODE ****"
# following entries must match mount prefix
TEST_MNT="/tmp/pho_testdir1 /tmp/pho_testdir2 /tmp/pho_testdir3"
TEST_FS="posix"

export PHOBOS_LRS_mount_prefix=/tmp/pho_testdir
export PHOBOS_LRS_default_family="dir"

export PHOBOS_STORE_layout=$USE_LAYOUT

function clean_test
{
    echo "cleaning..."
    rm -f $TEST_FILES
    for d in $TEST_MNT; do
    rm -rf $d/*
    done
    rm -rf "$TEST_RECOV_DIR"
}

function error
{
    echo "ERROR: $*" >&2
    clean_test
    exit 1
}

trap clean_test ERR EXIT

# put a file into phobos object store
function test_check_put # verb, source_file, expect_failure
{
    local verb=$1
    # Is the test expected to fail?
    local fail=$2

    local src_files=()
    local i=0
    for f in "${@:3}"
    do
        src_files[$i]=$(readlink -m $f)
        i=$(($i+1))
    done

    echo $test_bin $verb "${src_files[@]}"
    $test_bin $verb "${src_files[@]}"
    if [[ $? != 0 ]]; then
        if [ "$fail" != "yes" ]; then
            error "$verb failure"
        else
            echo "OK: $verb failed as expected"
            return 1
        fi
    fi

    for f in ${src_files[@]}
    do
        local name=$(echo $f | tr './!<>{}#"' '_')

        # check that the extent is found into the storage backend
        local out=$(find $TEST_MNT -type f -name "*$name*" | head -n 1)
        echo -e " \textent: $out"
        [ -z "$out" ] && error "*$name* not found in backend"
        diff -q $f $out && echo "$f: contents OK"

        # check the 'id' xattr attached to this extent
        local id=$(getfattr --only-values --absolute-names -n "user.id" $out)
        [ -z "$id" ] && error "saved file has no 'id' xattr"
        [ $id != $f ] && error "unexpected id value in xattr"

        # check user_md xattr attached to this extent
        local umd=$(getfattr --only-values --absolute-names \
                    -n "user.user_md" $out)
        [ -z "$umd" ] && error "saved file has no 'user_md' xattr"
    done

    true
}

# retrieve the given extent using phobos_get()
function test_check_get # extent_path
{
    local arch=$1

    # get the id from the extent
    id=$(getfattr --only-values --absolute-names -n "user.id" $arch)
    [ -z "$id" ] && error "saved file has no 'id' xattr"

    # split the path into <mount_point> / <relative_path>
    # however, the mount_point may not be real in the case of
    # a "directory device" (e.g. subdirectory of an existing filesystem)
    # XXXX the following code relies on the fact mount points have depth 2...
    dir=$(echo "$arch" | cut -d '/' -f 1-3)
    addr=$(echo "$arch" | cut -d '/' -f 4-)
    size=$(stat -c '%s' "$arch")
    echo "address=$addr,size=$size"

    tgt="$TEST_RECOV_DIR/$id"
    mkdir -p $(dirname "$tgt")

    $test_bin get "$id" "$tgt"

    diff -q "$arch" "$tgt"

    rm -f $tgt
}

function assert_fails # program args ...
{
    local rc=0
    # Call program
    "$*" || rc=$?

    # Check
    if [[ "$rc" != 0 ]]; then
        return 0
    else
        return 1
    fi
}

if [[ $TEST_FS == "posix" ]]; then
    for dir in $TEST_MNT; do
        # clean and re-create
        rm -rf $dir/*
        # allow later cleaning by other users
        umask 000
        [ ! -d $dir ] && mkdir -p "$dir"
    done
else
    # clean the contents
    for dir in $TEST_MNT; do
        rm -rf  $dir/*
    done
fi
[ ! -d $TEST_RECOV_DIR ] && mkdir -p "$TEST_RECOV_DIR"
rm -rf "$TEST_RECOV_DIR/"*

# create test file in /tmp
dd if=/dev/urandom of=$TEST_RAND bs=1M count=10


echo
echo "**** TESTS: PUT WITH TAGS ****"
for f in $TEST_FILES; do
    assert_fails $test_bin tag-put $f no-such-tag
    assert_fails $test_bin tag-put $f mytag no-such-tag
    # Ensure the right directory is chosen for this tag
    $test_bin tag-put $f mytag |& tee /dev/stderr | grep /tmp/pho_testdir2
done

drop_tables
setup_tables
insert_examples

echo
echo "**** TESTS: PUT ****"
for f in $TEST_FILES; do
    test_check_put "put" "no" "$f"
    test_check_put "put" "yes" "$f" && error "second PUT should fail"
done


echo
echo "**** TESTS: GET ****"
# retrieve all files from the backend, get and check them
find $TEST_MNT -type f | while read f; do
    test_check_get "$f"
done

# Clean inserted files and DSS entries so that we can reinsert them using MPUT
find $TEST_MNT -type f -delete
rm -rf ${TEST_RECOV_DIR}
mkdir ${TEST_RECOV_DIR}
drop_tables
setup_tables
insert_examples

echo
echo "**** TESTS: MPUT ****"
test_check_put "mput" "no" $TEST_FILES

echo
echo "**** TESTS: GET ****"
# retrieve all files from the backend, get and check them
find $TEST_MNT -type f | while read f; do
    test_check_get "$f"
done

# exit normally, clean TRAP
trap - EXIT ERR
clean_test || true

# -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
# vim:expandtab:shiftwidth=4:tabstop=4:
