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
Foreign Function Interface (FFI) over libphobos API.

This module wraps calls from the library and expose them under an
object-oriented API to the rest of the CLI.
"""

from ctypes import CDLL


class GenericError(Exception):
    """Base error to describe DSS failures."""

class LibPhobos(object):
    """Low level phobos API abstraction class to expose calls to CLI"""
    LIBPHOBOS_NAME = "libphobos_store.so"
    def __init__(self, *args, **kwargs):
        """Get a handler over the library"""
        super(LibPhobos, self).__init__(*args, **kwargs)
        self.libphobos = CDLL(self.LIBPHOBOS_NAME)
