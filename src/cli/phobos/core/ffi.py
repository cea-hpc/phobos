#!/usr/bin/env python3

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
from enum import IntEnum

from phobos.core.const import (PHO_LABEL_MAX_LEN, PHO_URI_MAX,
                               PHO_RSC_DIR, PHO_RSC_DISK, PHO_RSC_TAPE,
                               fs_type2str, fs_status2str,
                               rsc_adm_status2str, rsc_family2str)

LIBPHOBOS_NAME = "libphobos_store.so"
LIBPHOBOS = CDLL(LIBPHOBOS_NAME)

LIBPHOBOS_ADMIN_NAME = "libphobos_admin.so"
LIBPHOBOS_ADMIN = CDLL(LIBPHOBOS_ADMIN_NAME)

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

class DSSLock(Structure):
    """Resource lock as managed by DSS."""
    _fields_ = [
        ('lock_ts', c_longlong),
        ('_lock', c_char_p)
    ]

    @property
    def lock(self):
        """Wrapper to get lock"""
        return self._lock.decode('utf-8') if self._lock else None

    @lock.setter
    def lock(self, val):
        """Wrapper to set lock"""
        self._lock = val.encode('utf-8')


class CommInfo(Structure):
    """Communication information."""
    _fields_ = [
        ('is_server', c_int),
        ('_path', c_char_p),
        ('socket_fd', c_int),
        ('epoll_fd', c_int),
        ('ev_tab', c_void_p),
    ]

    @property
    def path(self):
        """Wrapper to get path"""
        return self._path.decode('utf-8') if self._path else None

    @path.setter
    def path(self, val):
        """Wrapper to set path"""
        self._path = val.encode('utf-8')


class LRSSched(Structure):
    """Local Resource Scheduler sub data structure."""
    _fields_ = [
        ('dss', c_void_p),
        ('devices', c_void_p),
        ('dev_count', c_size_t),
        ('_lock_owner', c_char_p),
        ('req_queue', c_void_p),
        ('release_queue', c_void_p),
    ]

    @property
    def lock_owner(self):
        """Wrapper to get lock_owner"""
        return self._lock_owner.decode('utf-8') if self._lock_owner else None

    @lock_owner.setter
    def lock_owner(self, val):
        """Wrapper to set lock_owner"""
        self._lock_owner = val.encode('utf-8')

class LRS(Structure):
    """Local Resource Scheduler."""
    _fields_ = [
        ('sched', LRSSched),
        ('comm', CommInfo)
    ]

class Id(Structure):
    """Resource Identifier."""
    _fields_ = [
        ('family', c_int),
        ('_name', c_char * PHO_URI_MAX)
    ]

    @property
    def name(self):
        """Wrapper to get name"""
        return self._name.decode('utf-8')

    @name.setter
    def name(self, val):
        """Wrapper to set name"""
        self._name = val.encode('utf-8')

class Resource(Structure):
    """Resource."""
    _fields_ = [
        ('id', Id),
        ('_model', c_char_p),
        ('adm_status', c_int),
    ]

    @property
    def model(self):
        """Wrapper to get model"""
        return self._model.decode('utf-8') if self._model else None

    @model.setter
    def model(self, val):
        """Wrapper to set model"""
        self._model = val.encode('utf-8') if val else None

class ResourceFamily(IntEnum):
    """Resource family enumeration."""
    RSC_DISK = PHO_RSC_DISK
    RSC_TAPE = PHO_RSC_TAPE
    RSC_DIR  = PHO_RSC_DIR

    def __str__(self):
        return rsc_family2str(self.value)

class DevInfo(Structure, CLIManagedResourceMixin):
    """DSS device descriptor."""
    _fields_ = [
        ('rsc', Resource),
        ('_path', c_char_p),
        ('_host', c_char_p),
        ('lock', DSSLock)
    ]

    def get_display_fields(self):
        """Return a dict of available fields and optional display formatters."""
        return {
            'adm_status': rsc_adm_status2str,
            'family': rsc_family2str,
            'host': None,
            'model': None,
            'path': None,
            'name': None,
            'lock_status': None,
            'lock_ts': None
        }

    @property
    def name(self):
        """Wrapper to get name"""
        return self.rsc.id.name

    @name.setter
    def name(self, val):
        """Wrapper to set name"""
        self.rsc.id.name = val

    @property
    def family(self):
        """Wrapper to get family"""
        return self.rsc.id.family

    @property
    def model(self):
        """Wrapper to get model"""
        return self.rsc.model

    @model.setter
    def model(self, val):
        """Wrapper to set model"""
        self.rsc.model = val

    @property
    def adm_status(self):
        """Wrapper to get adm_status"""
        return self.rsc.adm_status

    @property
    def lock_status(self):
        """Wrapper to get lock_status"""
        return self.lock.lock

    @lock_status.setter
    def lock_status(self, val):
        """Wrapper to set lock_status"""
        self.lock.lock = val

    @property
    def lock_ts(self):
        """Wrapper to get lock timestamp"""
        return self.lock.lock_ts

    @property
    def host(self):
        """Wrapper to get host"""
        return self._host.decode('utf-8') if self._host else None

    @host.setter
    def host(self, val):
        """Wrapper to set host"""
        self._host = val.encode('utf-8')

    @property
    def path(self):
        """Wrapper to get path"""
        return self._path.decode('utf-8') if self._path else None

    @path.setter
    def path(self, val):
        """Wrapper to set path"""
        self._path = val.encode('utf-8')


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
            for i in range(len(tag_list)):
                tag_list[i] = tag_list[i].encode('utf-8') if isinstance(tag_list[i], str) else tag_list[i]
            tags = (c_char_p * len(tag_list))(*tag_list)
            LIBPHOBOS.tags_init(byref(self), tags, len(tag_list))

    def free(self):
        """Free all allocated resources. Only call this if tag values were
        dynamically allocated (and not borrowed from another structure).
        """
        LIBPHOBOS.tags_free(byref(self))

class MediaFS(Structure):
    """Media filesystem descriptor."""
    _fields_ = [
        ('type', c_int),
        ('status', c_int),
        ('_label', c_char * (PHO_LABEL_MAX_LEN + 1))
    ]

    @property
    def label(self):
        """Wrapper to get label"""
        return self._label.decode('utf-8') if self._label else None

    @label.setter
    def label(self, val):
        """Wrapper to set label"""
        self._label = val.encode('utf-8')

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
        ('rsc', Resource),
        ('addr_type', c_int),
        ('fs', MediaFS),
        ('stats', MediaStats),
        ('_tags', Tags),
        ('lock', DSSLock)
    ]

    def get_display_fields(self):
        """Return a dict of available fields and optional display formatters."""
        return {
            'adm_status': rsc_adm_status2str,
            'family': rsc_family2str,
            'addr_type': None,
            'model': None,
            'name': None,
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

    @property
    def lock_status(self):
        """Wrapper to get lock status"""
        return self.lock.lock

    @lock_status.setter
    def lock_status(self, val):
        """Wrapper to set lock status"""
        self.lock.lock = val

    def is_locked(self):
        """True if this media is locked"""
        return self.lock_status and self.lock_status != ""

    @property
    def lock_ts(self):
        """Wrapper to get lock timestamp"""
        return self.lock.lock_ts

    @property
    def family(self):
        """Wrapper to get family"""
        return self.rsc.id.family

    @family.setter
    def family(self, val):
        """Wrapper to set family"""
        self.rsc.id.family = val

    @property
    def name(self):
        """Wrapper to get medium name"""
        return self.rsc.id.name

    @name.setter
    def name(self, val):
        """Wrapper to set medium name"""
        self.rsc.id.name = val

    @property
    def model(self):
        """Wrapper to get medium model"""
        return self.rsc.model

    @model.setter
    def model(self, val):
        """Wrapper to get medium model"""
        self.rsc.model = val

    @property
    def adm_status(self):
        """Wrapper to get adm_status"""
        return self.rsc.adm_status

    @adm_status.setter
    def adm_status(self, val):
        """Wrapper to set adm_status"""
        self.rsc.adm_status = val

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
        """Wrapper to get tags"""
        return [t.decode('utf-8') for t in self._tags.tags[:self._tags.n_tags]]

    @tags.setter
    def tags(self, tags):
        """Wrapper to set tags"""
        self._tags.free()
        self._tags = Tags(tags)
        # Manually creating and assigning tags means that we will have to free
        # them when MediaInfo is garbage collected
        self._free_tags = True

    def __del__(self):
        if hasattr(self, "_free_tags") and self._free_tags:
            self._tags.free()

class ObjectInfo(Structure, CLIManagedResourceMixin):
    """Object descriptor."""
    _fields_ = [
        ('_oid', c_char_p),
        ('_user_md', c_char_p)
    ]

    def get_display_fields(self):
        """Return a dict of available fields and optional display formatters."""
        return {
            'oid': None,
            'user_md': None,
        }

    @property
    def oid(self):
        """Wrapper to get oid"""
        return self._oid.decode('utf-8') if self._oid else None

    @oid.setter
    def oid(self, val):
        """Wrapper to set oid"""
        self._oid = val.encode('utf-8')

    @property
    def user_md(self):
        """Wrapper to get user_md"""
        return self._user_md.decode('utf-8') if self._user_md else None

    @user_md.setter
    def user_md(self, val):
        """Wrapper to set user_md"""
        self._user_md = val.encode('utf-8')

class Buffer(Structure):
    """String buffer."""
    _fields_ = [
        ('size', c_size_t),
        ('_buff', c_char_p)
    ]

    @property
    def buff(self):
        """Wrapper to get buff"""
        return self._buff.decode('utf-8')

    @buff.setter
    def buff(self, val):
        """Wrapper to set buff"""
        self._buff = val.encode('utf-8')

class ExtentInfo(Structure):
    """DSS extent descriptor."""
    _fields_ = [
        ('layout_idx', c_int),
        ('size', c_ssize_t),
        ('media', Id),
        ('address', Buffer),
        ('addr_type', c_int)
    ]

class PhoAttrs(Structure):
    """Attribute set."""
    _fields_ = [
        ('attr_set', c_void_p)
    ]

class ModuleDesc(Structure):
    """Module description."""
    _fields_ = [
        ('_mod_name', c_char_p),
        ('mod_major', c_int),
        ('mod_minor', c_int),
        ('mod_attrs', PhoAttrs)
    ]

    @property
    def mod_name(self):
        """Wrapper to get mod_name"""
        return self._mod_name.decode('utf-8') if self._mod_name else None

class LayoutInfo(Structure, CLIManagedResourceMixin):
    """Object layout and extents description."""
    _fields_ = [
        ('_oid', c_char_p),
        ('state', c_int),
        ('layout_desc', ModuleDesc),
        ('wr_size', c_size_t),
        ('extents', c_void_p),
        ('ext_count', c_int)
    ]

    def get_display_fields(self):
        """Return a dict of available fields and optional display formatters."""
        return {
            'oid': None,
            'ext_count': None,
            'media_name': None,
            'family': None,
            'address': None,
            'size': None,
            'layout': None,
        }

    @property
    def oid(self):
        """Wrapper to get oid"""
        return self._oid.decode('utf-8') if self._oid else None

    @property
    def media_name(self):
        """Wrapper to get medium name."""
        return [cast(self.extents, POINTER(ExtentInfo))[i].media.name
                    for i in range(self.ext_count)]

    @property
    def family(self):
        """Wrapper to get medium family."""
        return [rsc_family2str(cast(self.extents,
                               POINTER(ExtentInfo))[i].media.family)
                    for i in range(self.ext_count)]

    @property
    def size(self):
        """Wrapper to get extent size."""
        return [cast(self.extents, POINTER(ExtentInfo))[i].size
                    for i in range(self.ext_count)]

    @property
    def address(self):
        """Wrapper to get extent address."""
        return [cast(self.extents, POINTER(ExtentInfo))[i].address.buff
                    for i in range(self.ext_count)]

    @property
    def layout(self):
        """Wrapper to get object layout."""
        return self.layout_desc.mod_name

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
        ('_plr_file', c_char_p),
        ('_plr_func', c_char_p),
        ('plr_line', c_int),
        ('plr_err', c_int),
        ('plr_time', Timeval),
        ('_plr_msg', c_char_p)
    ]

    @property
    def plr_file(self):
        """Wrapper to get plr_file"""
        return self._plr_file.decode('utf-8')

    @plr_file.setter
    def plr_file(self, val):
        """Wrapper to set plr_file"""
        self._plr_file = val.encode('utf-8')

    @property
    def plr_func(self):
        """Wrapper to get plr_func"""
        return self._plr_func.decode('utf-8')

    @plr_func.setter
    def plr_func(self, val):
        """Wrapper to set plr_func"""
        self._plr_func = val.encode('utf-8')

    @property
    def plr_msg(self):
        """Wrapper to get plr_msg"""
        return self._plr_msg.decode('utf-8')

    @plr_msg.setter
    def plr_msg(self, val):
        """Wrapper to set plr_msg"""
        self._plr_msg = val.encode('utf-8')

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
