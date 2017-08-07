#!/bin/bash
# -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
# vim:expandtab:shiftwidth=4:tabstop=4:

set -e

# format and clean all by default
CLEAN_ALL=${CLEAN_ALL:-1}

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
    # no reformat if CLEAN_ALL == 0
    [ "$CLEAN_ALL" -eq "0" ] && return 0

    # make sure no LTFS filesystem is mounted, so we can unmount it
    systemctl stop ltfs || true
    # make systemctl actually perform "stop" the next time it is done
    systemctl start ltfs || true

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

    # need to format 2 tapes for concurrent_put
    local tp
    for tp in $tapes; do
        $phobos -v tape format $tp --unlock
    done
}

function dir_setup
{
    export dirs="/tmp/test.pho.1 /tmp/test.pho.2"
    mkdir -p $dirs

    # no new directory registration if CLEAN_ALL == 0
    [ "$CLEAN_ALL" -eq "0" ] && return 0

    echo "adding directories $dirs"
    $phobos dir add $dirs
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
    rm -rf /tmp/test.pho.1 /tmp/test.pho.2
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
    $phobos put -m $md $tmp $key.1 &
    (( single==0 )) && $phobos put -m $md $tmp $key.2 &

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
        error "2 media locks expected (actual: $nb_lock)"
    elif (( single==1 )) && (( $nb_lock != 1 )); then
        error "1 media lock expected (actual: $nb_lock)"
    fi

    wait

    rm -f $tmp

    # check they are both in DB
    $phobos getmd $key.1
    if (( single==0 )); then
	$phobos getmd $key.2
    fi
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
    concurrent_put
    check_status dir "$dirs"
    lock_test
    dir_cleanup
else
    echo "Tape test mode"
    export PHOBOS_LRS_default_family="tape"
    tape_setup
    put_get_test
    concurrent_put
    check_status tape "$tapes"
    lock_test
fi
