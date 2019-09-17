#!/bin/bash

# -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
# vim:expandtab:shiftwidth=4:tabstop=4:

# This scripts configure/compile/run phobos tests.

# (c) 2014-2019 CEA/DAM
# Licensed under the terms of the GNU Lesser GPL License version 2.1

set -xe

function pgsql_start() {
    if [ -n "$START_PGSQL" ]; then
        sudo -u postgres /usr/pgsql-9.4/bin/pg_ctl \
            -D /tmp/pg_data -l /tmp/postgres.log start
    fi
}

function pgsql_stop() {
    if [ -n "$START_PGSQL" ]; then
        sudo -u postgres /usr/pgsql-9.4/bin/pg_ctl -D /tmp/pg_data -m fast stop
    fi
}

#set phobos root as cwd from phobos/ci directory
cur_dir=$(dirname $(readlink -m $0))
cd "$cur_dir"/..

# export PKG_CONFIG_PATH=/usr/pgsql-9.4/lib/pkgconfig;
./autogen.sh
./configure
make rpm

make clean || cat src/tests/test-suite.log
make
# FIXME: when cloning the repo, some scripts do not have o+rx
# permissions, it is however necessary to execute them as postgres
chmod o+rx . .. ./scripts/phobos_db{,_local}

# Manually start postgresql (handy for container CI)
pgsql_start
trap pgsql_stop EXIT

sudo -u postgres ./scripts/phobos_db_local drop_db || true
sudo -u postgres ./scripts/phobos_db_local setup_db -s -p phobos
export VERBOSE=1 DEBUG=1
sudo -E make check
