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

function drain_all_drives
{
    # TODO replace umount/mtx unload by a 'phobos drive drain' command
    # umount all ltfs
    mount | awk '/^ltfs/ {print $3}' | xargs -r umount

    # unload all tapes
    mtx status | awk -F'[ :]' '/Data Transfer Element.*Full/ {print $8, $4}' |
        while read slot drive; do
            mtx_retry unload $slot $drive
        done
}

# list <count> tapes matching the given pattern
# returns nodeset range
function get_tapes {
    local pattern=$1
    local count=$2

    mtx status | sed -n 's/.*:VolumeTag\s\?=\s\?\(.*\)/\1/p' |
        grep "$pattern" | head -n $count | nodeset -f
}

# get <count> drives
function get_drives {
    local count=$1

    find /dev -regex "/dev/st[0-9]+$" | head -n $count | xargs
}

# get LTO<5|6> <count> drives
function get_lto_drives {
    local generation=$1
    local count=$2

    lsscsi | grep TD${generation} | awk '{print $6}' | head -n $count | xargs
}
