#!/bin/sh

# -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
# vim:expandtab:shiftwidth=4:tabstop=4:

# This script compiles and installs the phobos RPM and configure a ready-to-use
# environment (for demo or prod tests)

# (c) 2014-2022 CEA/DAM
# Licensed under the terms of the GNU Lesser GPL License version 2.1

set -xe

# set phobos root as cwd from phobos/ci directory
cur_dir=$(dirname $(readlink -m $0))
cd "$cur_dir"/..

# compile phobos RPM
./autogen.sh
./configure $1

make rpm

# (re-)install RPM
if [ ! -z `type -t phobos` ]; then
    sudo yum -y remove phobos
fi
sudo yum -y install rpms/RPMS/x86_64/phobos-1*

# clean phobos compilation directory
cd ..
rm -rf phobos

# setup phobos database
sudo -u postgres phobos_db drop_db || true
sudo -u postgres phobos_db setup_db -s -p phobos

# create socket directory (in case we use the daemon in interactive mode)
if [ ! -d "/run/phobosd" ]; then
    sudo mkdir /run/phobosd
fi
