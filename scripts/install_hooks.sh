#!/bin/bash

# (c) 2017 CEA/DAM
# Licensed under the terms of the GNU Lesser GPL License version 2.1

script_dir=$(dirname $(readlink -m $0))
hook_dir=$(readlink -m $script_dir/../.git/hooks)

if [ ! -e $hook_dir/commit-msg ]; then
    echo "$hook_dir/commit-msg -> ../../scripts/commit-msg"
    ln -s "../../scripts/commit-msg" $hook_dir/commit-msg
fi

if [ ! -x $script_dir/commit-msg ]; then
    echo "chmod u+x $script_dir/commit-msg"
    chmod u+x $script_dir/commit-msg
fi

if [ ! -e $hook_dir/pre-commit ]; then
    echo "$hook_dir/pre-commit -> ../../scripts/pre-commit"
    ln -s "../../scripts/pre-commit" $hook_dir/pre-commit
fi

if [ ! -x $script_dir/pre-commit ]; then
    echo "chmod u+x $script_dir/pre-commit"
    chmod u+x $script_dir/pre-commit
fi
