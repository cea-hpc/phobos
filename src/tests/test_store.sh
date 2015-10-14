#!/bin/bash

# ENV:
#	NO_TAPE=1 force executing tests in POSIX FS

set -e

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
export PHOBOS_CFG_FILE="$test_bin_dir/phobos.conf"


. $test_bin_dir/setup_db.sh
drop_tables
setup_tables
insert_examples

ldm_helper=$(readlink -e $test_bin_dir/../../scripts/)/pho_ldm_helper
export PHOBOS_LDM_cmd_drive_query="$ldm_helper query_drive '%s'"
export PHOBOS_LDM_cmd_drive_load="$ldm_helper load_drive '%s' '%s'"
export PHOBOS_LDM_cmd_drive_unload="$ldm_helper unload_drive '%s' '%s'"
export PHOBOS_LDM_cmd_mount_ltfs="$ldm_helper mount_ltfs '%s' '%s'"
export PHOBOS_LDM_cmd_umount_ltfs="$ldm_helper umount_ltfs '%s' '%s'"

# testing with real tapes requires tape devices + access to /dev/changer device
nb_tapes=$(ls -d /dev/IBMtape* 2>/dev/null | wc -l)
if  [[ -z "$NO_TAPE" ]] && [[ -w /dev/changer ]] && (( $nb_tapes > 0 )); then
	echo "**** TAPE TEST MODE ****"
	echo "changer:"
	ls -ld /dev/changer
	echo "drives:"
	ls -ld /dev/IBMtape*
	TEST_MNT=/mnt/phobos* # must match mount prefix (default: /mnt/phobos-*)
	TEST_FS="ltfs"

	export PHOBOS_LRS_default_family="tape"
	export FAST_LTFS_SYNC=1

	# make sure no LTFS filesystem is mounted, so the test must mount it
	service ltfs stop

	# put the "low capacity" tape '073220L6' into the unlocked drive
    # (SN 1014005381)
	# to force phobos performing umount/unload/load/mount
	# to write the big RAND file
	SN=1014005381
	id=$(cat /proc/scsi/IBMtape | grep $SN | awk '{print $1}')
	drv=/dev/IBMtape${id}

	echo "eject 073221L6 from $drv if present"
	$ldm_helper unload_drive $drv 073221L6 \
		|| true # ignore if not mounted
	echo "eject 073222L6 from $drv if present"
	$ldm_helper unload_drive $drv 073222L6 \
		|| true # ignore if not mounted

	echo "mount 073220L6 to $drv if absent"
	$ldm_helper load_drive $drv 073220L6

else
	echo "**** POSIX TEST MODE ****"
	TEST_MNT="/tmp/pho_testdir1 /tmp/pho_testdir2" # must match mount prefix
	TEST_FS="posix"

	export PHOBOS_LRS_mount_prefix=/tmp/pho_testdir
	export PHOBOS_LRS_default_family="dir"
fi

function clean_test
{
    echo "cleaning..."
    rm -f "$TEST_RAND"
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
    local src_file=$(readlink -m $2)
    local name=$(echo $src_file | tr './!<>{}#"''' '_')
    # non empty if a failure is expected
    local fail=$3

    $test_bin $1 $src_file
    if [[ $? != 0 ]]; then
        if [[ -z $fail ]]; then
            error "$1 failure"
        else
            echo "OK: $1 failed as expected"
            return 1
        fi
    fi

    # check that the extent is found into the storage backend
    out=$(find $TEST_MNT -type f -name "*$name*")
    echo -e " \textent: $out"
    [ -z "$out" ] && error "*$name* not found in backend"
    diff -q $src_file $out && echo "$src_file: contents OK"

    # check the 'id' xattr attached to this extent
    id=$(getfattr --only-values --absolute-names -n "user.id" $out)
    [ -z "$id" ] && error "saved file has no 'id' xattr"
    [ $id != $src_file ] && error "unexpected id value in xattr"

    # check user_md xattr attached to this extent
    umd=$(getfattr --only-values --absolute-names -n "user.user_md" $out)
    [ -z "$umd" ] && error "saved file has no 'user_md' xattr"

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

    # As extents are not saved in the DB for now,
    # insert an extent that matches the objet we want to read
    if [[ $TEST_FS == "ltfs" ]]; then
        # (PSQL is defined in setup_db.sh)
        $PSQL << EOF
        insert into extent (oid, copy_num, state, lyt_type, lyt_info, extents)
        values ('$id', 0, 'sync', 'simple','{}',
            '[{"media":"073222L6","addr":"$addr","sz":"$size","fam":"tape"}]');
EOF
    else
        host=$(hostname --short)
        $PSQL << EOF
        insert into extent (oid, copy_num, state, lyt_type, lyt_info, extents)
        values ('$id', 0, 'sync', 'simple','{}',
            '[{"media":"$host:$dir","addr":"$addr","sz":"$size","fam":"dir"}]');
EOF
    fi

    tgt="$TEST_RECOV_DIR/$id"
    mkdir -p $(dirname "$tgt")

    $test_bin get "$id" "$tgt"

    diff -q "$arch" "$tgt"
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
echo "**** TESTS: PUT ****"

for f in $TEST_FILES; do
    test_check_put "post" "$f"
    test_check_put "post" "$f" "yes" && error "second POST should fail"
    test_check_put "put" "$f"
done

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
