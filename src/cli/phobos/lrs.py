#!/usr/bin/python

#
#  All rights reserved (c) 2014-2017 CEA/DAM.
#
#  This file is part of Phobos.
#
#  Phobos is free software: you can redistribute it and/or modify it under
#  the terms of the GNU Lesser General Public License as published by
#  the Free Software Foundation, either version 2.1 of the License, or
#  (at your option) any later version.
#
#  Phobos is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU Lesser General Public License for more details.
#
#  You should have received a copy of the GNU Lesser General Public License
#  along with Phobos. If not, see <http://www.gnu.org/licenses/>.
#

"""
Provide access to LRS functionnality with the right level (tm) of abstraction.
"""

import os

from phobos.capi.const import (PHO_DEV_TAPE, PHO_DEV_DIR, PHO_FS_LTFS,
                               PHO_FS_POSIX)
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
