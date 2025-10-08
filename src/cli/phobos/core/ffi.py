#!/usr/bin/env python3
# pylint: disable=too-many-lines

#
#  All rights reserved (c) 2014-2025 CEA/DAM.
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

from ctypes import (byref, CDLL, CFUNCTYPE, c_bool, c_char, c_char_p,
                    c_int, c_long, c_longlong, c_size_t, c_ssize_t, c_ubyte,
                    c_uint64, c_void_p, POINTER, Structure)
from enum import IntEnum

from phobos.core.const import (PHO_LABEL_MAX_LEN, PHO_URI_MAX, # pylint: disable=no-name-in-module
                               PHO_RSC_ADM_ST_LOCKED, PHO_RSC_ADM_ST_UNLOCKED,
                               PHO_RSC_DIR, PHO_RSC_TAPE, PHO_RSC_RADOS_POOL,
                               PHO_FS_LTFS, PHO_FS_POSIX, PHO_FS_RADOS,
                               PHO_TIMEVAL_MAX_LEN, MD5_BYTE_LENGTH,
                               fs_type2str, fs_status2str, rsc_adm_status2str,
                               rsc_family2str, extent_state2str,
                               copy_status2str)

LIBPHOBOS_NAME = "libphobos_store.so"
LIBPHOBOS = CDLL(LIBPHOBOS_NAME)
# FIXME we should call phobos_fini(). But this can only be done once we don't
# need phobos anymore. Since phobos_fini() only frees memory currently, this is
# not an issue.
LIBPHOBOS.phobos_init()

LIBPHOBOS_ADMIN_NAME = "libphobos_admin.so"
LIBPHOBOS_ADMIN = CDLL(LIBPHOBOS_ADMIN_NAME)

class CLIManagedResourceMixin:
    """Interface for objects directly exposed/manipulated by the CLI."""
    def get_display_fields(self, max_width):
        """Return a dict of available fields and optional display formatters."""
        raise NotImplementedError("Abstract method subclasses must implement.")

    def get_display_dict(self, numeric=False, max_width=None, fmt=None):
        """
        Return a dict representing the structure as we want it to be displayed,
        i.e.: w/ only the desired fields and w/ conversion methods applied.
        """
        export = {}
        disp_fields = self.get_display_fields(max_width)
        for key in sorted(disp_fields.keys()):
            if not numeric and disp_fields[key]:
                conv = disp_fields.get(key, str)
            else:
                # Some formats (e.g. JSON) need a specific convertion function,
                # Converting to a string too early would result in invalid JSON.
                conv = fmt if fmt is not None else str
            export[key] = conv(getattr(self, key))
        return export

class Timeval(Structure): # pylint: disable=too-few-public-methods
    """standard struct timeval."""
    _fields_ = [
        ('tv_sec', c_long),
        ('tv_usec', c_long)
    ]

    def to_string(self):
        """Return a string displaying to values of a Timeval structure."""
        tv_str = (c_char * PHO_TIMEVAL_MAX_LEN)()
        LIBPHOBOS.timeval2str(byref(self), tv_str)
        return tv_str.value.decode('utf-8')

class DSSLock(Structure): # pylint: disable=too-few-public-methods
    """Resource lock as managed by DSS."""
    _fields_ = [
        ('_lock_hostname', c_char_p),
        ('lock_owner', c_int),
        ('lock_ts', Timeval),
        ('last_locate', Timeval),
        ('lock_is_early', c_bool),
    ]

    @property
    def lock_hostname(self):
        """Wrapper to get lock"""
        return (self._lock_hostname.decode('utf-8') if self._lock_hostname
                else None)

    @lock_hostname.setter
    def lock_hostname(self, val):
        """Wrapper to set lock"""
        # pylint: disable=attribute-defined-outside-init
        self._lock_hostname = val.encode('utf-8')


class CommInfo(Structure): # pylint: disable=too-few-public-methods
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
        # pylint: disable=attribute-defined-outside-init
        self._path = val.encode('utf-8')


class LRSSched(Structure): # pylint: disable=too-few-public-methods
    """Local Resource Scheduler sub data structure."""
    _fields_ = [
        ('dss', c_void_p),
        ('devices', c_void_p),
        ('dev_count', c_size_t),
        ('_lock_hostname', c_char_p),
        ('lock_owner', c_int),
        ('req_queue', c_void_p),
        ('release_queue', c_void_p),
    ]

    @property
    def lock_hostname(self):
        """Wrapper to get lock_hostname"""
        return (self._lock_hostname.decode('utf-8') if self._lock_hostname
                else None)

    @lock_hostname.setter
    def lock_hostname(self, val):
        """Wrapper to set lock_hostname"""
        # pylint: disable=attribute-defined-outside-init
        self._lock_hostname = val.encode('utf-8')

class LRS(Structure): # pylint: disable=too-few-public-methods
    """Local Resource Scheduler."""
    _fields_ = [
        ('sched', LRSSched),
        ('comm', CommInfo)
    ]

class Id(Structure): # pylint: disable=too-few-public-methods
    """Resource Identifier."""
    _fields_ = [
        ('family', c_int),
        ('_name', c_char * PHO_URI_MAX),
        ('_library', c_char * PHO_URI_MAX)
    ]

    @property
    def name(self):
        """Wrapper to get name"""
        return self._name.decode('utf-8')

    @name.setter
    def name(self, val):
        """Wrapper to set name"""
        # pylint: disable=attribute-defined-outside-init
        self._name = val.encode('utf-8')

    @property
    def library(self):
        """Wrapper to get library"""
        return self._library.decode('utf-8')

    @library.setter
    def library(self, val):
        """Wrapper to set library"""
        # pylint: disable=attribute-defined-outside-init
        self._library = val.encode('utf-8')

class Resource(Structure): # pylint: disable=too-few-public-methods
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
        # pylint: disable=attribute-defined-outside-init
        self._model = val.encode('utf-8') if val else None

class ResourceFamily(IntEnum):
    """Resource family enumeration."""
    RSC_TAPE = PHO_RSC_TAPE
    RSC_DIR = PHO_RSC_DIR
    RSC_RADOS_POOL = PHO_RSC_RADOS_POOL

    def __str__(self):
        return rsc_family2str(self.value)

class DevInfo(Structure, CLIManagedResourceMixin):
    """DSS device descriptor."""
    _fields_ = [
        ('rsc', Resource),
        ('_path', c_char_p),
        ('_host', c_char_p),
        ('lock', DSSLock),
        ('health', c_size_t),
    ]

    def get_display_fields(self, max_width=None):
        """Return a dict of available fields and optional display formatters."""
        return {
            'adm_status': rsc_adm_status2str,
            'family': rsc_family2str,
            'host': None,
            'model': None,
            'path': None,
            'name': None,
            'lock_hostname': None,
            'lock_owner': None,
            'lock_ts': Timeval.to_string,
            'library': None
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
    def library(self):
        """Wrapper to get library"""
        return self.rsc.id.library

    @library.setter
    def library(self, val):
        """Wrapper to set library"""
        self.rsc.id.library = val

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
    def lock_hostname(self):
        """Wrapper to get lock_hostname"""
        return self.lock.lock_hostname

    @property
    def lock_owner(self):
        """Wrapper to get lock_owner"""
        return self.lock.lock_owner

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
        # pylint: disable=attribute-defined-outside-init
        self._host = val.encode('utf-8')

    @property
    def path(self):
        """Wrapper to get path"""
        return self._path.decode('utf-8') if self._path else None

    @path.setter
    def path(self, val):
        """Wrapper to set path"""
        # pylint: disable=attribute-defined-outside-init
        self._path = val.encode('utf-8')


class StringArray(Structure): # pylint: disable=too-few-public-methods
    """List of strings"""
    _fields_ = [
        ('strings', POINTER(c_char_p)),
        ('count', c_size_t),
    ]

    def __init__(self, string_list):
        """Allocate C resources to create a native StringArray instance from a
        python list of strings.
        """
        super().__init__()
        if not string_list:
            self.strings = None
            self.count = 0
        else:
            enc_strings = list([s.encode('utf-8') if isinstance(s, str)
                                else s for _, s in enumerate(string_list)])
            strings = (c_char_p * len(enc_strings))(*enc_strings)
            LIBPHOBOS.string_array_init(byref(self), strings, len(enc_strings))

    def free(self):
        """Free all allocated resources. Only call this if tag values were
        dynamically allocated (and not borrowed from another structure).
        """
        LIBPHOBOS.string_array_free(byref(self))

class FSType(IntEnum):
    """File system type enumeration."""
    FS_POSIX = PHO_FS_POSIX
    FS_LTFS = PHO_FS_LTFS
    FS_RADOS = PHO_FS_RADOS

    def __str__(self):
        return fs_type2str(self.value)

class MediaFS(Structure): # pylint: disable=too-few-public-methods
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
        # pylint: disable=attribute-defined-outside-init
        self._label = val.encode('utf-8')

class MediaStats(Structure): # pylint: disable=too-few-public-methods
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

class OperationFlags(Structure): # pylint: disable=too-few-public-methods
    """Media operation flags."""
    _fields_ = [
        ('put', c_bool),
        ('get', c_bool),
        ('delete', c_bool)
    ]

    def __init__(self, *args, put=True, get=True, delete=True, **kwargs):
        super().__init__(*args, **kwargs)
        self.put = put
        self.get = get
        self.delete = delete

class MediaInfo(Structure, CLIManagedResourceMixin):
    """DSS media descriptor."""
    _fields_ = [
        ('rsc', Resource),
        ('addr_type', c_int),
        ('fs', MediaFS),
        ('stats', MediaStats),
        ('_tags', StringArray),
        ('lock', DSSLock),
        ('flags', OperationFlags),
        ('health', c_size_t),
        ('_groupings', StringArray),
    ]

    def get_display_fields(self, max_width=None):
        """Return a dict of available fields and optional display formatters."""
        return {
            'adm_status': rsc_adm_status2str,
            'family': rsc_family2str,
            'addr_type': None,
            'model': None,
            'name': None,
            'tags': None,
            'lock_hostname': None,
            'lock_owner': None,
            'lock_ts': Timeval.to_string,
            'put_access': None,
            'get_access': None,
            'delete_access': None,
            'library': None,
            'groupings': None,
        }

    def get_display_dict(self, numeric=False, max_width=None, fmt=None):
        """Update level0 representation with nested structures content."""
        export = super(MediaInfo, self).get_display_dict(numeric, max_width,
                                                         fmt)
        export.update(self.expanded_fs_info)
        export.update(self.expanded_stats)
        return export

    @property
    def lock_hostname(self):
        """Wrapper to get lock_hostname"""
        return self.lock.lock_hostname

    @property
    def lock_owner(self):
        """Wrapper to get lock owner"""
        return self.lock.lock_owner

    def is_locked(self):
        """True if this media is locked"""
        return self.lock_hostname is not None and self.lock_hostname != ""

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
    def library(self):
        """Wrapper to get library"""
        return self.rsc.id.library

    @library.setter
    def library(self, val):
        """Wrapper to set library"""
        self.rsc.id.library = val

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

    @adm_status.setter
    def is_adm_locked(self, val):
        """Wrapper to set adm_status using a boolean: True is locked"""
        self.rsc.adm_status = (PHO_RSC_ADM_ST_LOCKED if val
                               else PHO_RSC_ADM_ST_UNLOCKED)

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
        # pylint: disable=unsubscriptable-object
        return [t.decode('utf-8')
                for t in self._tags.strings[:self._tags.count]]

    @tags.setter
    def tags(self, tags):
        """Wrapper to set tags"""
        # pylint: disable=access-member-before-definition
        # pylint: disable=attribute-defined-outside-init
        self._tags.free()
        self._tags = StringArray(tags)
        # Manually creating and assigning tags means that we will have to free
        # them when MediaInfo is garbage collected
        self._free_tags = True

    @property
    def put_access(self):
        """Wrapper to get put operation flag"""
        return self.flags.put

    @put_access.setter
    def put_access(self, put_access):
        """Wrapper to set put operation flag"""
        self.flags.put = put_access

    @property
    def get_access(self):
        """Wrapper to get get operation flag"""
        return self.flags.get

    @get_access.setter
    def get_access(self, get_access):
        """Wrapper to set get operation flag"""
        self.flags.get = get_access

    @property
    def delete_access(self):
        """Wrapper to get delete operation flag"""
        return self.flags.delete

    @delete_access.setter
    def delete_access(self, delete_access):
        """Wrapper to set delete operation flag"""
        self.flags.delete = delete_access

    @property
    def groupings(self):
        """Wrapper to get groupings"""
        # pylint: disable=unsubscriptable-object
        return [t.decode('utf-8')
                for t in self._groupings.strings[:self._groupings.count]]

    @groupings.setter
    def groupings(self, groupings):
        """Wrapper to set groupings"""
        # pylint: disable=access-member-before-definition
        # pylint: disable=attribute-defined-outside-init
        self._groupings.free()
        self._groupings = StringArray(groupings)
        # Manually creating and assigning groupings means that we will have to
        # free them when MediaInfo is garbage collected
        self._free_groupings = True

    def __del__(self):
        if hasattr(self, "_free_tags") and self._free_tags:
            self._tags.free()

        if hasattr(self, "_free_groupings") and self._free_groupings:
            self._groupings.free()

def truncate_user_md(user_md_str, max_width):
    """Truncate user_md."""
    # we check that 'obj' (here, the user_md) is not None because if it
    # is, that means the object has no user_md, and there is no need to
    # truncate or print anything
    if user_md_str is None:
        return "{}"

    if max_width is None:
        return user_md_str

    return (user_md_str[:(max_width - 4)] + "...}"
            if len(user_md_str) > max_width - 4 else user_md_str)

class ObjectInfo(Structure, CLIManagedResourceMixin):
    """Object descriptor."""
    _fields_ = [
        ('_oid', c_char_p),
        ('_uuid', c_char_p),
        ('version', c_int),
        ('_user_md', c_char_p),
        ('creation_time', Timeval),
        ('deprec_time', Timeval),
        ('_grouping', c_char_p),
        ('size', c_ssize_t),
    ]

    def get_display_fields(self, max_width):
        """Return a dict of available fields and optional display formatters."""
        return {
            'oid': None,
            'uuid': None,
            'version': None,
            'creation_time': Timeval.to_string,
            'user_md': (lambda obj, width=max_width: truncate_user_md(obj,
                                                                      width)),
            'grouping': None,
            'size': None,
        }

    @property
    def oid(self):
        """Wrapper to get oid"""
        return self._oid.decode('utf-8') if self._oid else None

    @oid.setter
    def oid(self, val):
        """Wrapper to set oid"""
        # pylint: disable=attribute-defined-outside-init
        self._oid = val.encode('utf-8')

    @property
    def uuid(self):
        """Wrapper to get uuid"""
        return self._uuid.decode('utf-8') if self._uuid else None

    @uuid.setter
    def uuid(self, val):
        """Wrapper to set uuid"""
        # pylint: disable=attribute-defined-outside-init
        self._uuid = val.encode('utf-8') if val else None

    @property
    def user_md(self):
        """Wrapper to get user_md"""
        return self._user_md.decode('utf-8') if self._user_md else None

    @user_md.setter
    def user_md(self, val):
        """Wrapper to set user_md"""
        # pylint: disable=attribute-defined-outside-init
        self._user_md = val.encode('utf-8')

    @property
    def grouping(self):
        """Wrapper to get grouping"""
        return self._grouping.decode('utf-8') if self._grouping else None

    @grouping.setter
    def grouping(self, val):
        """Wrapper to set grouping"""
        # pylint: disable=attribute-defined-outside-init
        self._grouping = val.encode('utf-8')

class DeprecatedObjectInfo(ObjectInfo):
    """Deprecated object wrapper to get the correct display fields"""
    def get_display_fields(self, max_width=None):
        """Return a dict of available fields and optional display formatters."""
        return {
            'oid': None,
            'uuid': None,
            'version': None,
            'user_md': None,
            'creation_time': Timeval.to_string,
            'deprec_time': Timeval.to_string,
            'grouping': None,
            'size': None,
        }

class CopyInfo(Structure, CLIManagedResourceMixin):
    """Copy descriptor."""
    _fields_ = [
        ('_uuid', c_char_p),
        ('version', c_int),
        ('_copy_name', c_char_p),
        ('status', c_int),
        ('creation_time', Timeval),
        ('access_time', Timeval),
    ]

    def get_display_fields(self, max_width):
        """Return a dict of available fields and optional display formatters."""
        return {
            'uuid': None,
            'version': None,
            'copy_name': None,
            'status': copy_status2str,
            'creation_time': Timeval.to_string,
            'access_time': Timeval.to_string,
        }

    @property
    def uuid(self):
        """Wrapper to get uuid"""
        return self._uuid.decode('utf-8') if self._uuid else None

    @uuid.setter
    def uuid(self, val):
        """Wrapper to set uuid"""
        # pylint: disable=attribute-defined-outside-init
        self._uuid = val.encode('utf-8') if val else None

    @property
    def copy_name(self):
        """Wrapper to get copy_name"""
        return self._copy_name.decode('utf-8') if self._copy_name else None

    @copy_name.setter
    def copy_name(self, val):
        """Wrapper to set copy_name"""
        # pylint: disable=attribute-defined-outside-init
        self.copy_name = val.encode('utf-8') if val else None

class Buffer(Structure): # pylint: disable=too-few-public-methods
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
        # pylint: disable=attribute-defined-outside-init
        self._buff = val.encode('utf-8')

class PhoAttrs(Structure): # pylint: disable=too-few-public-methods
    """Attribute set."""
    _fields_ = [
        ('attr_set', c_void_p)
    ]

class ExtentInfo(Structure): # pylint: disable=too-few-public-methods
    """DSS extent descriptor."""
    _fields_ = [
        ('extent_uuid', c_char_p),
        ('layout_idx', c_int),
        ('state', c_int),
        ('size', c_ssize_t),
        ('media', Id),
        ('address', Buffer),
        ('offset', c_ssize_t),
        ('with_xxh128', c_bool),
        ('xxh128', c_ubyte * 16),
        ('with_md5', c_bool),
        ('md5', c_ubyte * MD5_BYTE_LENGTH),
        ('info', PhoAttrs),
        ('creation_time', Timeval),
    ]

class ModuleDesc(Structure): # pylint: disable=too-few-public-methods
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
        ('_uuid', c_char_p),
        ('version', c_int),
        ('layout_desc', ModuleDesc),
        ('wr_size', c_size_t),
        ('extents', POINTER(ExtentInfo)),
        ('ext_count', c_int),
        ('_copy_name', c_char_p),
    ]

    def get_display_fields(self, max_width=None):
        """Return a dict of available fields and optional display formatters."""
        return {
            'oid': None,
            'object_uuid': None,
            'version': None,
            'state': None,
            'ext_count': None,
            'ext_uuid': None,
            'media_name': None,
            'media_library': None,
            'family': None,
            'address': None,
            'size': None,
            'offset': None,
            'layout': None,
            'xxh128': None,
            'md5': None,
            'library': None,
            'copy_name': None,
            'creation_time':
                (lambda seq_t: str([Timeval.to_string(t) for t in seq_t])),
        }

    def get_sort_fields(self): # pylint: disable=no-self-use
        """Return a dict of available fields"""
        return {
            'ext_count': None,
            'layout': None,
            'object_uuid': None,
            'oid': None,
            'version': None,
            'size': None,
        }

    @property
    def copy_name(self):
        """Wrapper to get copy_name"""
        return self._copy_name.decode('utf-8') if self._copy_name else None

    @property
    def oid(self):
        """Wrapper to get oid"""
        return self._oid.decode('utf-8') if self._oid else None

    @property
    def object_uuid(self):
        """Wrapper to get uuid"""
        return self._uuid.decode('utf-8') if self._uuid else None

    @property
    def library(self):
        """Wrapper to get library"""
        return [self.extents[i].media.library for i in range(self.ext_count)]

    @property
    def state(self):
        """Wrapper to get state"""
        return [extent_state2str(self.extents[i].state)
                for i in range(self.ext_count)]

    @property
    def ext_uuid(self):
        """Wrapper to get extent UUIDs"""
        return [self.extents[i].extent_uuid.decode('utf-8')
                for i in range(self.ext_count)]

    @property
    def media_name(self):
        """Wrapper to get medium name."""
        return [self.extents[i].media.name for i in range(self.ext_count)]

    @property
    def media_library(self):
        """Wrapper to get medium library."""
        return [self.extents[i].media.library for i in range(self.ext_count)]

    @property
    def family(self):
        """Wrapper to get medium family."""
        return [rsc_family2str(self.extents[i].media.family)
                for i in range(self.ext_count)]

    @property
    def size(self):
        """Wrapper to get extent size."""
        return [self.extents[i].size for i in range(self.ext_count)]

    @property
    def offset(self):
        """Wrapper to get extent offset."""
        return [self.extents[i].offset for i in range(self.ext_count)]

    @property
    def address(self):
        """Wrapper to get extent address."""
        return [self.extents[i].address.buff for i in range(self.ext_count)]

    @property
    def xxh128(self):
        """Wrapper to get extent xxh128."""
        return [''.join('%02x' % one_byte
                        for one_byte in self.extents[i].xxh128)
                if self.extents[i].with_xxh128
                else None
                for i in range(self.ext_count)]

    @property
    def md5(self):
        """Wrapper to get extent md5."""
        return [''.join('%02x' % one_byte
                        for one_byte in self.extents[i].md5)
                if self.extents[i].with_md5
                else None
                for i in range(self.ext_count)]

    @property
    def layout(self):
        """Wrapper to get object layout."""
        return self.layout_desc.mod_name

    @property
    def creation_time(self):
        """Wrapper to get extent creation time"""
        return [self.extents[i].creation_time for i in range(self.ext_count)]

class PhoLogRec(Structure):
    """Single log record."""
    _fields_ = [
        ('plr_level', c_int),
        ('plr_tid', c_int),
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
        # pylint: disable=attribute-defined-outside-init
        self._plr_file = val.encode('utf-8')

    @property
    def plr_func(self):
        """Wrapper to get plr_func"""
        return self._plr_func.decode('utf-8')

    @plr_func.setter
    def plr_func(self, val):
        """Wrapper to set plr_func"""
        # pylint: disable=attribute-defined-outside-init
        self._plr_func = val.encode('utf-8')

    @property
    def plr_msg(self):
        """Wrapper to get plr_msg"""
        return self._plr_msg.decode('utf-8')

    @plr_msg.setter
    def plr_msg(self, val):
        """Wrapper to set plr_msg"""
        # pylint: disable=attribute-defined-outside-init
        self._plr_msg = val.encode('utf-8')


class LogFilter(Structure):
    """Logs filter structure description."""
    _fields_ = [
        ('device', Id),
        ('medium', Id),
        ('errno', POINTER(c_int)),
        ('cause', c_int),
        ('start', Timeval),
        ('end', Timeval),
        ('errors', c_bool),
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
    class _PhoRcFunc(func_type): # pylint: disable=too-few-public-methods
        """Overrides __call__ to set pho_rc_check as an errcheck"""
        # Definition needed by func_type's metaclass
        _flags_ = func_type._flags_
        def __call__(self, *args, **kwargs):
            # pylint: disable=attribute-defined-outside-init
            self.errcheck = pho_rc_check
            return super(_PhoRcFunc, self).__call__(*args, **kwargs)
        def __str__(self):
            return name
    return _PhoRcFunc

class LibItemAddr(Structure): # pylint: disable=too-few-public-methods
    """Location descriptor in a library"""
    _fields_ = [
        ('lia_type', c_int),
        ('lia_addr', c_uint64)
    ]

class LibDrvInfo(Structure): # pylint: disable=too-few-public-methods
    """Device information in a library."""
    _fields_ = [
        ('ldi_addr', LibItemAddr),
        ('ldi_first_addr', c_uint64),
        ('ldi_full', c_bool),
        ('ldi_medium_id', Id)
    ]

class DriveStatus(CLIManagedResourceMixin): #pylint: disable=too-many-instance-attributes
    """Wrapper class to use dump_object_list"""

    def __init__(self, values=None):
        if not values:
            return

        self.name = values.get("name", "")
        self.library = values.get("library", "")
        self.device = values.get("device", "")
        self.serial = values.get("serial", "")
        self.address = values.get("address", "")
        self.mount_path = values.get("mount_path", "")
        self.media = values.get("media", "")
        self.ongoing_io = values.get("ongoing_io", "")
        self.currently_dedicated_to = values.get("currently_dedicated_to", "")
        self.adm_status = values.get("adm_status")

    def get_display_fields(self, max_width=None):
        """Return a dict of available fields and optional display formatters."""
        return {
            'name': None,
            'library': None,
            'device': None,
            'address': None,
            'serial': None,
            'mount_path': None,
            'media': None,
            'ongoing_io': None,
            'currently_dedicated_to': None,
            'adm_status': None,
        }
