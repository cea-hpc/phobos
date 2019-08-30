#!/bin/sh

# -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
# vim:expandtab:shiftwidth=4:tabstop=4:

# This scripts relies on quadstor to setup a virtual tape library needed by
# phobos CI tests.
# (This script was tested with quadstor-vtl-ext-3.0.28.)

# (c) 2014-2019 CEA/DAM
# Licensed under the terms of the GNU Lesser GPL License version 2.1

set -xe

# Create a loopback device for storage
VTL_STORAGE=/tmp/vtl_storage
dd if=/dev/zero of=$VTL_STORAGE bs=1M count=5000
losetup /dev/loop0 $VTL_STORAGE
mdadm --create /dev/md0 --level=linear --force --raid-devices=1 /dev/loop0
systemctl restart quadstorvtl

# Create a pool with no dedup (dedup needs at least 11G disk space)
/quadstorvtl/bin/spconfig -a -g phobos-pool

# Give storage device to quadstorvtl
/quadstorvtl/bin/bdconfig -a -d /dev/md0 -g phobos-pool

# VTL types:
# 09      IBM System Storage TS3100
# Drive type:
# 20      IBM 3580 Ultrium5
# 21      IBM 3580 Ultrium6
# Note: --drive-{type,count} are repeatable
# TODO: add LTO8 drives as well
/quadstorvtl/bin/vtconfig --add --vtl=phobos-ibm \
    --type=09 --slots=20 --ieports=4 --drive-type=20 --drive-count=4 \
    --drive-type=21 --drive-count=4

# Add 20 cartridges (tapes), types:
# 12      LTO 5 1500GB
# 13      LTO 6 2500GB
# TODO: add LTO8 tapes as well
/quadstorvtl/bin/vcconfig --add --vtl=phobos-ibm \
    --type 12 --pool=phobos-pool --label=P00000 --count=10
/quadstorvtl/bin/vcconfig --add --vtl=phobos-ibm \
    --type 13 --pool=phobos-pool --label=Q00000 --count=10

# Symlink for phobos tests
ln -s /dev/sg9 /dev/changer
