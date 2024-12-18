#!/bin/sh

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

cur_dir=$(dirname $(readlink -m $0))

PY=$(which python3)
ARCH=$(uname -m)
PY_VERSION=$($PY --version 2>&1 | cut -d' ' -f2 | cut -d'.' -f1,2)

# pure-python modules
PHO_PYTHON_PATH="$cur_dir/../build/lib.linux-$ARCH-$PY_VERSION/"

# toplevel phobos library
PHO_STORELIB_PATH="$cur_dir/../../store/.libs/"
PHO_ADMINLIB_PATH="$cur_dir/../../admin/.libs/"
PHO_LAYOUTLIB_PATH="$cur_dir/../../layout-modules/.libs/"
PHO_LDMLIB_PATH="$cur_dir/../../ldm-modules/.libs/"
PHO_IOLIB_PATH="$cur_dir/../../io-modules/.libs/"

export LD_LIBRARY_PATH="$PHO_STORELIB_PATH:$PHO_ADMINLIB_PATH:\
$PHO_PYTHON_PATH:$PHO_LAYOUTLIB_PATH:$PHO_LDMLIB_PATH:$PHO_IOLIB_PATH"
export PYTHONPATH="$PHO_PYTHON_PATH"

PHOBOS_DB="$cur_dir/../../../scripts/phobos_db"

export PHOBOS_CFG_FILE="$cur_dir/../../../tests/phobos.conf"
# get the connect string from the test conf, get the string after the first '='
conn_str="$(grep "dbname" "$PHOBOS_CFG_FILE")"
conn_str="${conn_str#*=}"

export PHOBOS_DSS_connect_string="$conn_str"
export phobos="$cur_dir/../scripts/phobos"
export phobosd="$cur_dir/../../lrs/phobosd"

TESTS=$(ls *Test.py)

# If rados is not enabled, remove RADOS tests from TESTS to execute
if [ "x$1" != "xrados" ]; then
    TESTS=${TESTS//PhobosCLIRADOSTest.py/}
fi

for test_case in $TESTS
do
    $PHOBOS_DB drop_tables
    $PHOBOS_DB setup_tables
    $PY $test_case || exit 1
done

# clean test data
$PHOBOS_DB drop_tables
