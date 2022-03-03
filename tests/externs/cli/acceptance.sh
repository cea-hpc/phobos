#!/bin/bash
# -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
# vim:expandtab:shiftwidth=4:tabstop=4:

#
#  All rights reserved (c) 2014-2022 CEA/DAM.
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

# TODO: change the way the resources are managed through the acceptance
# test cases: maybe considering all are locked, and only unlocking
# resources the test need at start, and relocking them when the test ends

set -xe

TAGS=foo-tag,bar-tag

# set python and phobos environment
test_dir=$(dirname $(readlink -e $0))
. $test_dir/../../test_env.sh
. $test_dir/../../setup_db.sh
. $test_dir/../../test_launch_daemon.sh
. $test_dir/../../tape_drive.sh

function setup
{
    if [ -w /dev/changer ]; then
        export PHOBOS_LRS_families="dir,tape"
    else
        export PHOBOS_LRS_families="dir"
    fi
    setup_tables
    invoke_daemon
}

# empty all drives
function empty_drives
{
    mtx status | grep "Data Transfer Element" | grep "Full" |
        while read line; do
            echo "full drive: $line"
            drive=$(echo $line | awk '{print $4}' | cut -d ':' -f 1)
            slot=$(echo $line | awk '{print $7}')
            echo "Unloading drive $drive in slot $slot"
            mtx unload $slot $drive || echo "mtx failure"
        done
}

function tape_setup
{
    export PHOBOS_STORE_default_family="tape"

    # make sure no LTFS filesystem is mounted, so we can unmount it
    /usr/share/ltfs/ltfs stop || true

    #  make sure all drives are empty
    empty_drives

    local N_TAPES=2
    local N_DRIVES=8
    local LTO5_TAGS=$TAGS,lto5
    local LTO6_TAGS=$TAGS,lto6

    # get LTO5 tapes
    local lto5_tapes="$(get_tapes L5 $N_TAPES)"
    echo "adding tapes $lto5_tapes with tags $LTO5_TAGS..."
    $phobos tape add --tags $LTO5_TAGS --type lto5 "$lto5_tapes"

    # get LTO6 tapes
    local lto6_tapes="$(get_tapes L6 $N_TAPES)"
    echo "adding tapes $lto6_tapes with tags $LTO6_TAGS..."
    $phobos tape add --tags $LTO6_TAGS --type lto6 "$lto6_tapes"

    # set tapes
    export tapes="$lto5_tapes,$lto6_tapes"

    # comparing with original list
    $phobos tape list | sort | xargs > /tmp/pho_tape.1
    echo "$tapes" | nodeset -e | sort > /tmp/pho_tape.2
    diff /tmp/pho_tape.1 /tmp/pho_tape.2
    rm -f /tmp/pho_tape.1 /tmp/pho_tape.2

    # show a tape info
    local tp1=$(echo $tapes | nodeset -e | awk '{print $1}')
    $phobos tape list --output all $tp1

    # unlock all tapes but one
    for t in $(echo "$tapes" | nodeset -e -S '\n' |
                    head -n $(($N_TAPES * 2 - 1))); do
        $phobos tape unlock $t
    done

    # get drives
    local drives=$(get_drives $N_DRIVES)
    N_DRIVES=$(echo $drives | wc -w)
    echo "adding drives $drives..."
    $phobos drive add $drives

    # show a drive info
    local dr1=$(echo $drives | awk '{print $1}')
    echo "$dr1"
    # check drive status
    $phobos drive list --output adm_status $dr1 --format=csv |
        grep "^locked" || error "Drive should be added with locked state"

    # unlock all drives but one (except if N_DRIVE < 2)
    if (( $N_DRIVES > 1 )); then
        local serials=$($phobos drive list | head -n $(($N_DRIVES - 1)))
    else
        local serials=$($phobos drive list)
    fi
    for d in $serials; do
        echo $d
        $phobos drive unlock $d
    done

    # need to format at least 2 tapes for concurrent_put
    # format lto5 tapes
    $phobos --verbose tape format $lto5_tapes --unlock
    # format lto6 tapes
    $phobos --verbose tape format $lto6_tapes --unlock
}

function dir_setup
{
    export PHOBOS_STORE_default_family="dir"

    export dirs="/tmp/test.pho.1 /tmp/test.pho.2"
    mkdir -p $dirs

    echo "adding directories $dirs"
    $phobos dir add --tags $TAGS $dirs
    $phobos dir format --fs posix $dirs

    # comparing with original list
    $phobos dir list | sort > /tmp/pho_dir.1
    :> /tmp/pho_dir.2
    #directory ids are <hostname>:<path>
    for d in $dirs; do
        echo "$d" >> /tmp/pho_dir.2
    done
    diff /tmp/pho_dir.1 /tmp/pho_dir.2
    rm -f /tmp/pho_dir.1 /tmp/pho_dir.2

    # show a directory info
    d1=$(echo $dirs | nodeset -e | awk '{print $1}')
    $phobos dir list --output all $d1

    # unlock all directories but one
    for t in $(echo $dirs | nodeset -e -S '\n' | head -n 1); do
        $phobos dir unlock $t
    done
}

function cleanup
{
    waive_daemon
    drop_tables
    rm -rf /tmp/test.pho.1 /tmp/test.pho.2
}

function lock_test
{
    # add 2 dirs: 1 locked, 1 unlocked
    # make sure they are inserted with the right status.
    local dir_prefix=/tmp/dir.$$
    mkdir $dir_prefix.1
    mkdir $dir_prefix.2
    $valg_phobos dir add $dir_prefix.1
    $phobos dir list --output adm_status $dir_prefix.1 --format=csv |
        grep "^locked" ||
        error "Directory should be added with locked state"
    $valg_phobos dir add $dir_prefix.2 --unlock
    $phobos dir list --output adm_status $dir_prefix.2 --format=csv |
        grep "^unlocked" ||
        error "Directory should be added with unlocked state"
    rmdir $dir_prefix.*
}

function put_get_test
{
    local md="a=1,b=2,c=3"
    local id=test/hosts.$$
    # phobos put
    $valg_phobos put --metadata $md /etc/hosts $id

    # phobos get
    local out_file=/tmp/out
    rm -f $out_file
    $valg_phobos get $id $out_file

    diff /etc/hosts $out_file
    rm -f $out_file

    local md_check=$($valg_phobos getmd $id)
    [ "x$md" = "x$md_check" ] ||
        error "Object attributes do not match expectations"
}

# Test family based media selection
function put_family
{
    local id=test/hosts-fam.$$
    local request="SELECT extents FROM extent WHERE oid='$id';"

    # phobos put
    PHOBOS_STORE_default_family="disk" $valg_phobos put \
        -f $1 /etc/hosts $id

    # PSQL command to get the the extent metadata, with the family of
    # the medium it was written to
    # XXX: will be replaced by a 'phobos object list' when implemented
    $PSQL -t -c "$request" | grep -q "\"fam\": \"$1\"" ||
        error "Put with family should have written object on a family medium"
}

function put_layout
{
    local id1=test/hosts-lay1.$$
    local id2=test/hosts-lay2.$$
    local request1="SELECT lyt_info FROM extent WHERE oid='$id1';"
    local request2="SELECT lyt_info FROM extent WHERE oid='$id2';"

    # unlock enough resources to make the test:
    # - only the second dir, the first one is already unlocked
    # - not needed for tapes, as only one among eight is locked
    # XXX: will also unlock a tape drive when lock admin on drives
    # will be considered by daemon scheduler
    if [[ $PHOBOS_STORE_default_family == "dir" ]]; then
        $phobos dir unlock /tmp/test.pho.2
    fi

    # phobos put
    $valg_phobos put --alias simple /etc/hosts $id1
    $valg_phobos put --layout raid1 /etc/hosts $id2

    $phobos extent list --output layout,ext_count $id1 | grep "raid1" \
                                                       | grep " 1" ||
        error "Put with default alias should have written object using " \
              "a raid1 layout with repl_count of 1"

    $phobos extent list --output layout,ext_count $id2 | grep "raid1" \
                                                       | grep " 2" ||
        error "Put with raid1 layout should have written object using " \
              "a raid1 layout with repl_count of 2"

    # relock resources we previously unlocked
    if [[ $PHOBOS_STORE_default_family == "dir" ]]; then
        $phobos dir lock /tmp/test.pho.2
    fi
}

# Test tag based media selection
function put_tags
{
    local md="a=1,b=2,c=3"
    local id=test/hosts2.$$
    local id2=test/hosts3.$$
    local first_tag=$(echo $TAGS | cut -d',' -f1)

    # phobos put with bad tags
    $valg_phobos put --tags $TAGS-bad --metadata $md /etc/hosts $id &&
        error "Should not be able to put objects with no media matching tags"
    $valg_phobos put --tags $first_tag-bad --metadata $md /etc/hosts $id &&
        error "Should not be able to put objects with no media matching tags"

    # phobos put with existing tags
    $valg_phobos put --tags $TAGS --metadata $md /etc/hosts $id ||
        error "Put with valid tags should have worked"
    $valg_phobos put --tags $first_tag --metadata $md /etc/hosts $id2 ||
        error "Put with one valid tag should have worked"
}

function test_single_alias_put
{
    local alias=$1
    local id=$2
    local expected_fam=$3
    local expected_layout=$4
    local additional_arguments=$5

    $valg_phobos put --alias $alias $additional_arguments /etc/hosts $id ||
        error "Put with alias $alias should have worked"

    local fam_request="SELECT extents FROM extent WHERE oid='$id';"
    $PSQL -t -c "$fam_request" |
        grep -q "\"fam\": \"$expected_fam\"" ||
        error "Put with alias \"$alias\" should have written object on a" \
              "\"$expected_fam\" medium"

    local lay_request="SELECT lyt_info FROM extent WHERE oid='$id';"
    $PSQL -t -c "$lay_request" |
        grep -q "\"name\": \"$expected_layout\"" ||
        error "Put with alias \"$alias\" should have used " \
              "\"$expected_layout\" layout"
}

# Test alias replacement
function put_alias
{
    local alias_full="full-test"
    local alias_tape="full-tape-test"
    local alias_empty_family="empty-family-test"
    local alias_empty_layout="empty-layout-test"
    local alias_empty_tags="empty-tag-test"
    local alias_bad="alias-noexist"
    local alias_nonexist_tags="erroneus-tag-test"

    local id_full=test/hosts-alias1.$$
    local id_empty_family=test/hosts-alias2.$$
    local id_empty_layout=test/hosts-alias3.$$
    local id_empty_tags=test/hosts-alias4.$$
    local id_noexist=test/hosts-alias5.$$
    local id_with_fam=test/hosts-alias6.$$
    local id_with_lay=test/hosts-alias7.$$
    local id_with_tag1=test/hosts-alias8.$$
    local id_with_tag2=test/hosts-alias9.$$
    local id_with_tag3=test/hosts-alias10.$$

    # change repl count to test different layouts
    local repl_count=$PHOBOS_LAYOUT_RAID1_repl_count
    export PHOBOS_LAYOUT_RAID1_repl_count=1

    # phobos put with full alias - expect alias family and layout
    test_single_alias_put $alias_full $id_full "dir" "raid1"

    # phobos put with reduced aliases - expect default for empty parameters
    test_single_alias_put $alias_empty_family $id_empty_family \
        $PHOBOS_STORE_default_family "raid1"
    test_single_alias_put $alias_empty_layout $id_empty_layout "dir" "raid1"
    test_single_alias_put $alias_empty_tags $id_empty_tags "dir" "raid1"

    # phobos put with non-existing alias - expect default
    test_single_alias_put $alias_bad $id_noexist $PHOBOS_STORE_default_family \
        "raid1"

    # phobos put with additional family parameter - expect additional parameter
    test_single_alias_put $alias_tape $id_with_fam "dir" "raid1" "--family dir"

    # phobos put with additional layout parameter - expect additional parameter
    test_single_alias_put $alias_full $id_with_lay "dir" "raid1" \
        "--layout raid1"

    # phobos put with additional tags parameter - expect success
    test_single_alias_put $alias_empty_family $id_with_tag1 "dir" "raid1" \
        "--tags $(echo $TAGS | cut -d',' -f2)"

    # phobos put with additional, non existing tags parameter - expect failure
    $valg_phobos put -a $alias_full --tags no-such-tag /etc/hosts \
        $id_with_tag2 &&
        error "Put with additional, non existing tag should not have worked"

    # phobos put with alias with wrong tags parameter - expect failure
    $valg_phobos put --alias $alias_nonexist_tags /etc/hosts $id_with_tag3 &&
        error "Put with alias $alias_nonexist_tags should not have worked"

    # reset replication count for raid layout
    export PHOBOS_LAYOUT_RAID1_repl_count=repl_count
}

# Test mput command
function mput
{
    local filenames=("$(mktemp)" "$(mktemp)" "$(mktemp)")

    echo "test1" > ${filenames[0]}
    echo "test2" > ${filenames[1]}
    echo "test3" > ${filenames[2]}

    OBJECTS=("${filenames[0]} mput_oid_1 -" \
             "${filenames[1]} mput_oid_2 foo=1" \
             "${filenames[2]} mput_oid_3 a=1,b=2,c=3")

    # create mput input file
    echo "${OBJECTS[0]}
          ${OBJECTS[1]}
          ${OBJECTS[2]}" > mput_list

    # phobos execute mput command
    $valg_phobos mput mput_list ||
        (rm mput_list; error "Mput should have worked")

    rm mput_list

    # modify metadata of first entry as '-' is interpreted as no metadata
    OBJECTS[0]="${filenames[0]} mput_oid_1 "

    # check mput correct behavior
    for object in "${OBJECTS[@]}"
    do
        src=$(echo "$object" | cut -d ' ' -f 1)
        oid=$(echo "$object" | cut -d ' ' -f 2)
        mdt=$(echo "$object" | cut -d ' ' -f 3)
        # check metadata were well read from the input file
        [[ $($valg_phobos getmd $oid) == $mdt ]] ||
            error "'$oid' object was not put with the right metadata"

        # check file contents are correct
        $valg_phobos get $oid file_test ||
            error "Can not retrieve '$oid' object"

        [[ $(md5sum file_test | cut -d ' ' -f 1) == \
           $(md5sum $src | cut -d ' ' -f 1) ]] ||
            (rm file_test; error "Retrieved object '$oid' is different")

        rm file_test
    done

    rm ${filenames[@]}
}

# make sure there are at least N available dir/drives
function ensure_nb_drives
{
    local count=$1
    local name
    local d

    if [[ $PHOBOS_STORE_default_family == "tape" ]]; then
        name=drive
    else
        name=dir
    fi

    local nb=0
    for d in $($phobos $name list); do
        if (( $nb < $count )); then
            $phobos $name unlock $d
            ((nb++)) || true
        fi
    done

    ((nb == count))
}

# Execute a phobos command but sleep on pho_posix_put for 1 second before
# checking the number of locks taken on devices and media then sleep again.
function phobos_delayed_dev_release
{
    (
        tmp_gdb_script=$(mktemp)
        trap "rm $tmp_gdb_script" EXIT
        cat <<EOF > "$tmp_gdb_script"
set breakpoint pending on
break write_all_chunks
commands
shell sleep 2
continue
end
run $phobos $*
EOF

        gdb -batch -x "$tmp_gdb_script" -q python3
    )
}

function concurrent_put
{
    local md="a=1,b=2,c=3"
    local tmp="/tmp/data.$$"
    local key=data.$(date +%s).$$
    local single=0

    # this test needs 2 drives
    ensure_nb_drives 2 || single=1

    # create input file
    dd if=/dev/urandom of=$tmp bs=1M count=100

    # 2 simultaneous put
    phobos_delayed_dev_release put --metadata $md $tmp $key.1 &
    local pid1=$!
    (( single==0 )) && phobos_delayed_dev_release put --metadata $md $tmp \
                            $key.2 &
    local pid2=$!

    wait $pid1
    wait $pid2

    rm -f $tmp

    # check they are both in DB
    $phobos getmd $key.1
    if (( single==0 )); then
        $phobos getmd $key.2
    fi
}

# Try to put with lots of phobos instances and check that the retry mechanism
# allows all the instances to succeed
function concurrent_put_get_retry
{
    files=`ls -p | grep -v / | head -n 30`

    # Concurrent put
    echo -n $files | xargs -n1 -P0 -d' ' -i $phobos put {} {} ||
        error "some phobos instances failed to put"

    PHO_TMP_DIR=$(mktemp -d /tmp/test.pho.XXXX)

    # Concurrent get
    echo -n $files |
        xargs -n1 -P0 -d' ' -i bash -c \
            "$phobos get {} \"$PHO_TMP_DIR/\$(basename {})\"" ||
        error "some phobos instances failed to get"

    rm -rf $PHO_TMP_DIR
}

function check_status
{
    local type="$1"
    local media="$2"

    for m in $media; do
        $phobos $type list --output all "$m"
    done
}

function drain_all_drives_daemon
{
    drain_all_drives
    waive_daemon
    invoke_daemon
}

function lock_all_drives
{
    for d in $($phobos drive list); do
        $phobos drive lock --force "$d"
    done
}

function unlock_all_drives
{
    for d in $($phobos drive list); do
        $phobos drive unlock --force "$d"
    done
}

function tape_drive_compat
{
    $valg_phobos put --tags lto5 /etc/services svc_lto5 ||
        error "fail to put on lto5 tape"

    $valg_phobos put --tags lto6 /etc/services svc_lto6 ||
        error "fail to put on lto6 tape"

    $valg_phobos get svc_lto5 /tmp/svc_lto5 ||
        error "fail to get from lto5 tape"
    rm /tmp/svc_lto5

    $valg_phobos get svc_lto6 /tmp/svc_lto6 ||
        error "fail to get from lto6 tape"
    rm /tmp/svc_lto6

    #test with only one lto5 drive
    lock_all_drives
    drain_all_drives_daemon
    local lto5_d=$($phobos drive list --model ULT3580-TD5 | head -n 1)
    $phobos drive unlock --force $lto5_d
    $valg_phobos get svc_lto5 /tmp/svc_lto5_from_lto5_drive ||
        error "fail to get data from lto5 tape with one lto5 drive"
    rm /tmp/svc_lto5_from_lto5_drive
    $valg_phobos get svc_lto6 /tmp/svc_lto6_from_lto5_drive &&
        error "getting data from lto6 tape with one lto5 drive must fail"

    #test with only one lto6 drive
    lock_all_drives
    drain_all_drives_daemon
    local lto6_d=$($phobos drive list --model ULT3580-TD6 | head -n 1)
    $phobos drive unlock --force $lto6_d
    $valg_phobos get svc_lto5 /tmp/svc_lto5_from_lto6_drive ||
        error "fail to get data from lto5 tape with one lto6 drive"
    rm /tmp/svc_lto5_from_lto6_drive
    $valg_phobos get svc_lto6 /tmp/svc_lto6_from_lto6_drive ||
        error "fail to get data from lto6 tape with one lto6 drive"
    rm /tmp/svc_lto6_from_lto6_drive

    unlock_all_drives
}

trap cleanup EXIT
setup

echo "POSIX test mode"
dir_setup

put_get_test
put_family dir
put_layout
put_tags
put_alias
mput
concurrent_put
check_status dir "$dirs"
lock_test
concurrent_put_get_retry

if  [[ -w /dev/changer ]]; then
    echo "Tape test mode"
    drop_tables
    setup_tables
    tape_setup

    put_get_test
    put_family tape
    put_tags
    mput
    concurrent_put
    check_status tape "$tapes"
    lock_test
    concurrent_put_get_retry
    tape_drive_compat
fi
