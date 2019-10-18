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
Provide access to LRS functionality with the right level (tm) of abstraction.
"""

import errno

from ctypes import byref

from phobos.core.ffi import LIBPHOBOS, LRS, MediaId
from phobos.core.const import (PHO_FS_LTFS, PHO_FS_POSIX,
                               PHO_DEV_DIR, PHO_DEV_TAPE)

def lrs_fs_format(dss, medium_id, fs_type, unlock=False):
    """Format a medium though the LRS layer."""
    fs_type = fs_type.lower()
    if fs_type == 'ltfs':
        dev_type = PHO_DEV_TAPE
        fs_type_enum = PHO_FS_LTFS
    elif fs_type == 'posix':
        dev_type = PHO_DEV_DIR
        fs_type_enum = PHO_FS_POSIX
    else:
        raise EnvironmentError(errno.EOPNOTSUPP,
                               "Unknown filesystem type '%s'" % fs_type)

    mstruct = MediaId(dev_type, medium_id)

    lrs = LRS()

    rc = LIBPHOBOS.lrs_init(byref(lrs), byref(dss.handle), None)
    if rc:
        raise EnvironmentError(rc, "Cannot initialize LRS")

    rc = LIBPHOBOS.lrs_format(byref(lrs), byref(mstruct), fs_type_enum, unlock)
    LIBPHOBOS.lrs_fini(byref(lrs))
    if rc:
        raise EnvironmentError(rc, "Cannot format medium '%s'" % medium_id)
