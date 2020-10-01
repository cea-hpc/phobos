#!/usr/bin/env python3

#
#  All rights reserved (c) 2014-2020 CEA/DAM.
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
Utilities functions.
"""

from collections import OrderedDict

from phobos.core.ffi import DevInfo, MediaInfo

UNIT_PREFIXES = ['', 'K', 'M', 'G', 'T', 'P', 'E', 'Z', 'Y']

def num2human(n, unit='', base=1000, decimals=1):
    """Convert a number to a human readable string"""
    base = float(base)
    for prefix in UNIT_PREFIXES:
        if n < base:
            break
        n /= base

    return '{{:.{:d}f}}{:s}{:s}'.format(decimals, prefix, unit).format(n)

def bytes2human(n, *args, **kwargs):
    """Convert a size in bytes to a human readable string"""
    return num2human(n, 'B', 1024, *args, **kwargs)
