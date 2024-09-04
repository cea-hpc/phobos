#!/bin/bash

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

function drain_all_drives_changer
{
    local dev_changer=$1
    local i

    # TODO replace umount/mtx unload by a 'phobos drive drain' command
    # umount all ltfs
    mount | awk '/^ltfs/ {print $3}' | xargs -r umount

    # unload all tapes
    local drive_full=( $(mtx -f ${dev_changer} status | \
              awk -F'[ :]' '/Data Transfer Element.*Full/ {print $4}') )
    local nb_drive_full=${#drive_full[@]}
    if [[ "${nb_drive_full}" != 0 ]]; then
        local slot_empty=( $(mtx -f ${dev_changer} status | \
                             grep "Storage Element" | \
                             grep -v "IMPORT/EXPORT" | \
                             grep Empty | awk '{print $3}' | cut -f 1 -d ':') )

        for i in `seq 1 ${nb_drive_full}`; do
            mtx_retry -f ${dev_changer} unload ${slot_empty[$i - 1]} \
                      ${drive_full[$i - 1]}
        done
    fi
}

function drain_all_drives
{
    drain_all_drives_changer /dev/changer
    drain_all_drives_changer /dev/changer_bis
}

# list <count> tapes matching the given pattern
# returns nodeset range
function get_tapes {
    local pattern=$1
    local count=$2

    mtx status | sed -n 's/.*:VolumeTag\s\?=\s\?\(.*\)/\1/p' |
        grep "$pattern" | head -n $count | nodeset -f
}

# list <count> tapes from the secnd library matching the given pattern
# returns nodeset range
function get_tapes_bis {
    local pattern=$1
    local count=$2

    mtx -f /dev/changer_bis status |
        sed -n 's/.*:VolumeTag\s\?=\s\?\(.*\)/\1/p' |
        grep "$pattern" | head -n $count | nodeset -f
}

# get LTO<5|6> <count> drives
function get_lto_drives {
    local generation=$1
    local count=$2

    lsscsi | grep TD${generation} | awk '{print $6}' | head -n $count | xargs
}

# get LTO<5|6> <count> drives from the second library
function get_lto_drives_bis {
    local generation=$1
    local count=$2

    lsscsi | grep TD${generation} | awk '{print $6}' | tail -n $count | xargs
}
