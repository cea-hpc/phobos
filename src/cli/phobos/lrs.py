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

from ctypes import byref
from phobos.ffi import LibPhobos, GenericError

from phobos.capi.const import (PHO_FS_LTFS, PHO_FS_POSIX,
                               PHO_DEV_DIR, PHO_DEV_TAPE)

from phobos.types import UnionId, MediaId


class LRS(LibPhobos):
    def fs_format(self, dss_client, medium_id, fs_type, unlock=False):
        """Format a medium though the LRS layer."""
        fs_type = fs_type.lower()
        if fs_type == 'ltfs':
            dev_type = PHO_DEV_TAPE
            fs_type_enum = PHO_FS_LTFS
        elif fs_type == 'posix':
            dev_type = PHO_DEV_DIR
            fs_type_enum = PHO_FS_POSIX
        else:
            raise GenericError("Unsupported operation")

        mstruct = MediaId(dev_type, UnionId(medium_id))

        res = self.libphobos.lrs_format(byref(dss_client.handle),
                                        byref(mstruct), fs_type_enum, unlock)
        if res != 0:
            raise GenericError("Cannot format medium '%s': %s" % \
                               (medium_id, os.strerror(abs(res))))
