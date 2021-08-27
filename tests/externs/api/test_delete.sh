#!/bin/bash

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

#
# Context initializer for delete API call tests
#

set -e

test_bin_dir=$(dirname $(readlink -e $0))
test_bin="$test_bin_dir/test_delete"
. $test_bin_dir/../../test_env.sh
. $test_bin_dir/setup_db.sh
. $test_bin_dir/test_launch_daemon.sh

function clean_test
{
    waive_daemon
    drop_tables
}

trap clean_test ERR EXIT

drop_tables
setup_tables

psql phobos phobos << EOF
    INSERT INTO object VALUES ('test-oid1', '00112233445566778899aabbccddeef1',\
                               1, '{}'),
                              ('test-oid2', '00112233445566778899aabbccddeef2',\
                               1, '{}'),
                              ('test-oid3', '00112233445566778899aabbccddeef3',\
                               1, '{}');
EOF

invoke_daemon
$test_bin || exit 1

# -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
# vim:expandtab:shiftwidth=4:tabstop=4:
