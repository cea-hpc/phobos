#!/bin/sh

PY=$(which python)

# binary modules
PYBUILD_PATH=../build/lib.linux-x86_64-2.6
# pure-python modules
PYMODULES_PATH=../
# toplevel phobos library
PHOLIB_PATH=../../store/.libs/


export LD_LIBRARY_PATH=$PHOLIB_PATH
export PYTHONPATH="$PYBUILD_PATH:$PYMODULES_PATH"


for test_case in $(ls *Test.py)
do
    $PY $test_case
done
