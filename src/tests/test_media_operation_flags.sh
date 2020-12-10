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

trap drop_tables ERR EXIT

drop_tables
setup_tables
insert_examples

echo

echo "**** TESTS: CLI LIST MEDIA OPERATION TAGS ****"
$phobos dir list -o put_access,get_access,delete_access

echo "*** TEST END ***"
# Uncomment if you want the db to persist after test
# trap - EXIT ERR
