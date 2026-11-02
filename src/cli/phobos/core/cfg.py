#!/usr/bin/env python3

#
#  All rights reserved (c) 2014-2026 CEA/DAM.
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

import configparser
from ctypes import byref, c_char_p
import errno
import os
import re

from phobos.core.ffi import (LIBPHOBOS, ResourceFamily)

# Put this in a CFG class, like Store/Admin Clients
LIBPHOBOS.phobos_init_without_dss()

def load_file(path=None):
    """Load a configuration file from path"""
    ret = LIBPHOBOS.pho_cfg_init_local(path.encode('utf-8') if path else None)
    ret = abs(ret)
    if ret not in (0, errno.EALREADY):
        raise IOError(ret, f"Phobos config init failed: {os.strerror(ret)}")


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
    ret = LIBPHOBOS.phobos_get_cfg_val(section.encode('utf-8'),
                                       name.encode('utf-8'), byref(cfg_value))
    if ret != 0:
        if default is RAISE_ERROR:
            if ret == -errno.ENODATA:
                raise KeyError(f"No value in cfg for section '{section}',"
                               f"key '{name}', rc='{os.strerror(-ret)}'")
            raise IOError(-ret, os.strerror(-ret))
        return default
    return cfg_value.value.decode('utf-8') # pylint: disable=no-member

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
    except KeyError as err:
        raise KeyError(f"Default library is not set for '{family}', must be "
                       "defined in Phobos configuration") from err

def get_default_copy_name():
    """Return the default copy name"""
    try:
        return get_val("copy", "default_copy_name")
    except KeyError as err:
        raise KeyError("Default copy name is not set, must be defined in Phobos"
                       "configuration") from err

# Tape libraries are declared as 'tlc "<name>"' sections in the configuration
# file, as built by TLC_SECTION_CFG in the C sources.
TLC_SECTION_MATCHER = re.compile(r'^tlc\s+"(?P<name>[^"]+)"$')

# Default location of the configuration file, as defined by PHO_DEFAULT_CFG in
# the C sources (not exposed through the Python bindings).
DEFAULT_CFG_FILE = '/etc/phobos.conf'

def get_library_list(cfg_file=None):
    """Return the names of the tape libraries declared in the configuration
    file, in alphabetical order.

    The configuration file is resolved like pho_cfg_init_local() does:
    explicit cfg_file argument, then PHOBOS_CFG_FILE environment variable,
    then the default location.

    Raise an EnvironmentError if the configuration file cannot be read.
    """
    if not cfg_file:
        cfg_file = os.environ.get("PHOBOS_CFG_FILE", DEFAULT_CFG_FILE)

    parser = configparser.ConfigParser(interpolation=None, strict=False)
    with open(cfg_file, encoding='utf-8') as cfg_fd:
        parser.read_file(cfg_fd)

    libraries = []
    for section in parser.sections():
        match = TLC_SECTION_MATCHER.match(section.strip())
        if match:
            libraries.append(match.group('name'))

    return sorted(libraries)
