#!/usr/bin/env python2

#
#  All rights reserved (c) 2014-2019 CEA/DAM.
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
Provide access to admin commands with the right level (tm) of abstraction.
"""

import errno
import os

from ctypes import *

from phobos.core.const import (PHO_FS_LTFS, PHO_FS_POSIX,
                               PHO_RSC_DIR, PHO_RSC_TAPE)
from phobos.core.dss import DSSHandle
from phobos.core.ffi import CommInfo, LIBPHOBOS_ADMIN, LRS, Id

class AdminHandle(Structure):
    """Admin handler"""
    _fields_ = [
        ('dss', DSSHandle),
        ('lrs', LRS),
        ('comm', CommInfo),
        ('dir_sock_path', c_char_p),
    ]

class Client(object):
    """Wrapper on the phobos admin client"""
    def __init__(self):
        super(Client, self).__init__()
        self.handle = None

    def init(self, lrs_required):
        if self.handle is not None:
            self.fini()

        self.handle = AdminHandle()

        rc = LIBPHOBOS_ADMIN.phobos_admin_init(byref(self.handle), lrs_required)
        if rc:
            raise EnvironmentError(rc, 'Admin initialization failed')

    def fini(self):
        if self.handle is not None:
            LIBPHOBOS_ADMIN.phobos_admin_fini(byref(self.handle))
            self.handle = None

    def fs_format(self, medium_id, fs_type, unlock=False):
        """Format a medium through the LRS layer."""
        fs_type = fs_type.lower()
        if fs_type == 'ltfs':
            rsc_family = PHO_RSC_TAPE
            fs_type_enum = PHO_FS_LTFS
        elif fs_type == 'posix':
            rsc_family = PHO_RSC_DIR
            fs_type_enum = PHO_FS_POSIX
        else:
            raise EnvironmentError(errno.EOPNOTSUPP,
                                   "Unknown filesystem type '%s'" % fs_type)

        mstruct = Id(rsc_family, medium_id)
        rc = LIBPHOBOS_ADMIN.phobos_admin_format(byref(self.handle),
                                                 byref(mstruct), fs_type_enum,
                                                 unlock)
        if rc:
            raise EnvironmentError(rc, "Cannot format medium '%s'" % medium_id)

    def device_add(self, dev_family, dev_name):
        """Add a device to the LRS."""
        rc = LIBPHOBOS_ADMIN.phobos_admin_device_add(byref(self.handle),
                                                     dev_family, dev_name)

        if rc:
            raise EnvironmentError(rc, "Cannot add device '%s' to the LRS"
                                       % dev_name)
