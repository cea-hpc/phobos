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
Object-oriented wrappers over phobos external types, i.e. the ones shared
between modules.
"""

from ctypes import *

from phobos.ffi import LibPhobos
from phobos.capi.const import PHO_LABEL_MAX_LEN, NAME_MAX

class UnionId(Union):
    """Media ID union type."""
    _fields_ = [
        ('label', c_char * PHO_LABEL_MAX_LEN),
        ('path', c_char * NAME_MAX),
    ]

class Lock(Structure):
    """Resource lock as managed by DSS."""
    _fields_ = [
        ('lock_ts', c_longlong),
        ('lock', c_char_p)
    ]

class DevInfo(Structure):
    """DSS device descriptor."""
    _fields_ = [
        ('family', c_int),
        ('model', c_char_p),
        ('path', c_char_p),
        ('host', c_char_p),
        ('serial', c_char_p),
        ('adm_status', c_int),
        ('lock', Lock)
    ]

class MediaId(Structure):
    """Generic media identifier."""
    _fields_ = [
        ('type', c_int),
        ('id_u', UnionId)
    ]

class MediaFS(Structure):
    """Media filesystem descriptor."""
    _fields_ = [
        ('type', c_int),
        ('status', c_int),
        ('label', c_char * (PHO_LABEL_MAX_LEN + 1))
    ]

class MediaStats(Structure):
    """Media usage descriptor."""
    _fields_ = [
        ('nb_obj', c_longlong),
        ('logc_spc_used', c_longlong),
        ('phys_spc_used', c_longlong),
        ('phys_spc_free', c_longlong),
        ('nb_load', c_long),
        ('nb_errors', c_long),
        ('last_load', c_longlong)
    ]

class MediaInfo(Structure):
    """DSS media descriptor."""
    _fields_ = [
        ('id', MediaId),
        ('addr_type', c_int),
        ('model', c_char_p),
        ('adm_status', c_int),
        ('fs', MediaFS),
        ('stats', MediaStats),
        ('lock', Lock),
    ]
