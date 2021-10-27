# -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
# vim:expandtab:shiftwidth=4:tabstop=4:

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

test_bin_dir=$(dirname $(readlink -e $0))
test_bin="$test_bin_dir/test_store"

. $test_bin_dir/../../test_env.sh
. $test_bin_dir/setup_db.sh
. $test_bin_dir/test_launch_daemon.sh

################################################################################
#                                    SETUP                                     #
################################################################################

TEST_RECOV_DIR=/tmp/phobos_recov.$$

echo "**** POSIX TEST MODE ****"
# following entries must match mount prefix
TEST_MNT="/tmp/pho_testdir1 /tmp/pho_testdir2 /tmp/pho_testdir3 \
          /tmp/pho_testdir4 /tmp/pho_testdir5"

export PHOBOS_LRS_mount_prefix=/tmp/pho_testdir
export PHOBOS_LRS_families="dir"

export PHOBOS_STORE_default_family="dir"

function init()
{
    drop_tables
    setup_tables
    insert_examples
    rm -rf $TEST_RECOV_DIR
    mkdir $TEST_RECOV_DIR
}

function clear_mnt_content()
{
    for d in $TEST_MNT; do
        rm -rf $d/*
    done

}

function clean_test()
{
    drop_tables
    waive_daemon
    rm -rf "$TEST_FILES"
    rm -rf "$TEST_RECOV_DIR"
    for d in $TEST_MNT; do
        rm -rf $d
    done
}

function create_files()
{
    rm -rf "$TEST_FILES"
    clear_mnt_content

    TEST_RAND=/tmp/RAND_$$_$1
    dd if=/dev/urandom of=$TEST_RAND bs=1M count=10

    TEST_IN="/etc/redhat-release /etc/passwd /etc/group /etc/hosts"
    TEST_FILES=""
    for f in $TEST_IN; do
        new=/tmp/$(basename $f).$$_$1
        /bin/cp -p $f $new
        TEST_FILES="$TEST_FILES $new"
    done
    TEST_FILES="$TEST_FILES $TEST_RAND"
}

trap clean_test ERR EXIT

init
invoke_daemon

for dir in $TEST_MNT; do
    # allow later cleaning by other users
    umask 000
    mkdir -p "$dir"
done

################################################################################
#                   TEST PUTTING OBJECTS AND CHECK THE RESULTS                 #
################################################################################

function test_check_put() # verb, source_file
{
    local verb=$1

    local src_files=()
    local i=0
    for f in "${@:2}"
    do
        src_files[$i]=$(readlink -m $f)
        i=$(($i+1))
    done

    $LOG_COMPILER $test_bin $verb "${src_files[@]}"
    if [[ $? != 0 ]]; then
        error "Failed to $verb ${src_files[@]}"
    fi

    for f in ${src_files[@]}
    do
        local name=$(echo $f | tr './!<>{}#"' '_')

        # check that the extent is found in the storage backend
        local out=$(find $TEST_MNT -type f -name "*${name}_$verb*" | tail -n 1)
        [ -z "$out" ] && error "*$f* not found in backend"
        diff -q $f $out && echo "$f: contents OK"

        # check the 'id' xattr attached to this extent
        local id=$(getfattr --only-values --absolute-names -n "user.id" $out)
        [ -z "$id" ] && error "saved file has no 'id' xattr"
        [ $id != "${f}_$verb" ] && error "unexpected id value in xattr"

        # check user_md xattr attached to this extent
        local umd=$(getfattr --only-values --absolute-names \
                    -n "user.user_md" $out)
        [ -z "$umd" ] && error "saved file has no 'user_md' xattr"
    done

    true
}

################################################################################
#                       TEST RETRIEVING THE GIVEN EXTENT                       #
################################################################################

function test_check_get()
{
    local arch=$1

    # get the id from the extent
    id=$(getfattr --only-values --absolute-names -n "user.id" $arch)
    [ -z "$id" ] && error "saved file has no 'id' xattr"

    # split the path into <mount_point> / <relative_path>
    # however, the mount_point may not be real in the case of
    # a "directory device" (e.g. subdirectory of an existing filesystem)
    # XXXX the following code relies on the fact mount points have depth 2...
    tgt="$TEST_RECOV_DIR/$id"
    mkdir -p $(dirname "$tgt")

    $LOG_COMPILER $test_bin get "$id" "$tgt"

    diff -q "$arch" "$tgt"

    rm -f $tgt
}

################################################################################
#                          TEST PUT ON SPECIFIC MEDIA                          #
################################################################################

function test_put_tag()
{
    for f in $TEST_FILES; do
        $LOG_COMPILER $test_bin tag-put $f no-such-tag && \
            error "tag-put on a media with tag 'no-such-tag' " \
                  "should have failed"
        $LOG_COMPILER $test_bin tag-put $f mytag no-such-tag && \
            error "tag-put on a media with tag 'mytag' and 'no-such-tag' " \
                  "should have failed"

        # Ensure the right directory is chosen for this tag
        $LOG_COMPILER $test_bin tag-put $f mytag |& tee /dev/stderr | grep -e \
            "/tmp/pho_testdir[245]"
    done
}

################################################################################
#                            TEST WITH PUT/GET/LIST                            #
################################################################################

function test_put_get()
{
    if [ "$1" == "put" ]; then
        for f in $TEST_FILES; do
            test_check_put "put" "$f"
        done
    else
        test_check_put "mput" $TEST_FILES
    fi

    # retrieve all files from the backend, get and check them
    find $TEST_MNT -type f | while read f; do
        test_check_get "$f"
    done

    # check that object info can be retrieved using phobos_store_object_list()
    $LOG_COMPILER $test_bin list "_$1" $TEST_FILES
}

################################################################################
#                              MAIN TEST ROUTINE                               #
################################################################################

function test_routine()
{
    test_put_tag

    test_put_get "put"

    test_put_get "mput"
}

create_files "base"
test_routine

export PHOBOS_STORE_default_layout="raid1"
create_files "raid1"
test_routine

export PHOBOS_LAYOUT_RAID1_repl_count=1
create_files "raid1_1"
test_routine

export PHOBOS_LAYOUT_RAID1_repl_count=3
create_files "raid1_3"
test_routine
