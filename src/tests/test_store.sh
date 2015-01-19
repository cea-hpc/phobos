#!/bin/bash
set -e

TEST_MNT=/tmp/tape0
TEST_RAND=/tmp/RAND_$$
TEST_FILES="/etc/redhat-release /etc/passwd /etc/group /etc/hosts $TEST_RAND"

test_bin_dir=$(dirname $(readlink -m $0))
test_put=$test_bin_dir/test_put

# interpreted in phobos core (if built with _TEST flag)
export PHO_TEST_MNT=$TEST_MNT
export PHO_TEST_ADDR_TYPE="hash"

function error
{
    echo "ERROR: $*" >&2
    exit 1
}

function clean_test
{
    echo "cleaning..."
    rm -f "$TEST_RAND"
    rm -rf "$TEST_MNT/*"
}

trap clean_test ERR

function test_check_put
{
    local src_file=$(readlink -m $1)
    local name=$(echo $src_file | tr './!<>{}#"''' '_')

    $test_put $src_file || error "Put failure"
    out=$(find $TEST_MNT -type f -name "*$name*")
    echo "-> $out"
    [ -z "$out" ] && error "*$name* not found in backend"
    diff -q $src_file $out && echo "$src_file: contents OK"

    id=$(getfattr --only-values --absolute-names -n "user.id" $out)
    [ -z "$id" ] && error "saved file has no 'id' xattr"
    [ $id != $src_file ] && error "unexpected id value in xattr"

    umd=$(getfattr --only-values --absolute-names -n "user.user_md" $out)
    [ -z "$umd" ] && error "saved file has no 'user_md' xattr"

    true
}


[ ! -d $TEST_MNT ] && mkdir /tmp/tape0
rm -rf /tmp/tape0/*

# create test file in /tmp
dd if=/dev/urandom of=$TEST_RAND bs=1M count=10

for f in $TEST_FILES; do
    test_check_put "$f"
done

clean_test || true
