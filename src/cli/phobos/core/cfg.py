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
High level interface for managing configuration from CLI.
"""

from ctypes import byref, c_char_p
import errno
import os

from phobos.core.ffi import (LIBPHOBOS, ResourceFamily)

def load_file(path=None):
    """Load a configuration file from path"""
    ret = LIBPHOBOS.pho_cfg_init_local(path.encode('utf-8') if path else None)
    ret = abs(ret)
    if ret not in (0, errno.EALREADY):
        raise IOError(ret, "Phobos config init failed: %s" % os.strerror(ret))


# Singleton to differenciate cases where default was not provided and
# default=None
RAISE_ERROR = ()

def get_val(section, name, default=RAISE_ERROR):
    """Return the value of a property in a given section or an optional default
    value.
    Raise KeyError if the value has not been found in configuration and no
    default value has been provided.
    """
    cfg_value = c_char_p()
    ret = LIBPHOBOS.pho_cfg_get_val(section.encode('utf-8'),
                                    name.encode('utf-8'), byref(cfg_value))
    if ret != 0:
        if default is RAISE_ERROR:
            raise KeyError("No value in cfg for section '%s', key '%s', rc='%s'"
                           % (section, name, os.strerror(-ret)))
        return default
    return cfg_value.value.decode('utf-8')

def get_default_library(family):
    """Return the default library of family"""
    try:
        if family == ResourceFamily.RSC_TAPE:
            return get_val("store", "default_tape_library")
        elif family == ResourceFamily.RSC_DIR:
            return get_val("store", "default_dir_library")
        elif family == ResourceFamily.RSC_RADOS_POOL:
            return get_val("store", "default_rados_library")
        else:
            raise ValueError(f"bad ResourceFamily value {family}")
    except KeyError:
        raise KeyError("Default library is not set for '%s', must be defined "
                       "in Phobos configuration" % family)
