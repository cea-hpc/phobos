#!/bin/bash
set -e

TEST_MNT=/tmp/tape0 # warning hardcoded in LRS
TEST_RAND=/tmp/RAND_$$
TEST_RECOV_DIR=/tmp/phobos_recov.$$
TEST_FILES="/etc/redhat-release /etc/passwd /etc/group /etc/hosts $TEST_RAND"

test_bin_dir=$(dirname $(readlink -m $0))
test_put="$test_bin_dir/test_store put"
test_get="$test_bin_dir/test_store get"

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
    rm -rf "$TEST_MNT"
    rm -rf "$TEST_RECOV_DIR"
}

trap clean_test ERR

function test_check_put
{
    local src_file=$(readlink -m $1)
    local name=$(echo $src_file | tr './!<>{}#"''' '_')

    $test_put $src_file || error "Put failure"
    out=$(find $TEST_MNT -type f -name "*$name*")
    echo -e " \textent: $out"
    [ -z "$out" ] && error "*$name* not found in backend"
    diff -q $src_file $out && echo "$src_file: contents OK"

    id=$(getfattr --only-values --absolute-names -n "user.id" $out)
    [ -z "$id" ] && error "saved file has no 'id' xattr"
    [ $id != $src_file ] && error "unexpected id value in xattr"

    umd=$(getfattr --only-values --absolute-names -n "user.user_md" $out)
    [ -z "$umd" ] && error "saved file has no 'user_md' xattr"

    true
}

function test_check_get
{
    local arch=$1

    # get the id from the archive
    id=$(getfattr --only-values --absolute-names -n "user.id" $arch)
    [ -z "$id" ] && error "saved file has no 'id' xattr"

    tgt="$TEST_RECOV_DIR/$id"
    mkdir -p $(dirname "$tgt")

    $test_get "$id" "$tgt"

    diff -q "$arch" "$tgt"
}

[ ! -d $TEST_MNT ] && mkdir -p "$TEST_MNT"
[ ! -d $TEST_RECOV_DIR ] && mkdir -p "$TEST_RECOV_DIR"
rm -rf "$TEST_MNT/"*
rm -rf "$TEST_RECOV_DIR/"*

# create test file in /tmp
dd if=/dev/urandom of=$TEST_RAND bs=1M count=10

echo
echo "**** TESTS: PUT ****"

for f in $TEST_FILES; do
    test_check_put "$f"
done

echo
echo "**** TESTS: GET ****"

# retrieve all files from the backend, get and check them
find $TEST_MNT -type f | while read f; do
    test_check_get "$f"
done

clean_test || true
