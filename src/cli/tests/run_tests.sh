#!/bin/sh

PY=$(which python)
ARCH=$(uname -m)
PY_VERSION=$($PY -c 'import sys; print "%d.%d" % (sys.version_info[0],\
                                                  sys.version_info[1])')

# pure-python modules
PHO_PYTHON_PATH=../build/lib.linux-$ARCH-$PY_VERSION/

# toplevel phobos library
PHO_STORELIB_PATH=../../store/.libs/


export LD_LIBRARY_PATH=$PHO_STORELIB_PATH:$PHO_PYTHON_PATH
export PYTHONPATH="$PHO_PYTHON_PATH"

cur_dir=$(dirname $(readlink -m $0))
. $cur_dir/../../../scripts/pho_dss_helper
drop_tables
setup_tables

conn_str="dbname=phobos user=phobos password=phobos"
export PHOBOS_DSS_connect_string="$conn_str"
for test_case in *Test.py
do
    $PY $test_case || exit 1
done

# clean test data
drop_tables
