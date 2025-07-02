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
    start_tlc="systemctl start phobos_tlc"
    start_phobosd="systemctl start phobosd"
    stop_phobosd="systemctl stop phobosd"
    stop_tlc="systemctl stop phobos_tlc"

    start_phobosdb="phobos_db setup_tables"
    stop_phobosdb="phobos_db drop_tables"
    exec_nonregression=false

    # For preprod test, TEST_TAPES syntax example:
    # TEST_TAPES='(["L6"]="073206L6 073207L6")'
    eval test_tapes=$TEST_TAPES
    # For preprod test, TEST_DRIVES syntax example:
    # TEST_DRIVES='(["5"]="/dev/sg4 /dev/sg6")'
    eval test_drives=$TEST_DRIVES
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
        $phobos phobosd ping
        if ! $database_online; then
            setup_tape
        fi
    fi

    setup_test_dirs
    setup_dummy_files 30 1k 1
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
    local technology=$1
    local count=$2

    if $exec_nonregression; then
        get_lto_drives $technology $count
    else
        if [ -z "${test_drives[$technology]}" ]; then
            log "No drives available for technology $technology"
            return 1
        fi
        echo ${test_drives[$technology]} | cut -d' ' -f1-$count
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

    local N_TAPES_L5=2
    local N_TAPES_L6=2
    local N_DRIVES_L5=4
    local N_DRIVES_L6=4

    # get LTO5 tapes
    local lto5_tapes="$(get_test_tapes L5 $N_TAPES_L5)"
    if [ ! -z "$lto5_tapes" ]; then
        $phobos tape add --tags $TAGS,lto5 --type lto5 $lto5_tapes
    fi

    # get LTO6 tapes
    local lto6_tapes="$(get_test_tapes L6 $N_TAPES_L6)"
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
    local drives="$(get_test_drives 5 ${N_DRIVES_L5}) \
                  $(get_test_drives 6 ${N_DRIVES_L6})"

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

function test_multi_put
{
    ENTRY
    local prefix=$(generate_prefix_id)

    OBJECTS=("${FILES[0]} $prefix/id0 -" \
             "${FILES[1]} $prefix/id1 foo=1" \
             "${FILES[2]} $prefix/id2 a=1,b=2,c=3")
    local out_file=$DIR_TEST_OUT/out

    # create multi_put input file
    echo "${OBJECTS[0]}
          ${OBJECTS[1]}
          ${OBJECTS[2]}" > $DIR_TEST_IN/multi_put_list

    # phobos execute multi_put command
    $phobos put --file $DIR_TEST_IN/multi_put_list ||
        { rm $DIR_TEST_IN/multi_put_list;
          exit_error "Mput should have worked"; }

    rm $DIR_TEST_IN/multi_put_list

    # modify metadata of first entry as '-' is interpreted as no metadata
    OBJECTS[0]="${FILES[0]} $prefix/id0 "

    # check multi_put correct behavior
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

    for drive in $($phobos drive list); do
        $phobos drive unload $drive
    done

    # lock all drives except one, lock all tapes except one
    $phobos drive lock $($phobos drive list)
    $phobos tape lock $($phobos tape list)

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

function check_copies()
{
    local oid=$1
    local expected_copies=$2
    shift 2
    local copy_names=( "$@" )

    local copies=$($phobos copy list $oid)
    local count=$(echo "$copies" | wc -l)
    if [[ $count -ne $expected_copies ]]; then
        error "There should be $expected_copies copies, got $count"
    fi

    for copy in "${copy_names[@]}"; do
        echo "$copies" | grep "$copy"
    done
}

function check_extents()
{
    local output=$1
    local oid=$2
    shift 2
    local extents_and_count=( "$@" )

    local array_length=${#extents_and_count[@]}
    local expected_extent_count=0

    for (( i=1; i<$array_length; i+=2 )); do
        (( expected_extent_count += ${extents_and_count[$i]} ))
    done

    local extents=($($phobos extent list --degroup -o $output $oid))

    local count=${#extents[@]}
    if [[ $count -ne $expected_extent_count ]]; then
        error "There should be $expected_extent_count extents, got $count"
    fi

    for (( i=0; i<$array_length; i++ )); do
        local current_extent="${extents_and_count[$i]}"
        i=$((i+1))
        local expected_count="${extents_and_count[$i]}"

        echo "${extents[@]}" | grep "$current_extent"
        local count=0

        for extent in ${extents[@]}; do
            if [[ $extent == "$current_extent" ]]; then
                count=$(($count + 1))
            fi
        done

        if [[ $count -ne $expected_count ]]; then
            error "There should be $expected_count extents for" \
                  "$current_extent, got $count"
        fi
    done
}

function test_copy_on_dir
{
    ENTRY

    local oid="$(generate_prefix_id).oid"
    local TEST_DIRS=(
          $(mktemp -d /tmp/test_pho.XXXX)
          $(mktemp -d /tmp/test_pho.XXXX)
         )

    $phobos dir add ${TEST_DIRS[@]}
    $phobos dir format --unlock ${TEST_DIRS[@]}

    $phobos put -f dir ${FILES[0]} $oid

    $phobos copy create -f dir $oid copy-source ||
        error "Phobos copy create should have worked"

    check_copies "$oid" 2 "source" "copy-source"
    check_extents "copy_name" "$oid" "source" 1 "copy-source" 1

    $phobos copy delete $oid source

    check_copies "$oid" 1 "copy-source"
    check_extents "copy_name" "$oid" "copy-source" 1

    $phobos dir lock ${TEST_DIRS[@]}
    rm -rf ${TEST_DIRS[@]}
}

function test_copy_dir_to_tape
{
    ENTRY

    local oid="$(generate_prefix_id).oid"
    local DIR=$(mktemp -d /tmp/test_pho.XXXX)

    $phobos dir add $DIR
    $phobos dir format --unlock $DIR

    $phobos put -f dir ${FILES[0]} $oid

    $phobos copy create -f tape $oid copy-source ||
        error "Phobos copy create should have worked"

    check_copies "$oid" 2 "source" "copy-source"
    check_extents "copy_name" "$oid" "source" 1 "copy-source" 1
    check_extents "family" "$oid" "['dir']" 1 "['tape']" 1

    $phobos copy delete $oid source

    check_copies "$oid" 1 "copy-source"
    check_extents "copy_name" "$oid" "copy-source" 1
    check_extents "family" "$oid" "['tape']" 1

    $phobos dir lock $DIR
    rm -rf ${DIR}
}

function test_repack
{
    ENTRY

    $phobos tape lock $($phobos tape list)

    local prefix="$(generate_prefix_id)"
    local oid0="$prefix.oid0"
    local oid1="$prefix.oid1"

    local tapes=$($phobos tape list --tags lto6 | head -n 2)
    local tape1=$(echo $tapes | cut -d' ' -f1)
    local tape2=$(echo $tapes | cut -d' ' -f2)

    $phobos tape unlock $tape1
    $phobos tape format --force $tape2

    $phobos drive status
    $phobos drive list -o all
    $phobos tape list -o all

    $phobos put ${FILES[0]} $oid0
    $phobos put ${FILES[1]} $oid1

    $phobos del $oid0

    $phobos tape unlock $tape2
    $phobos tape repack $tape1

    local oid0_nb_extents="$($phobos extent list --name $tape1 $oid0 | wc -l)"
    if (( oid0_nb_extents != 0 )); then
        error "'$oid0' shouldn't be on '$tape1'"
    fi

    oid0_nb_extents="$($phobos extent list --name $tape2 $oid0 | wc -l)"
    if (( oid0_nb_extents != 0 )); then
        error "'$oid0' shouldn't be on '$tape2'"
    fi

    local oid1_nb_extents="$($phobos extent list --name $tape1 $oid1 | wc -l)"
    if (( oid1_nb_extents != 0 )); then
        error "'$oid1' shouldn't be on '$tape1'"
    fi

    oid1_nb_extents="$($phobos extent list --name $tape2 $oid1 | wc -l)"
    if (( oid1_nb_extents != 1 )); then
        error "'$oid1' should be on '$tape2'"
    fi

    local nb=$($phobos object list --pattern --deprecated-only $prefix.*| wc -l)
    if (( nb != 0 )); then
        error "repack should have deleted deprecated objects"
    fi

    nb=$($phobos object list --pattern $prefix.* | wc -l)
    if (( nb != 1 )); then
        error "repack should have let non-deprecated objects alive"
    fi
}

trap cleanup_test EXIT
setup_test

test_put_get
test_put_delete
test_multi_put
test_concurrent_put_get_retry
test_copy_on_dir

if ! $database_online; then
    # These tests can only work if setup_tape was called
    test_put_with_tags
    test_lock
    test_copy_dir_to_tape
    test_repack
fi
