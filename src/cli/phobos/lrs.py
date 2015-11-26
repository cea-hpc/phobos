#!/usr/bin/python

# Copyright CEA/DAM 2015
# This file is part of the Phobos project

"""
Provide access to LRS functionnality with the right level (tm) of abstraction.
"""

import os

from phobos.capi.dss import PHO_DEV_TAPE, PHO_FS_LTFS, media_id, media_id_set
from phobos.capi.lrs import lrs_format

from phobos.dss import GenericError


def fs_format(dss_client, medium_id, fs_type, unlock=False):
    """Format a medium though the LRS layer."""
    # As for now, the 'format' command can only work with LTFS and tapes.
    if fs_type.lower() != 'ltfs':
        raise GenericError('Operation not supported')

    mstruct = media_id()
    mstruct.type = PHO_DEV_TAPE
    media_id_set(mstruct, medium_id)

    res = lrs_format(dss_client.handle, mstruct, PHO_FS_LTFS, unlock)
    if res != 0:
        raise GenericError("Cannot format medium '%s': %s" % \
                           (medium_id, os.strerror(abs(res))))
