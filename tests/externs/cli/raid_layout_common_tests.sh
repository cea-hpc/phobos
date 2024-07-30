#!/bin/bash
# -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
# vim:expandtab:shiftwidth=4:tabstop=4:

#
#  All rights reserved (c) 2014-2024 CEA/DAM.
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

test_dir=$(dirname $(readlink -e $0))
. $test_dir/test_env.sh
. $test_dir/setup_db.sh
. $test_dir/test_launch_daemon.sh
. $test_dir/tape_drive.sh
. $test_dir/utils_generation.sh

function set_extent_opt()
{
    local layout=$(echo $RAID_LAYOUT | awk '{print toupper($0)}')

    eval "export PHOBOS_LAYOUT_${layout}_extent_$1=true"
}

function make_file()
{
    local size=$1
    local file=$(mktemp)

    dd if=/dev/urandom of="$file" bs=$size count=1
    if (( ODD_FILE_SIZE )); then
        echo -n 1 >> "$file" # Make the file with an even size
    fi

    echo "$file"
}

function get_extent_info()
{
    local oid=$1
    local column=$2

    $phobos extent list -f json -o "$column" "$oid" | jq -r ".[0].$column[]"
}

function getxattr()
{
    local file="$1"
    local key="$2"

    getfattr -n "$key" -e text "$file" 2>/dev/null |
        sed '/^[[:space:]]*$/d' | # remove empty lines
        tail -n 1 # Only keep the actual output
}

function check_hash()
{
    local algo=$1
    local file=$2
    local oid=$3
    local expected_hash=$(${algo}sum "$file" | awk '{print $1}')

    # Check the hash is correct in the DSS
    $phobos extent list --degroup -o "$algo",address "$oid"
    $phobos extent list --degroup -o "$algo",address "$oid" |
        grep "$(basename "$file")" |
        grep "$expected_hash"

    # Check the hash is correct on the medium
    getxattr "$file" "user.$algo" | grep "$expected_hash"
}

function check_phobos_xattr()
{
    local input_file=$1
    local extent=$2
    local oid=$3
    local object_uuid=$($phobos object list -o uuid "$oid")

    getxattr "$extent" user.object_size | grep "$(stat -c %s "$input_file")"
    getxattr "$extent" user.object_uuid | grep "$object_uuid"
    getxattr "$extent" user.layout | grep $RAID_LAYOUT
    check_hash md5 "$extent" "$oid"
    check_hash xxh128 "$extent" "$oid"
}

function check_extent_count()
{
    local nb_extents=$($phobos extent list --degroup "$1" | wc -l)
    local expected=$2

    if (( nb_extents != expected )); then
        error "We where expecting splits in the test"
    fi
}

function raid1_extent_offset()
{
    local n1=$1[@]
    local media=(${!n1})
    local n2=$2[@]
    local addresses=(${!n2})
    local extent_index=$3
    local extent="${media[$extent_index]}/${addresses[$extent_index]}"
    local repl_count=$(getxattr "$extent" user.raid1.repl_count |
                       cut -d'=' -f2 |
                       sed 's/"//g')
    local total=0
    local i

    for ((i = 0; i < $extent_index / $repl_count; i++)); do
        (( total += $(stat -c %s "${media[2 * i]}/${addresses[2 * i]}") ))
    done

    echo $total
}

function raid4_extent_offset()
{
    local n1=$1[@]
    local media=(${!n1})
    local n2=$2[@]
    local addresses=(${!n2})
    local extent_index=$3
    local extent="${media[$extent_index]}/${addresses[$extent_index]}"
    local repl_count=$(getxattr "$extent" user.raid1.repl_count |
                       cut -d'=' -f2 |
                       sed 's/"//g')
    local total=0
    local i

    for ((i = 0; i < $extent_index / 3; i++)); do
        ((total += $(stat -c %s "${media[3 * i]}/${addresses[3 * i]}")))
        ((total += $(stat -c %s "${media[3 * i + 1]}/${addresses[3 * i + 1]}")))
    done

    echo $total
}

function check_offset()
{
    local n1=$1[@]
    local media=(${!n1})
    local n2=$2[@]
    local addresses=(${!n2})
    local layout_index=$3

    getxattr "$extent" user.extent_offset |
        grep $(${RAID_LAYOUT}_extent_offset media addresses $layout_index)
}

function check_extent_md()
{
    local oid=$1
    local input_file=$2
    local addresses=($(get_extent_info "$oid" address))
    local media=($(get_extent_info "$oid" media_name))
    local ext_uuid=($(get_extent_info "$oid" ext_uuid))

    for (( i = 0; i < ${#addresses[@]}; i++ )); do
        local extent="${media[i]}/${addresses[i]}"

        check_phobos_xattr "$input_file" "$extent" "$oid"
        check_offset media addresses $i
    done
}

function setup_dir
{
    DIRS=(
        $(mktemp -d)
        $(mktemp -d)
        $(mktemp -d)
    )

    export PHOBOS_LRS_families="dir"
    export PHOBOS_STORE_default_family="dir"
    export PHOBOS_STORE_default_layout="$RAID_LAYOUT"
    set_extent_opt md5
    set_extent_opt xxh128

    if [[ "$1" == "odd" ]]; then
        export ODD_FILE_SIZE=1
    elif [[ "$1" == "even" ]]; then
        export ODD_FILE_SIZE=0
    else
        error "Invalid argument $1: expected 'even' or 'odd'"
    fi

    setup_tables
    invoke_lrs

    $phobos dir add ${DIRS[@]}
    $phobos dir format --unlock ${DIRS[@]}
}

function cleanup_dir
{
    waive_lrs
    rm -r ${DIRS[@]}
    drop_tables
}

function test_put_get()
{
    local oid=$FUNCNAME
    local file=$(make_file 512K)

    $valg_phobos put "$file" $oid
    check_extent_md $oid "$file"
    $valg_phobos get $oid /tmp/out.$$

    diff "$file" /tmp/out.$$
    rm /tmp/out.$$ "$file"
}

function test_empty_put()
{
    local oid=$FUNCNAME
    local file=$(mktemp)

    $valg_phobos put "$file" $oid
    check_extent_md $oid "$file"
    $valg_phobos get $oid /tmp/out.$$

    diff "$file" /tmp/out.$$
    rm /tmp/out.$$ "$file"
}

function test_read_with_missing_extent()
{
    local oid=$FUNCNAME
    local file=$(make_file 512K)

    $valg_phobos put "$file" $oid
    check_extent_md $oid "$file"

    for d in $($phobos dir list); do
        $phobos dir lock $d

        $valg_phobos get $oid /tmp/out.$$
        diff "$file" /tmp/out.$$
        rm /tmp/out.$$

        $phobos dir unlock $d
    done

    rm "$file"
}

function test_with_different_block_size()
{
    local oid=$FUNCNAME
    local file=$(make_file 512K)

    export PHOBOS_IO_io_block_size=$(( 2 << 14 ))

    $valg_phobos put "$file" $oid
    check_extent_md $oid "$file"
    unset PHOBOS_IO_io_block_size
    $valg_phobos get $oid /tmp/out.$$

    diff "$file" /tmp/out.$$
    rm /tmp/out.$$ "$file"
}

function setup_dir_split()
{
    DIRS=(
        $(make_tmp_fs 1M)
        $(make_tmp_fs 1M)
        $(make_tmp_fs 1M)
        $(make_tmp_fs 1M)
        $(make_tmp_fs 1M)
        $(make_tmp_fs 1M)
    )

    setup_tables
    export PHOBOS_LRS_families="dir"
    export PHOBOS_STORE_default_family="dir"
    export PHOBOS_STORE_default_layout="$RAID_LAYOUT"
    set_extent_opt md5
    set_extent_opt xxh128

    if [[ "$1" == "odd" ]]; then
        export ODD_FILE_SIZE=1
    elif [[ "$1" == "even" ]]; then
        export ODD_FILE_SIZE=0
    else
        error "Invalid argument $1: expected 'even' or 'odd'"
    fi

    invoke_lrs

    $phobos dir add ${DIRS[@]}
    $phobos dir format --unlock ${DIRS[@]}
}

function cleanup_dir_split()
{
    waive_lrs
    drop_tables
    for dir in ${DIRS[@]}; do
        cleanup_tmp_fs $dir
    done
}

function test_put_get_split()
{
    local file=$(make_file 2740KB)
    local oid=$FUNCNAME
    local out=/tmp/out.$$

    $valg_phobos put "$file" $oid
    check_extent_count "$oid" 6
    check_extent_md "$oid" "$file"
    $valg_phobos get $oid "$out"
    diff "$out" "$file"
    rm "$out" "$file"
}

function test_put_get_split_different_block_size()
{
    local file=$(make_file 2740KB)
    local oid=$FUNCNAME
    local out=/tmp/out.$$

    export PHOBOS_IO_io_block_size=$(( 2 << 14 ))
    $valg_phobos put "$file" $oid
    check_extent_count "$oid" 6
    check_extent_md "$oid" "$file"
    unset PHOBOS_IO_io_block_size
    $valg_phobos get $oid "$out"
    diff "$out" "$file"
    rm "$out" "$file"
}

function test_put_get_split_with_missing_extents()
{
    local file=$(make_file 2740KB)
    local oid=$FUNCNAME
    local out=/tmp/out.$$

    $valg_phobos put "$file" $oid
    check_extent_count "$oid" 6
    check_extent_md "$oid" "$file"

    for d in $($phobos dir list); do
        $phobos dir lock $d

        $valg_phobos get $oid "$out"
        diff "$file" "$out"
        rm "$out"

        $phobos dir unlock $d
    done

    rm "$file"
}

TESTS=(
    "setup_dir even; \
     test_put_get; \
     test_empty_put; \
     test_read_with_missing_extent; \
     test_with_different_block_size; \
     cleanup_dir"
    "setup_dir_split even; \
     test_put_get_split; \
     cleanup_dir_split"
    "setup_dir_split even; \
     test_put_get_split_different_block_size; \
     cleanup_dir_split"
    "setup_dir_split even; \
     test_put_get_split_with_missing_extents; \
     cleanup_dir_split"

    "setup_dir odd; \
     test_put_get; \
     test_read_with_missing_extent; \
     test_with_different_block_size; \
     cleanup_dir"
    "setup_dir_split odd; \
     test_put_get_split; \
     cleanup_dir_split"
    "setup_dir_split odd; \
     test_put_get_split_different_block_size; \
     cleanup_dir_split"
    "setup_dir_split odd; \
     test_put_get_split_with_missing_extents; \
     cleanup_dir_split"
)

if  [[ -w /dev/changer ]]; then
    TESTS+=()
fi
