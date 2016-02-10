#!/usr/bin/python

# Copyright CEA/DAM 2015
# This file is part of the Phobos project

"""
Provide access to LRS functionnality with the right level (tm) of abstraction.
"""

import os

from phobos.capi.dss import PHO_DEV_TAPE, PHO_DEV_DIR
from phobos.capi.dss import PHO_FS_LTFS, PHO_FS_POSIX
from phobos.capi.dss import media_id, media_id_set

from phobos.capi.lrs import lrs_format

from phobos.dss import GenericError


def fs_format(dss_client, medium_id, fs_type, unlock=False):
    """Format a medium though the LRS layer."""
    mstruct = media_id()
    if fs_type.lower() == 'ltfs':
        mstruct.type = PHO_DEV_TAPE
        fs_type_enum = PHO_FS_LTFS
    elif fs_type.lower() == 'posix':
        mstruct.type = PHO_DEV_DIR
        fs_type_enum = PHO_FS_POSIX
    else:
        raise GenericError("Unsupported operation")
    media_id_set(mstruct, medium_id)

    res = lrs_format(dss_client.handle, mstruct, fs_type_enum, unlock)
    if res != 0:
        raise GenericError("Cannot format medium '%s': %s" % \
                           (medium_id, os.strerror(abs(res))))
