#!/bin/bash

# acceptance:
#   - put_family test ------------> to move
#   - check_status ---------------> without effect
#   - lock_test ------------------> without effect on tape
#   - tape_drive_compat ----------> not possible on preprod node

# to add:
#   - format ---------------------> add in setup if requested
#   - ls -------------------------> used in other tests

# We consider three different states about our system, which is
# determined using env variables DAEMON_ONLINE & DATABASE_ONLINE:
# +-------------+      +-----------------+      +---------------+
# |  no daemon  |      |    no daemon    |      |    daemon     |
# | no database |      |    database     |      |   database    |
# +-------------+      +-----------------+      +---------------+
# |    none     |      | DATABASE_ONLINE |      | DAEMON_ONLINE |
# +-------------+      +-----------------+      +---------------+

set -ex

if [[ ! -w /dev/changer ]]; then
    echo "Cannot access library: test skipped"
    exit 77
fi

declare -A test_tapes
# For preprod test, TEST_TAPES syntax example:
# TEST_TAPES='(["L6"]="073206L6 073207L6")'
eval test_tapes=$TEST_TAPES
# For preprod test, TEST_DRIVES syntax example:
# TEST_DRIVES="/dev/sg4 /dev/sg6"
eval test_drives=\"$TEST_DRIVES\"

test_dir=$(dirname $(readlink -e $0))

if [ ! -z ${EXEC_NONREGRESSION+x} ]; then
    . $test_dir/../test_env.sh
    . $test_dir/../setup_db.sh
    . $test_dir/../test_launch_daemon.sh

    start_tlc="invoke_tlc"
    start_phobosd="invoke_lrs"
    stop_phobosd="waive_lrs"
    stop_tlc="waive_tlc"

    start_phobosdb="setup_tables"
    stop_phobosdb="drop_tables"
    exec_nonregression=true
else
    phobos="phobos"
    start_tlc="systemctl start tlc"
    start_phobosd="systemctl start phobosd"
    stop_phobosd="systemctl stop phobosd"
    stop_tlc="systemctl stop tlc"

    start_phobosdb="phobos_db setup_tables"
    stop_phobosdb="phobos_db drop_tables"
    exec_nonregression=false
fi

if [ -z ${DATABASE_ONLINE+x} ]; then
    database_online=false
else
    database_online=true
fi

if [ -z ${DAEMON_ONLINE+x} ]; then
    daemon_online=false
else
    daemon_online=true
fi

if [ -z ${KEEP_SETUP+x} ]; then
    keep_setup=false
else
    keep_setup=true
fi

. $test_dir/../utils_generation.sh
. $test_dir/../utils.sh
. $test_dir/../tape_drive.sh

function log()
{
    >&2 echo "$*"
}

function setup_test
{
    if ! $daemon_online; then
        if ! $database_online; then
            $start_phobosdb
        fi

        $start_tlc
        $start_phobosd
        $phobos ping phobosd
        if ! $database_online; then
            setup_tape
        fi
    fi

    setup_test_dirs
    setup_dummy_files 30
}

function cleanup_test
{
    cleanup_dummy_files
    if ! $keep_setup && ! $daemon_online; then
        $stop_phobosd
        $stop_tlc
        if ! $database_online; then
            $stop_phobosdb
        fi
    fi

    cleanup_test_dirs
}

function get_test_tapes()
{
    local technology=$1
    local count=$2

    if $exec_nonregression; then
        get_tapes $technology $count
    else
        if [ -z "${test_tapes[$technology]}" ]; then
            log "No tapes available for technology $technology"
            return 1
        fi
        echo ${test_tapes[$technology]} | cut -d' ' -f1-$count
    fi
}

function get_test_drives()
{
    local count=$1

    if $exec_nonregression; then
        get_drives $count
    else
        if [ -z "${TEST_DRIVES}" ]; then
            return 1
        fi
        echo ${TEST_DRIVES}
    fi
}

function setup_tape
{
    ENTRY

    TAGS=foo-tag,bar-tag

    # make sure no LTFS filesystem is mounted, so we can unmount it
    /usr/share/ltfs/ltfs stop || true

    # make sure all drives are empty
    drain_all_drives

    local N_TAPES=2
    local N_DRIVES=8

    # get LTO5 tapes
    local lto5_tapes="$(get_test_tapes L5 $N_TAPES)"
    if [ ! -z "$lto5_tapes" ]; then
        $phobos tape add --tags $TAGS,lto5 --type lto5 $lto5_tapes
    fi

    # get LTO6 tapes
    local lto6_tapes="$(get_test_tapes L6 $N_TAPES)"
    if [ ! -z "$lto6_tapes" ]; then
        $phobos tape add --tags $TAGS,lto6 --type lto6 $lto6_tapes
    fi

    if [[ -z "$lto5_tapes" && -z "$lto6_tapes" ]];then
        exit_error "No tapes available for the tests, exit."
    fi

    if [[ -z "${lto5_tapes}" ]]; then
        local tapes="$lto6_tapes"
    elif [[ -z "${lto6_tapes}" ]]; then
        local tapes="$lto5_tapes"
    else
        local tapes="$lto5_tapes,$lto6_tapes"
    fi

    local out1=$($phobos tape list | sort)
    local out2=$(echo "$tapes" | nodeset -e | sort)
    [ "$out1" != "$out2" ] ||
        exit_error "Tapes are not all added"

    # get drives
    local drives=$(get_test_drives $N_DRIVES)

    local nb_drives=$(echo "$drives" | wc -w)
    if (( nb_drives == 0 )); then
        exit_error "No drives found for testing"
    fi
    $phobos drive add $drives

    # unlock all drives
    $phobos drive unlock $($phobos drive list)

    # format and unlock all tapes
    if [[ ! -z "$lto5_tapes" ]]; then
        $phobos tape format $lto5_tapes --unlock
    fi
    if [[ ! -z "$lto6_tapes" ]]; then
        $phobos tape format $lto6_tapes --unlock
    fi
}

function test_put_get
{
    ENTRY
    local prefix=$(generate_prefix_id)

    local md="a=1,b=2,c=3"
    local id=$prefix/id
    local out_file=$DIR_TEST_OUT/out

    # put & get one object
    $phobos put --metadata $md ${FILES[0]} $id
    $phobos get $id $out_file

    # check file contents are correct
    diff_with_rm_on_err $out_file ${FILES[0]} "Files contents are different"
    rm $out_file

    # check object metadata are correct
    local md_check=$($phobos getmd $id)
    [ "$md" = "$md_check" ] ||
          exit_error "Object attributes do not match expectations"
}

function test_put_with_tags
{
    ENTRY
    local prefix=$(generate_prefix_id)

    local id0=$prefix/id0
    local id1=$prefix/id1
    local first_tag=$(echo $TAGS | cut -d',' -f1)

    # put with invalid tags
    $phobos put --tags bad-tag ${FILES[0]} $id0 &&
        exit_error "Should not be able to put objects with no media matching " \
                   "tags"
    $phobos put --tags $first_tag,bad-tag ${FILES[0]} $id0 &&
        exit_error "Should not be able to put objects with no media matching " \
                   "tags"

    # put with valid tags
    $phobos put --tags $TAGS ${FILES[0]} $id0 ||
        exit_error "Put with valid tags should have worked"
    $phobos put --tags $first_tag ${FILES[0]} $id1 ||
        exit_error "Put with one valid tag should have worked"
}

function test_put_delete
{
    ENTRY
    local prefix=$(generate_prefix_id)

    local id=$prefix/id
    local out_file=$DIR_TEST_OUT/out

    # put 1st generation object
    $phobos put ${FILES[0]} $id || exit_error "Put should have worked"
    local uuid0=$($phobos object list --output uuid $id)
    local ver0=$($phobos object list --output version $id)

    # delete the 1st generation to put 2nd generation object
    $phobos delete $id
    $phobos put ${FILES[1]} $id || exit_error "Put should have worked"
    local uuid1=$($phobos object list --output uuid $id)
    local ver1=$($phobos object list --output version $id)

    # overwrite the 2nd generation object
    $phobos put --overwrite ${FILES[2]} $id ||
        exit_error "Put should have worked"
    local uuid2=$($phobos object list --output uuid $id)
    local ver2=$($phobos object list --output version $id)

    # check uuids and versions are correct
    [ $uuid0 != $uuid1 ] ||
        exit_error "UUIDs of objects from different generations should differ"
    [ $uuid1 == $uuid2 ] ||
        exit_error "UUIDs of objects from the same generation should be equal"

    [ $ver0 -eq 1 ] ||
        exit_error "First put of new generation object should be version 1"
    [ $ver1 -eq 1 ] ||
        exit_error "First put of new generation object should be version 1"
    [ $ver2 -eq 2 ] || exit_error "Second generation object should be version 2"

    # check retrieved objects are correct
    $phobos get $id $out_file ||
        exit_error "Can not retrieve '$id' object"
    diff_with_rm_on_err $out_file ${FILES[2]} "Bad retrieved object '$id'"
    rm $out_file

    $phobos get --uuid $uuid2 $id $out_file ||
        exit_error "Can not retrieve '$uuid2' object"
    diff_with_rm_on_err $out_file ${FILES[2]} "Bad retrieved object '$uuid2'"
    rm $out_file

    $phobos get --uuid $uuid2 --version $ver2 $id $out_file ||
        exit_error "Can not retrieve '$uuid2:$ver2' object"
    diff_with_rm_on_err $out_file ${FILES[2]} \
        "Bad retrieved object '$uuid2:$ver2'"
    rm $out_file

    $phobos get --uuid $uuid1 --version $ver1 $id $out_file ||
        exit_error "Can not retrieve '$uuid1:$ver1' object"
    diff_with_rm_on_err $out_file ${FILES[1]} \
        "Bad retrieved object '$uuid1:$ver1'"
    rm $out_file

    $phobos get --uuid $uuid0 --version $ver0 $id $out_file ||
        exit_error "Can not retrieve '$uuid0:$ver0' object"
    diff_with_rm_on_err $out_file ${FILES[0]} \
        "Bad retrieved object '$uuid0:$ver0'"
    rm $out_file
}

function test_mput
{
    ENTRY
    local prefix=$(generate_prefix_id)

    OBJECTS=("${FILES[0]} $prefix/id0 -" \
             "${FILES[1]} $prefix/id1 foo=1" \
             "${FILES[2]} $prefix/id2 a=1,b=2,c=3")
    local out_file=$DIR_TEST_OUT/out

    # create mput input file
    echo "${OBJECTS[0]}
          ${OBJECTS[1]}
          ${OBJECTS[2]}" > $DIR_TEST_IN/mput_list

    # phobos execute mput command
    $phobos mput $DIR_TEST_IN/mput_list ||
        { rm $DIR_TEST_IN/mput_list; exit_error "Mput should have worked"; }

    rm $DIR_TEST_IN/mput_list

    # modify metadata of first entry as '-' is interpreted as no metadata
    OBJECTS[0]="${FILES[0]} $prefix/id0 "

    # check mput correct behavior
    for object in "${OBJECTS[@]}"
    do
        src=$(echo "$object" | cut -d ' ' -f 1)
        oid=$(echo "$object" | cut -d ' ' -f 2)
        mdt=$(echo "$object" | cut -d ' ' -f 3)
        # check metadata were well read from the input file
        res=$($phobos getmd $oid)
        [[ $res == $mdt ]] ||
            exit_error "'$oid' object was not put with the right metadata"

        # check file contents are correct
        $phobos get $oid $out_file ||
            exit_error "Can not retrieve '$oid' object"

        diff_with_rm_on_err $out_file $src \
            "Retrieved object '$oid' is different"

        rm $out_file
    done
}

function test_concurrent_put_get_retry
{
    ENTRY
    local prefix=$(generate_prefix_id)

    # concurrent put
    echo -n ${FILES[@]} | xargs -n1 -P0 -d' ' -I{} bash -c \
        "$phobos put {} $prefix/\$(basename {})" ||
        exit_error "Some phobos instances failed to put"

    # concurrent get
    echo -n ${FILES[@]} | xargs -n1 -P0 -d' ' -I{} bash -c \
        "$phobos get $prefix/\$(basename {}) \
         \"$DIR_TEST_OUT/\$(basename {})\"" ||
        exit_error "Some phobos instances failed to get"

    # check file contents are correct
    for file in "${FILES[@]}"
    do
        diff $file $DIR_TEST_OUT/$(basename $file) ||
            exit_error "Retrieved object '$file' is different"
    done
}

function test_lock
{
    ENTRY
    local prefix=$(generate_prefix_id)

    local drive_model=ULT3580-TD5
    if [[ "$($phobos drive list --model $drive_model)" == "" ]]; then
        echo "No drive compatible with ${drive_model} to test lock"
        return 0
    fi

    local tape_model=lto5
    if [[ "$(phobos tape list --tags $tape_model)" == "" ]]; then
        echo "No tape compatible with model ${tape_model} to test lock"
        return 0
    fi

    $stop_phobosd

    # lock all drives except one, lock all tapes except one
    $phobos drive lock $($phobos drive list)
    $phobos tape lock $($phobos tape list)

    drain_all_drives
    $start_phobosd

    local tapes=$($phobos tape list --tags $tape_model | head -n 2)
    local tape1=$(echo $tapes | cut -d' ' -f1)
    local tape2=$(echo $tapes | cut -d' ' -f2)

    $phobos drive unlock $($phobos drive list --model $drive_model | head -n 1)
    $phobos tape unlock $tape1

    # put an object
    $phobos put ${FILES[0]} $prefix/id0 ||
        exit_error "Put should have worked"

    $phobos extent list --output media_name | grep $tape1 ||
        exit_error "Data should be written on $tape1"

    # lock the valid tape, unlock another one
    $phobos tape lock $tape1
    $phobos tape unlock $tape2

    # put an object
    $phobos put ${FILES[0]} $prefix/id1 ||
        exit_error "Put should have worked"

    $phobos extent list --output media_name | grep $tape2 ||
        exit_error "Data should be written on $tape2"

    $phobos drive unlock $($phobos drive list)
    $phobos tape unlock $($phobos tape list)
}

trap cleanup_test EXIT
setup_test

test_put_get
test_put_delete
test_mput
test_concurrent_put_get_retry

if ! $database_online; then
    # These tests can only work if setup_tape was called
    test_put_with_tags
    test_lock
fi
