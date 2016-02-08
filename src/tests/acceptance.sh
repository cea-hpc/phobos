#!/bin/bash
# -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
# vim:expandtab:shiftwidth=4:tabstop=4:

set -e

# set python and phobos environment
test_dir=$(dirname $(readlink -e $0))
. $test_dir/test_env.sh
. $test_dir/setup_db.sh
drop_tables
setup_tables

# display error message and exits
function error {
    echo "$*"
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

    ls /dev/IBMtape[0-9]* | grep -P "IBMtape[0-9]+$" | head -n $count |
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
    # make sure no LTFS filesystem is mounted, so we can unmount it
    service ltfs stop
    #  make sure all drives are empty
    empty_drives

    local N_TAPES=2
    local N_DRIVES=2

    # get LTO5 tapes
    export tapes="$(get_tapes L5 $N_TAPES)"
    local type=lto5
    if [ -z "$tapes" ]; then
        # if there are none, get LTO6 tapes
        export tapes="$(get_tapes L6 $N_TAPES)"
        type=lto6
    fi
    echo "adding tapes $tapes..."
    $phobos tape add -t $type "$tapes"

    # comparing with original list
    $phobos tape list | sort | xargs > /tmp/pho_tape.1
    echo "$tapes" | nodeset -e | sort > /tmp/pho_tape.2
    diff /tmp/pho_tape.1 /tmp/pho_tape.2
    rm -f /tmp/pho_tape.1 /tmp/pho_tape.2

    # show a tape info
    local tp1=$(echo $tapes | nodeset -e | awk '{print $1}')
    $phobos tape show $tp1

    # unlock all tapes but one
    for t in $(echo $tapes | nodeset -e -S '\n' | head -n $(($N_TAPES - 1))); do
        $phobos tape unlock $t
    done

    # get drives
    local drives=$(get_drives $N_DRIVES)
    echo "adding drives $drives..."
    $phobos drive add $drives

    # show a drive info
    local dr1=$(echo $drives | awk '{print $1}')
    echo "$dr1"
    # check drive status
    $phobos drive show $dr1 --format=csv | grep ",locked" ||
        error "Drive should be added with locked state"

    # unlock all drives but one
    local serials=$($phobos drive list | head -n $(($N_DRIVES - 1)))
    for d in $serials; do
        echo $d
        $phobos drive unlock $d
    done

    # format a single tape (make test shorter)
    local tp=$(echo $tapes | nodeset -e | awk '{print $1}')
    $phobos tape format $tp --unlock
}

function dir_setup
{
    export dirs="/tmp/test.pho.1 /tmp/test.pho.2"
    mkdir -p $dirs
    echo "adding directories $dirs"
    $phobos dir add --unlock $dirs
    $phobos dir format --fs posix $dirs

    # comparing with original list
    $phobos dir list | sort > /tmp/pho_dir.1
    :> /tmp/pho_dir.2
    # directory ids are <hostname>:<path>
    for d in $dirs; do
        echo "$(hostname -s):$d" >> /tmp/pho_dir.2
    done
    diff /tmp/pho_dir.1 /tmp/pho_dir.2
    rm -f /tmp/pho_dir.1 /tmp/pho_dir.2

    # show a directory info
    d1=$(echo $dirs | nodeset -e | awk '{print $1}')
    $phobos dir show $d1

    # FIXME dir unlock is not implemented
    # unlock all directories but one
    #for t in $(echo $dirs | nodeset -e -S '\n' | head -n 1); do
    #    $phobos dir unlock $t
    #done
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
    [ $md = $md_check ] || error "Object attributes to not match expectations"
}

function check_status
{
    local type="$1"
    local media="$2"

    for m in $media; do
        $phobos $type show "$m"
    done
}

if  [[ ! -w /dev/changer ]]; then
    echo "Cannot access library: switch to POSIX test"
    export PHOBOS_LRS_default_family="dir"
    dir_setup
    put_get_test
    check_status dir "$dirs"
    lock_test
else
    echo "Tape test mode"
    export PHOBOS_LRS_default_family="tape"
    tape_setup
    put_get_test
    check_status tape "$tapes"
    lock_test
fi
