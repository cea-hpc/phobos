#!/bin/sh

PY=$(which python)

# pure-python modules
PHO_PYTHON_PATH=../

# toplevel phobos library
PHO_STORELIB_PATH=../../store/.libs/


export LD_LIBRARY_PATH=$PHO_STORELIB_PATH:$PHO_PYTHON_PATH
export PYTHONPATH="$PHO_PYTHON_PATH"


for test_case in *Test.py
do
    $PY $test_case || exit 1
done
