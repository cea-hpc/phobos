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
High level, object-oriented interface over DSS. This is the module to use
to interact with phobos DSS layer, as it provides a safe, expressive and
clean API to access it.
"""

import json
import logging
from ctypes import byref, c_int, c_void_p, POINTER, Structure
from abc import ABCMeta, abstractmethod, abstractproperty

from phobos.core.const import (DSS_SET_DELETE, DSS_SET_INSERT, DSS_SET_UPDATE, # pylint: disable=no-name-in-module
                               DSS_MEDIA, PHO_ADDR_HASH1, PHO_ADDR_PATH,
                               PHO_FS_RADOS, str2fs_type)
from phobos.core.ffi import (DevInfo, MediaInfo, MediaStats, LIBPHOBOS,
                             OperationFlags)

# Valid filter suffix and associated operators.
FILTER_OPERATORS = (
    ('__not', '$NOR'),
    ('__gt', '$GT'),
    ('__ge', '$GTE'),
    ('__lt', '$LT'),
    ('__le', '$LTE'),
    ('__like', '$LIKE'),
    ('__regexp', '$REGEXP'),
    ('__jcontain', '$INJSON'),
    ('__jkeyval', '$KVINJSON'),
    ('__jexist', '$XJSON'),
)

OBJECT_PREFIXES = {
    'device': 'DSS::DEV::',
    'layout': 'DSS::EXT::',
    'media':  'DSS::MDA::',
}

class JSONFilter(Structure): # pylint: disable=too-few-public-methods
    """JSON DSS filter"""
    _fields_ = [
        ('df_json', c_void_p)
    ]

def key_convert(obj_type, key):
    """Split key, return actual name and associated operator."""
    kname = key
    comp = None # default (equal)
    kname_prefx = OBJECT_PREFIXES[obj_type] # KeyError on unsupported obj_type
    for sufx, comp_enum in FILTER_OPERATORS:
        if key.endswith(sufx):
            kname, comp = key[:-len(sufx)], comp_enum
            break
    return "%s%s" % (kname_prefx, kname), comp

def dss_filter(obj_type, **kwargs):
    """Convert a k/v filter into a CDSS-compatible list of criteria."""
    if len(kwargs) == 0:
        return None
    filt = JSONFilter()
    criteria = []
    for key, val in kwargs.items():
        if val is None:
            continue
        key, comp = key_convert(obj_type, key)
        if not isinstance(val, list):
            val = [val]
        for v in val:
            if comp is None:
                # Implicit equal
                criteria.append({key: v})
            else:
                criteria.append({comp: {key: v}})

    assert len(criteria) > 0

    if len(criteria) == 1:
        filt_str = json.dumps(criteria[0])
    else:
        filt_str = json.dumps({'$AND': criteria})

    rc = LIBPHOBOS.dss_filter_build(byref(filt), filt_str.encode('utf-8'))
    if rc:
        raise EnvironmentError(rc, "Invalid filter criteria")

    return filt

class DSSResult: # pylint: disable=too-few-public-methods
    """Wrapper on the native struct dss_result"""

    def __init__(self, native_result, n_elts):
        """`native_result` must be a ctype pointer to an array of dss results
        (DevInfo, MediaInfo, etc.)
        """
        self._n_elts = n_elts
        self._native_res = native_result

    def __del__(self):
        LIBPHOBOS.dss_res_free(self._native_res, self._n_elts)

    def __len__(self):
        return self._n_elts

    def __getitem__(self, index):
        if index >= self._n_elts or index < -self._n_elts:
            raise IndexError("Index must be between -%d and %d, got %r"
                             % (self._n_elts, self._n_elts - 1, index))
        if index < 0:
            index = self._n_elts + index
        item = self._native_res[index]
        # This is necessary so that self is always referenced when _native_res
        # items are referenced. Without this, self.__del__ could be called
        # although there exist references on _native_res items.
        item._dss_result_parent_link = self # pylint: disable=protected-access
        return item

class BaseEntityManager:
    """Proxy to manipulate (CRUD) objects in DSS."""
    __metaclass__ = ABCMeta

    def __init__(self, client, *args, **kwargs):
        """Initialize new instance."""
        super(BaseEntityManager, self).__init__(*args, **kwargs)
        self.logger = logging.getLogger(__name__)
        self.client = client

    @abstractmethod
    def _dss_get(self, hdl, qry_filter, res, res_cnt):
        """Return DSS specialized GET method for this type of object."""

    @abstractproperty
    def wrapped_class(self):
        """Return the ctype class which this proxy wraps."""

    @abstractproperty
    def wrapped_ident(self):
        """Return the short name of the class which this proxy wraps."""

    @staticmethod
    def convert_kwargs(name, keyop, **kwargs):
        """Convert the given kwargs field using the new keyop."""
        if kwargs.get(name, None):
            arg = kwargs.pop(name)
            kwargs[keyop] = arg
        return kwargs

    def get(self, **kwargs):
        """Retrieve objects from DSS."""
        res = POINTER(self.wrapped_class)()
        res_cnt = c_int()

        # transform the family attribute into a string
        try:
            kwargs['family'] = str(kwargs['family'])
        except KeyError:
            pass

        # rename keys that need a specific operation to be correctly parsed
        # by the dss filter
        kwargs = self.convert_kwargs('tags', 'tags__jexist', **kwargs)
        kwargs = self.convert_kwargs('pattern', 'oid__regexp', **kwargs)
        kwargs = self.convert_kwargs('metadata', 'user_md__jkeyval', **kwargs)

        filt = dss_filter(self.wrapped_ident, **kwargs)
        if filt is not None:
            fref = byref(filt)
        else:
            fref = None

        try:
            rc = self._dss_get(byref(self.client.handle), fref, byref(res),
                               byref(res_cnt))
        finally:
            LIBPHOBOS.dss_filter_free(fref)

        if rc:
            raise EnvironmentError(rc, "Cannot issue get request")

        ret_items = DSSResult(res, res_cnt.value)

        return ret_items


class DeviceManager(BaseEntityManager):
    """Proxy to manipulate devices."""
    wrapped_class = DevInfo
    wrapped_ident = 'device'

    def __init__(self, client, *args, **kwargs):
        super(DeviceManager, self).__init__(client, *args, **kwargs)

    def _dss_get(self, hdl, qry_filter, res, res_cnt):
        """Invoke device-specific DSS get method."""
        return LIBPHOBOS.dss_device_get(hdl, qry_filter, res, res_cnt)

class MediaManager(BaseEntityManager):
    """Proxy to manipulate media."""
    wrapped_class = MediaInfo
    wrapped_ident = 'media'

    def __init__(self, client, *args, **kwargs):
        super(MediaManager, self).__init__(client, *args, **kwargs)

    def _dss_get(self, hdl, qry_filter, res, res_cnt):
        """Invoke media-specific DSS get method."""
        return LIBPHOBOS.dss_media_get(hdl, qry_filter, res, res_cnt)

    def _dss_set(self, hdl, media, media_cnt, opcode, fields):
        #pylint: disable=too-many-arguments,no-self-use
        """Invoke media-specific DSS set method."""
        return LIBPHOBOS.dss_media_set(hdl, media, media_cnt, opcode, fields)

    def _generic_set(self, media, opcode, fields=0):
        """Common operation to wrap dss_media_set()"""
        if not media:
            return

        med_cnt = len(media)
        med = (self.wrapped_class * med_cnt)()
        for i, elt in enumerate(media):
            med[i] = elt

        rc = self._dss_set(byref(self.client.handle), med, med_cnt, opcode,
                           fields)
        if rc:
            raise EnvironmentError(rc, "Cannot issue set request")

    def insert(self, media):
        """Insert media into DSS"""
        self._generic_set(media, DSS_SET_INSERT)

    def update(self, media, fields):
        """Update media in DSS"""
        self._generic_set(media, DSS_SET_UPDATE, fields)

    def delete(self, media):
        """Delete media from DSS"""
        self._generic_set(media, DSS_SET_DELETE)

    def _generic_lock_unlock(self, objects, lock_c_func, err_message,
                             lock_type, force=None):
        #pylint: disable=too-many-arguments
        """Call a dss_<object>_{un,}lock c function on this list of objects.
        lock_c_func could for example be dss_media_lock.
        """
        obj_count = len(objects)
        obj_array = (self.wrapped_class * obj_count)(*objects)
        if force is not None:
            rc = lock_c_func(byref(self.client.handle), lock_type,
                             obj_array, obj_count, force)
        else:
            rc = lock_c_func(byref(self.client.handle), lock_type,
                             obj_array, obj_count)
        if rc:
            raise EnvironmentError(rc, err_message)

    def add(self, media, fstype, tags=None):
        """Insert media into DSS."""
        media.fs.type = str2fs_type(fstype)
        media.addr_type = (PHO_ADDR_PATH if media.fs.type == PHO_FS_RADOS
                           else PHO_ADDR_HASH1)
        media.tags = tags or []

        media.stats = MediaStats()
        media.flags = OperationFlags()

        self.insert([media])

        self.logger.debug("Media '%s' successfully added: "\
                          "model=%s fs=%s (%s)",
                          media.name, media.model, fstype, media.adm_status)

    def remove(self, family, name):
        """Delete media from DSS."""
        media = MediaInfo(family=family, name=name)
        self.delete([media])
        self.logger.debug("Media '%s' successfully deleted: ", name)

    def lock(self, objects):
        """Lock all the media associated with a given list of MediaInfo"""
        self._generic_lock_unlock(
            objects, LIBPHOBOS.dss_lock, "Media locking failed", DSS_MEDIA,
        )

    def unlock(self, objects, force=False):
        """Unlock all the media associated with a given list of MediaInfo.
        If `force` is True, unlock the objects even if they were not locked
        by this instance.
        """
        self._generic_lock_unlock(
            objects, LIBPHOBOS.dss_unlock, "Media unlocking failed",
            DSS_MEDIA, force,
        )

class DSSHandle(Structure): # pylint: disable=too-few-public-methods
    """
    Wrap connection to the backend. Absolutely opaque and propagated everywhere.
    """
    _fields_ = [
        ('dh_conn', c_void_p)
    ]

class Client:
    """High-level, object-oriented, double-keyworded DSS wrappers for the CLI"""
    def __init__(self, *args, **kwargs):
        """Initialize a new DSS context."""
        super(Client, self).__init__(*args, **kwargs)
        self.handle = None
        self.media = MediaManager(self)
        self.devices = DeviceManager(self)

    def __enter__(self):
        """Enter a runtime context"""
        self.connect()
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        """Exit a runtime context."""
        self.disconnect()

    def __del__(self):
        """Force disconnection on garbage collection (can be handy to easen
        error handling).
        """
        self.disconnect()

    def connect(self):
        """ Establish a fresh connection or renew a stalled one if needed."""
        if self.handle is not None:
            self.disconnect()

        self.handle = DSSHandle()

        rc = LIBPHOBOS.dss_init(byref(self.handle))
        if rc:
            raise EnvironmentError(rc, 'DSS initialization failed')

    def disconnect(self):
        """Disconnect from DSS and reset handle."""
        if self.handle is not None:
            LIBPHOBOS.dss_fini(byref(self.handle))
            self.handle = None
