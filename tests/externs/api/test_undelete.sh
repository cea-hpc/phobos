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

#
# Context initializer for delete API call tests
#

test_bin_dir=$(dirname $(readlink -e $0))
test_bin="$test_bin_dir/test_undelete"
. $test_bin_dir/../../test_env.sh
. $test_bin_dir/setup_db.sh
. $test_bin_dir/test_launch_daemon.sh

set -xe

function setup
{
    setup_tables
    $PSQL << EOF
    INSERT INTO deprecated_object VALUES
        ('test-oid1', '00112233445566778899aabbccddeef1', 1, '{}'),
        ('test-oid1', '00112233445566778899aabbccddeef1', 2, '{}'),
        ('test-oid2', '00112233445566778899aabbccddeef2', 1, '{}');
EOF
    invoke_lrs
}

function cleanup
{
    waive_lrs
    drop_tables
}

trap cleanup EXIT
setup

$LOG_COMPILER $test_bin
