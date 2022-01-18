#!/bin/sh -e
# -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
# vim:expandtab:shiftwidth=4:tabstop=4:

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

set -xe

test_bin_dir=$(dirname $(readlink -e $0))
test_bin="$test_bin_dir/test_concurrency"
. $test_bin_dir/../../test_env.sh
. $test_bin_dir/setup_db.sh
. $test_bin_dir/test_launch_daemon.sh
. $test_bin_dir/utils_generation.sh

function setup_dir()
{
    local dir="$DIR_TEST_OUT"
    local file="$FILES[0]"

    $phobos dir add "$dir"
    $phobos dir format --fs posix --unlock "$dir"
}

function setup()
{
    setup_tables

    export PHOBOS_LRS_families="dir"
    invoke_daemon

    setup_test_dirs
    setup_dummy_files 1

    setup_dir
}

function cleanup()
{
    cleanup_dummy_files
    cleanup_test_dirs

    waive_daemon
    drop_tables
}

trap cleanup EXIT
setup

$test_bin --num-threads 50 --file "${FILES[0]}" --action put --load-first
$test_bin --num-threads 50 --file "${FILES[0]}" --action put
