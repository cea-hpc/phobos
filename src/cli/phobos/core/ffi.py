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
High level interface over libphobos API.

This module wraps calls from the library and expose them under an
object-oriented API to the rest of the CLI.
"""

import logging

from ctypes import *

from phobos.core.const import PHO_LABEL_MAX_LEN, NAME_MAX
from phobos.core.const import fs_type2str, fs_status2str
from phobos.core.const import adm_status2str, dev_family2str

LIBPHOBOS_NAME = "libphobos_store.so"
LIBPHOBOS = CDLL(LIBPHOBOS_NAME)

class CLIManagedResourceMixin(object):
    """Interface for objects directly exposed/manipulated by the CLI."""
    def get_display_fields(self):
        """Return a dict of available fields and optional display formatters."""
        raise NotImplementedError("Abstract method subclasses must implement.")

    def get_display_dict(self, numeric=False):
        """
        Return a dict representing the structure as we want it to be displayed,
        i.e.: w/ only the desired fields and w/ conversion methods applied.
        """
        export = {}
        disp_fields = self.get_display_fields()
        for key in sorted(disp_fields.keys()):
            if not numeric and disp_fields[key]:
                conv = disp_fields.get(key, str)
            else:
                conv = str
            export[key] = conv(getattr(self, key))
        return export

class UnionId(Union):
    """Media ID union type."""
    _fields_ = [
        ('label', c_char * PHO_LABEL_MAX_LEN),
        ('path', c_char * NAME_MAX)
    ]

class DSSLock(Structure):
    """Resource lock as managed by DSS."""
    _fields_ = [
        ('lock_ts', c_longlong),
        ('lock', c_char_p)
    ]

class DevInfo(Structure, CLIManagedResourceMixin):
    """DSS device descriptor."""
    _fields_ = [
        ('family', c_int),
        ('model', c_char_p),
        ('path', c_char_p),
        ('host', c_char_p),
        ('serial', c_char_p),
        ('adm_status', c_int),
        ('lock', DSSLock)
    ]

    def get_display_fields(self):
        """Return a dict of available fields and optional display formatters."""
        return {
            'adm_status': adm_status2str,
            'family': dev_family2str,
            'host': None,
            'model': None,
            'path': None,
            'serial': None,
            'lock_status': None,
            'lock_ts': None
        }

    @property
    def lock_status(self):
        """ Wrapper to get lock status"""
        return self.lock.lock

    @property
    def lock_ts(self):
        """ Wrapper to get lock timestamp"""
        return self.lock.lock_ts

class Tags(Structure):
    """List of tags"""
    _fields_ = [
        ('tags', POINTER(c_char_p)),
        ('n_tags', c_size_t),
    ]

    def __init__(self, tag_list):
        """Allocate C resources to create a native Tags instance from a python
        list of strings.
        """
        if not tag_list:
            self.tags = None
            self.n_tags = 0
        else:
            tags = (c_char_p * len(tag_list))(*tag_list)
            LIBPHOBOS.tags_init(byref(self), tags, len(tag_list))

    def free(self):
        """Free all allocated resources. Only call this if tag values were
        dynamically allocated (and not borrowed from another structure).
        """
        LIBPHOBOS.tags_free(byref(self))

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

class MediaInfo(Structure, CLIManagedResourceMixin):
    """DSS media descriptor."""
    _fields_ = [
        ('id', MediaId),
        ('addr_type', c_int),
        ('model', c_char_p),
        ('adm_status', c_int),
        ('fs', MediaFS),
        ('stats', MediaStats),
        ('_tags', Tags),
        ('lock', DSSLock)
    ]

    def get_display_fields(self):
        """Return a dict of available fields and optional display formatters."""
        return {
            'adm_status': adm_status2str,
            'addr_type': None,
            'model': None,
            'ident': None,
            'tags': None,
            'lock_status': None,
            'lock_ts': None
        }

    def get_display_dict(self, numeric=False):
        """Update level0 representation with nested structures content."""
        export = super(MediaInfo, self).get_display_dict()
        export.update(self.expanded_fs_info)
        export.update(self.expanded_stats)
        return export

    def is_locked(self):
        """True if this media is locked"""
        return self.lock_status != ""

    @property
    def lock_status(self):
        """Wrapper to get lock status"""
        return self.lock.lock

    @property
    def lock_ts(self):
        """Wrappert to get lock timestamp"""
        return self.lock.lock_ts

    @property
    def ident(self):
        return self.id.id_u.label

    @property
    def expanded_fs_info(self):
        """Wrapper to get media fs info as dict"""
        fs_info = {
            'fs.type': fs_type2str(self.fs.type),
            'fs.status': fs_status2str(self.fs.status),
            'fs.label': self.fs.label
        }
        return fs_info

    @property
    def expanded_stats(self):
        """Wrapper to get media stats as dict"""
        return {
            'stats.nb_obj': self.stats.nb_obj,
            'stats.logc_spc_used': self.stats.logc_spc_used,
            'stats.phys_spc_used': self.stats.phys_spc_used,
            'stats.phys_spc_free': self.stats.phys_spc_free,
            'stats.nb_load': self.stats.nb_load,
            'stats.nb_errors': self.stats.nb_errors,
            'stats.last_load': self.stats.last_load
        }

    @property
    def tags(self):
        return self._tags.tags[:self._tags.n_tags]

    @tags.setter
    def tags(self, tags):
        self._tags.free()
        self._tags = Tags(tags)

class Timeval(Structure):
    """standard struct timeval."""
    _fields_ = [
        ('tv_sec', c_long),
        ('tv_usec', c_long)
    ]

class PhoLogRec(Structure):
    """Single log record."""
    _fields_ = [
        ('plr_level', c_int),
        ('plr_pid', c_int),
        ('plr_file', c_char_p),
        ('plr_func', c_char_p),
        ('plr_line', c_int),
        ('plr_err', c_int),
        ('plr_time', Timeval),
        ('plr_msg', c_char_p)
    ]

def pho_rc_check(rc, func, args):
    """Helper to be set as errcheck for phobos functions returning rc"""
    if rc:
        # Work with CFUNCTYPE and lib functions
        try:
            name = func.__name__
        except AttributeError:
            name = str(func)

        raise EnvironmentError(
            -rc, "%s(%s) failed" % (name, ", ".join(map(repr, args)))
        )

    # Convention to signal ctypes to leave the return value untouched
    return args

def pho_rc_func(name, *args, **kwargs):
    """Wrapper on CFUNCTYPE that sets the return type to c_int and errcheck
    attribute to pho_rc_check.

    name is the name of the function (for proper error reporting)
    *args are only the types of the argument of the function
    **kwargs are the kwargs accepted by CFUNCTYPE
    """
    func_type = CFUNCTYPE(c_int, *args, **kwargs)
    # Dynamically generate a subclass of func_type, as func_type must both
    # describe the type of the function and be callable
    class _PhoRcFunc(func_type):
        """Overrides __call__ to set pho_rc_check as an errcheck"""
        # Definition needed by func_type's metaclass
        _flags_ = func_type._flags_
        def __call__(self, *args, **kwargs):
            self.errcheck = pho_rc_check
            return super(_PhoRcFunc, self).__call__(*args, **kwargs)
        def __str__(self):
            return name
    return _PhoRcFunc
