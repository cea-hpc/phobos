#!/bin/bash

#
#  All rights reserved (c) 2014-2021 CEA/DAM.
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

test_bin_dir=$(dirname "${BASH_SOURCE[0]}")

. $test_bin_dir/test_env.sh
. $test_bin_dir/setup_db.sh
. $test_bin_dir/test_launch_daemon.sh


function cleanup() {
    waive_daemon
    drop_tables
}

trap drop_tables ERR EXIT

drop_tables
setup_tables
insert_examples
trap cleanup ERR EXIT
invoke_daemon

echo

echo "**** TESTS: TAGS a currently used dir ****"
# put first_tag on dir1
$phobos dir update --tags first_tag /tmp/pho_testdir1

# put an object asking a media with first_tag to "load" dir1
$phobos put --family dir --tags first_tag /etc/hosts dir_obj1

# put second_tag on dir1 whereas dir1 is "loaded" with old first_tag
$phobos dir update --tags second_tag /tmp/pho_testdir1

# put again one object using outdated tag and check it is updated
$phobos put --family dir --tags first_tag /etc/hosts dir_obj2 &&
    echo "Using an old tag to put should fail" && exit 1

if [[ -w /dev/changer ]]; then
    echo "**** TESTS: TAGS a currently used tape ****"
    # add and unlock one LTO6 drive
    $phobos drive add --unlock /dev/st4
    # add one LT06 tape with first_tag
    tape_name=$(mtx status | grep VolumeTag | sed -e "s/.*VolumeTag//" |
                tr -d " =" | grep "L6" | head -n 1)
    $phobos tape add -T first_tag -t lto6 $tape_name
    # format and unlock the LTO6 tape
    $phobos tape format --unlock $tape_name

    # put an object asking a media with first_tag to "load" the tape
    $phobos put --family tape --tags first_tag /etc/hosts tape_obj1

    # put second_tag on the tape whereas the tape is "loaded" with old first_tag
    $phobos tape update --tags second_tag $tape_name

    # put again one object using outdated tag and check it is updated
    $phobos put --family tape --tags first_tag /etc/hosts tape_obj2 &&
        echo "Using an old tag to put should fail" && exit 1
fi

echo "*** TEST END ***"
# Uncomment if you want the db to persist after test
# trap - EXIT ERR
