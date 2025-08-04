#
#  All rights reserved (c) 2025 CEA/DAM.
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
# Context initializer for stat call tests
#

test_bin_dir=$(dirname $(readlink -e $0))
test_bin="$test_bin_dir/test_stats"
. $test_bin_dir/../../test_env.sh
. $test_bin_dir/setup_db.sh
. $test_bin_dir/test_launch_daemon.sh

set -xe

function setup
{
    setup_tables
    invoke_lrs
}

function cleanup
{
    waive_lrs
    drop_tables
}

trap cleanup EXIT
drop_tables
setup

$LOG_COMPILER $test_bin
