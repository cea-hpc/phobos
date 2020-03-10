#!/usr/bin/env python2

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
Multi-layer logging system to bind phobos logging abilities to python logging
module.
"""

import os

from logging import getLevelName
from logging import CRITICAL, ERROR, WARNING, INFO, DEBUG

from ctypes import CFUNCTYPE, POINTER

from phobos.core.ffi import LIBPHOBOS, PhoLogRec

from phobos.core.const import (PHO_LOG_DISABLED, PHO_LOG_ERROR, PHO_LOG_WARN,
                               PHO_LOG_INFO, PHO_LOG_VERB, PHO_LOG_DEBUG,
                               PHO_LOG_DEFAULT)

# Phobos extra logging levels
DISABLED = CRITICAL + 10
VERBOSE = (INFO + DEBUG) / 2


class LogControl(object):
    """Log controlling class. Wraps phobos low-level logging API."""
    # Log handling callback type for use w/ python callables
    LogCBType = CFUNCTYPE(None, POINTER(PhoLogRec))

    def __init__(self, *args, **kwargs):
        """Initialize fresh instance."""
        super(LogControl, self).__init__(*args, **kwargs)
        self._cb_ref = None

    def set_callback(self, callback):
        """Set a python callable as a log handling callback to the C library."""
        if callback is None:
            set_cb = cast(None, self.LogCBType)
        else:
            set_cb = self.LogCBType(callback)

        LIBPHOBOS.pho_log_callback_set(set_cb)

        self._cb_ref = set_cb

    def set_level(self, lvl):
        """Set the library logging level."""
        LIBPHOBOS.pho_log_level_set(self.level_py2pho(lvl))

    @staticmethod
    def level_pho2py(py_level):
        """Convert phobos log level to python standard equivalent."""
        levels_map = {
            PHO_LOG_DISABLED: DISABLED,
            PHO_LOG_ERROR: ERROR,
            PHO_LOG_WARN: WARNING,
            PHO_LOG_INFO: INFO,
            PHO_LOG_VERB: VERBOSE,
            PHO_LOG_DEBUG: DEBUG
        }
        return levels_map.get(py_level, INFO)

    @staticmethod
    def level_py2pho(py_level):
        """Convert standard python levels to phobos levels."""
        levels_map = {
            DISABLED: PHO_LOG_DISABLED,
            CRITICAL: PHO_LOG_ERROR,
            ERROR: PHO_LOG_ERROR,
            WARNING: PHO_LOG_WARN,
            INFO: PHO_LOG_INFO,
            VERBOSE: PHO_LOG_VERB,
            DEBUG: PHO_LOG_DEBUG
        }
        return levels_map.get(py_level, PHO_LOG_DEFAULT)

    @staticmethod
    def level_name(lvl):
        """Wrapper to get the log level name including custom level names."""
        if lvl == DISABLED:
            return 'DISABLED'
        elif lvl == VERBOSE:
            return 'VERBOSE'
        else:
            return getLevelName(lvl)
