#!/usr/bin/env python3

#
#  All rights reserved (c) 2014-2022 CEA/DAM.
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
Provide access to LDM functionality with the right level (tm) of abstraction.
"""

from ctypes import byref, c_char_p, c_int, c_void_p, Structure

from phobos.core.ffi import LIBPHOBOS

class DevState(Structure): # pylint: disable=too-few-public-methods
    """Device information as managed by LDM."""
    _fields_ = [
        ('lds_family', c_int),
        ('lds_model', c_char_p),
        ('lds_serial', c_char_p),
    ]

    def __del__(self):
        """Free allocated memory on garbage collection"""
        LIBPHOBOS.ldm_dev_state_fini(byref(self))

class DevAdapter(Structure): # pylint: disable=too-few-public-methods
    """Opaque device handle."""
    _fields_ = [
        ('dev_lookup', c_void_p),
        ('dev_query', c_void_p),
        ('dev_load', c_void_p),
        ('dev_eject', c_void_p)
    ]
