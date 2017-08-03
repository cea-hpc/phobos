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
Provide access to LDM functionnality with the right level (tm) of abstraction.
"""

import os

from ctypes import *

from phobos.ffi import LibPhobos


class DevState(Structure):
    """Device information as managed by LDM."""
    _fields_ = [
        ('lds_family', c_int),
        ('lds_model',  c_char_p),
        ('lds_serial', c_char_p),
        ('lds_loaded', c_bool)
    ]

class DevAdapter(Structure):
    """Opaque device handle."""
    _fields_ = [
        ('dev_lookup', c_void_p),
        ('dev_query',  c_void_p),
        ('dev_load',   c_void_p),
        ('dev_eject',  c_void_p)
    ]

class LDM(LibPhobos):
    def device_query(self, dev_type, dev_path):
        """Retrieve device information at LDM level."""
        adapter = DevAdapter()
        rc = self.libphobos.get_dev_adapter(dev_type, byref(adapter))
        if rc:
            self.logger.error("Cannot get device adapter for '%r'" % dev_type)
            return rc, None

        real_path = os.path.realpath(dev_path)
        state = DevState()

        rc = self.libphobos.ldm_dev_query(byref(adapter), real_path, byref(state))
        if rc:
            self.logger.error("Cannot retrieve device state for '%r'" % dev_path)
            return rc, None

        return 0, state
