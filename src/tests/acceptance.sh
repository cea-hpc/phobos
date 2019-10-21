#!/bin/bash
# -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
# vim:expandtab:shiftwidth=4:tabstop=4:

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

set -e

# format and clean all by default
CLEAN_ALL=${CLEAN_ALL:-1}
TAGS=foo-tag,bar-tag

# set python and phobos environment
test_dir=$(dirname $(readlink -e $0))
. $test_dir/test_env.sh
. $test_dir/setup_db.sh
if [ "$CLEAN_ALL" -eq "1" ]; then
    drop_tables
    setup_tables
fi

# display error message and exits
function error {
    echo "$*"
    # Wait pending processes before exit and cleanup
    wait
    exit 1
}

# list <count> tapes matching the given pattern
# returns nodeset range
function get_tapes {
    local pattern=$1
    local count=$2

    mtx status | grep VolumeTag | sed -e "s/.*VolumeTag//" | tr -d " =" |
        grep "$pattern" | head -n $count | nodeset -f
}

# get <count> drives
function get_drives {
    local count=$1

    ls /dev/st[0-9]* | grep -P "st[0-9]+$" | head -n $count |
        xargs
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
    # no reformat if CLEAN_ALL == 0
    [ "$CLEAN_ALL" -eq "0" ] && return 0

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
    $phobos tape add -T $LTO5_TAGS -t lto5 "$lto5_tapes"

    # get LTO6 tapes
    local lto6_tapes="$(get_tapes L6 $N_TAPES)"
    echo "adding tapes $lto6_tapes with tags $LTO6_TAGS..."
    $phobos tape add -T $LTO6_TAGS -t lto6 "$lto6_tapes"

    # set tapes
    export tapes="$lto5_tapes,$lto6_tapes"

    # comparing with original list
    $phobos tape list | sort | xargs > /tmp/pho_tape.1
    echo "$tapes" | nodeset -e | sort > /tmp/pho_tape.2
    diff /tmp/pho_tape.1 /tmp/pho_tape.2
    rm -f /tmp/pho_tape.1 /tmp/pho_tape.2

    # show a tape info
    local tp1=$(echo $tapes | nodeset -e | awk '{print $1}')
    $phobos tape show $tp1

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
    $phobos drive show $dr1 --format=csv | grep "^locked" ||
        error "Drive should be added with locked state"

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
    $phobos -v tape format $lto5_tapes --unlock
    # format lto6 tapes
    $phobos -v tape format $lto6_tapes --unlock
}

function dir_setup
{
    export dirs="/tmp/test.pho.1 /tmp/test.pho.2"
    mkdir -p $dirs

    # no new directory registration if CLEAN_ALL == 0
    [ "$CLEAN_ALL" -eq "0" ] && return 0

    echo "adding directories $dirs"
    $phobos dir add -T $TAGS $dirs
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
    $phobos dir show $d1

    # unlock all directories but one
    for t in $(echo $dirs | nodeset -e -S '\n' | head -n 1); do
        $phobos dir unlock $t
    done
}

function dir_cleanup
{
    rm -rf /tmp/test.pho.1 /tmp/test.pho.2 "$PHO_TMP_DIR"
}

function lock_test
{
    # add 2 dirs: 1 locked, 1 unlocked
    # make sure they are inserted with the right status.
    local dir_prefix=/tmp/dir.$$
    mkdir $dir_prefix.1
    mkdir $dir_prefix.2
    $phobos dir add $dir_prefix.1
    $phobos dir show $dir_prefix.1 --format=csv | grep ",locked" ||
        error "Directory should be added with locked state"
    $phobos dir add $dir_prefix.2 --unlock
    $phobos dir show $dir_prefix.2 --format=csv | grep ",unlocked" ||
        error "Directory should be added with unlocked state"
    rmdir $dir_prefix.*
}

function put_get_test
{
    local md="a=1,b=2,c=3"
    local id=test/hosts.$$
    # phobos put
    $phobos put -m $md /etc/hosts $id

    # phobos get
    local out_file=/tmp/out
    rm -f $out_file
    $phobos get $id $out_file

    diff /etc/hosts $out_file
    rm -f $out_file

    local md_check=$($phobos getmd $id)
    [ "x$md" = "x$md_check" ] ||
        error "Object attributes do not match expectations"
}

# Test tag based media selection
function put_tags
{
    local md="a=1,b=2,c=3"
    local id=test/hosts2.$$
    local id2=test/hosts3.$$
    local first_tag=$(echo $TAGS | cut -d',' -f1)

    # phobos put with bad tags
    $phobos put -T $TAGS-bad -m $md /etc/hosts $id &&
        error "Should not be able to put objects with no media matching tags"
    $phobos put -T $first_tag-bad -m $md /etc/hosts $id &&
        error "Should not be able to put objects with no media matching tags"

    # phobos put with existing tags
    $phobos put -T $TAGS -m $md /etc/hosts $id ||
        error "Put with valid tags should have worked"
    $phobos put -T $first_tag -m $md /etc/hosts $id2 ||
        error "Put with one valid tag should have worked"
}

# make sure there are at least N available dir/drives
function ensure_nb_drives
{
    local count=$1
    local name
    local d

    if [[ $PHOBOS_LRS_default_family == "tape" ]]; then
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

# Execute a phobos command but sleep on layout_io for $1 second
function phobos_delayed_dev_release
{
    sleep_time="$1"
    shift
    (
        tmp_gdb_script=$(mktemp)
        trap "rm $tmp_gdb_script" EXIT
        cat <<EOF > "$tmp_gdb_script"
set breakpoint pending on
break layout_io
commands
shell sleep $sleep_time
continue
end
run $phobos $*
EOF

        gdb -batch -x "$tmp_gdb_script" -q python
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
    phobos_delayed_dev_release 2 put -m $md $tmp $key.1 &
    (( single==0 )) && phobos_delayed_dev_release 2 put -m $md $tmp $key.2 &

    # after 1 sec, make sure 2 devices and 2 media are locked
    sleep 1
    nb_lock=$($PSQL -qt -c "select * from media where lock != ''" \
              | grep -v "^$" | wc -l)
    echo "$nb_lock media are locked"
    if (( single==0 )) && (( $nb_lock != 2 )); then
        error "2 media locks expected (actual: $nb_lock)"
    elif (( single==1 )) && (( $nb_lock != 1 )); then
        error "1 media lock expected (actual: $nb_lock)"
    fi
    nb_lock=$($PSQL -qt -c "select * from device where lock != ''" \
              | grep -v "^$" | wc -l)
    echo "$nb_lock devices are locked"
    if (( single==0 )) && (( $nb_lock != 2 )); then
        error "2 device locks expected (actual: $nb_lock)"
    elif (( single==1 )) && (( $nb_lock != 1 )); then
        error "1 device lock expected (actual: $nb_lock)"
    fi

    wait

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
    files=$(echo $test_dir/*)

    # Concurrent put
    echo -n $files | xargs -n1 -P0 -d' ' -i $phobos put {} {} ||
        error "some phobos instances failed to put"

    # Concurrent get
    echo -n $files |
        xargs -n1 -P0 -d' ' -i bash -c \
            "$phobos get {} \"$PHO_TMP_DIR/\$(basename {})\"" ||
        error "some phobos instances failed to get"

    # Cleanup so that future gets can succeed
    rm -rf "$PHO_TMP_DIR"/*
}

function check_status
{
    local type="$1"
    local media="$2"

    for m in $media; do
        $phobos $type show "$m"
    done
}

function mtx_retry
{
    local retry_count=0
    while ! mtx $*; do
        echo "mtx failure, retrying in 1 sec" >&2
        ((retry_count++)) || true
        (( $retry_count > 5 )) && return 1
        sleep 1
    done
}

function drain_all_drives
{
#TODO replace umount/mtx unload by a 'phobos drive drain' command
    #umount all ltfs
    mount | awk '/^ltfs/ {print $3}' | xargs -r umount

    #unload all tapes
    mtx status | awk -F'[ :]' '/Data Transfer Element.*Full/ {print $8, $4}' |
        while read slot drive; do
            mtx_retry unload $slot $drive
        done
}

function lock_all_drives
{
    for d in $($phobos drive list); do
        $phobos drive lock "$d"
    done
}

function unlock_all_drives
{
    for d in $($phobos drive list); do
        $phobos drive unlock "$d"
    done
}

function tape_drive_compat
{
    $phobos put -T lto5 /etc/services svc_lto5 ||
        error "fail to put on lto5 tape"

    $phobos put -T lto6 /etc/services svc_lto6 ||
        error "fail to put on lto6 tape"

    $phobos get svc_lto5 /tmp/svc_lto5 ||
        error "fail to get from lto5 tape"
    rm /tmp/svc_lto5

    $phobos get svc_lto6 /tmp/svc_lto6 ||
        error "fail to get from lto6 tape"
    rm /tmp/svc_lto6

    #test with only one lto5 drive
    lock_all_drives
    drain_all_drives
    local lto5_d=$($phobos drive list -m ULT3580-TD5 | head -n 1)
    $phobos drive unlock $lto5_d
    $phobos get svc_lto5 /tmp/svc_lto5_from_lto5_drive ||
        error "fail to get data from lto5 tape with one lto5 drive"
    rm /tmp/svc_lto5_from_lto5_drive
    $phobos get svc_lto6 /tmp/svc_lto6_from_lto5_drive &&
        error "getting data from lto6 tape with one lto5 drive must fail"

    #test with only one lto6 drive
    lock_all_drives
    drain_all_drives
    local lto6_d=$($phobos drive list -m ULT3580-TD6 | head -n 1)
    $phobos drive unlock $lto6_d
    $phobos get svc_lto5 /tmp/svc_lto5_from_lto6_drive ||
        error "fail to get data from lto5 tape with one lto6 drive"
    rm /tmp/svc_lto5_from_lto6_drive
    $phobos get svc_lto6 /tmp/svc_lto6_from_lto6_drive ||
        error "fail to get data from lto6 tape with one lto6 drive"
    rm /tmp/svc_lto6_from_lto6_drive

    unlock_all_drives
}

echo "POSIX test mode"
export PHOBOS_LRS_default_family="dir"
PHO_TMP_DIR="$(mktemp -d)"
trap dir_cleanup EXIT
dir_setup
put_get_test
put_tags
concurrent_put
check_status dir "$dirs"
lock_test
concurrent_put_get_retry

if  [[ -w /dev/changer ]]; then
    echo "Tape test mode"
    if [ "$CLEAN_ALL" -eq "1" ]; then
        drop_tables
        setup_tables
    fi
    export PHOBOS_LRS_default_family="tape"
    tape_setup
    put_get_test
    put_tags
    concurrent_put
    check_status tape "$tapes"
    lock_test
    concurrent_put_get_retry
    tape_drive_compat
fi
