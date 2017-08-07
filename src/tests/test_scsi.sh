#!/bin/bash

#
# Wrapper to skip SCSI tests when /dev/changer is not writable
#

if  [[ -w /dev/changer ]]; then
    echo "library is accessible"
    echo "changer:"
    ls -ld /dev/changer

    # make sure no process uses the drive
    systemctl stop ltfs || true
    # make systemctl actually perform "stop" the next time it is done
    systemctl start ltfs || true

    ./test_scsi
else
    ls -ld /dev/changer
    echo "Cannot access library: test skipped"
    exit 77 # special value to mark test as 'skipped'
fi

# -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
# vim:expandtab:shiftwidth=4:tabstop=4:
