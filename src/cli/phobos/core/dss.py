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
High level, object-oriented interface over DSS. This is the module to use
to interact with phobos DSS layer, as it provides a safe, expressive and
clean API to access it.
"""

import json
import logging
import os
import time
from ctypes import *
from socket import gethostname
from abc import ABCMeta, abstractmethod, abstractproperty

from phobos.core.const import str2fs_type
from phobos.core.const import PHO_ADDR_HASH1
from phobos.core.const import PHO_MDA_ADM_ST_LOCKED, PHO_MDA_ADM_ST_UNLOCKED
from phobos.core.const import PHO_DEV_ADM_ST_LOCKED, PHO_DEV_ADM_ST_UNLOCKED
from phobos.core.const import DSS_SET_INSERT, DSS_SET_UPDATE, DSS_SET_DELETE

from phobos.core.ffi import DevInfo, MediaInfo, MediaId, MediaStats
from phobos.core.ffi import LIBPHOBOS
from phobos.core.ldm import ldm_device_query


# Valid filter suffix and associated operators.
FILTER_OPERATORS = (
    ('__not', '$NOR'),
    ('__gt', '$GT'),
    ('__ge', '$GTE'),
    ('__lt', '$LT'),
    ('__le', '$LTE'),
    ('__like', '$LIKE'),
    ('__jcontain', '$INJSON'),
    ('__jexist', '$XJSON')
)

OBJECT_PREFIXES = {
    'device': 'DSS::DEV::',
    'layout': 'DSS::EXT::',
    'media':  'DSS::MDA::',
}

class JSONFilter(Structure):
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
    for key, val in kwargs.iteritems():
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

    rc = LIBPHOBOS.dss_filter_build(byref(filt), filt_str)
    if rc:
        raise EnvironmentError(rc, "Invalid filter criteria")

    return filt

class DSSResult(object):
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
        if (index >= self._n_elts or index < -self._n_elts):
            raise IndexError("Index must be between -%d and %d, got %r"
                             % (self._n_elts, self._n_elts - 1, index))
        if index < 0:
            index = self._n_elts + index
        item = self._native_res[index]
        # This is necessary so that self is always referenced when _native_res
        # items are referenced. Without this, self.__del__ could be called
        # although there exist references on _native_res items.
        item._dss_result_parent_link = self
        return item

class BaseObjectManager(object):
    """Proxy to manipulate (CRUD) objects in DSS."""
    __metaclass__ = ABCMeta

    def __init__(self, client, *args, **kwargs):
        """Initialize new instance."""
        super(BaseObjectManager, self).__init__(*args, **kwargs)
        self.logger = logging.getLogger(__name__)
        self.client = client

    @abstractmethod
    def _dss_get(self, hdl, qry_filter, res, res_cnt):
        """Return DSS specialized GET method for this type of object."""
        pass

    @abstractmethod
    def _dss_set(self, hdl, obj, obj_cnt, opcode):
        """Return DSS specialized SET method for this type of object."""
        pass

    @abstractproperty
    def wrapped_class(self):
        """Return the ctype class which this proxy wraps."""
        pass

    @abstractproperty
    def wrapped_ident(self):
        """Return the short name of the clas which this proxy wraps."""
        pass

    def get(self, **kwargs):
        """Retrieve objects from DSS."""
        res = POINTER(self.wrapped_class)()
        res_cnt = c_int()

        # rename the tags key to be correctly parsed by the dss filter
        if kwargs.get('tags', None):
            tags = kwargs.pop('tags')
            kwargs['tags__jexist'] = tags

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

    def _generic_set(self, objects, opcode):
        """Common operation to wrap dss_{device,media,...}_set()"""
        if not objects:
            return

        obj_cnt = len(objects)
        obj = (self.wrapped_class * obj_cnt)()
        for i, o in enumerate(objects):
            obj[i] = o

        rc = self._dss_set(byref(self.client.handle), obj, obj_cnt, opcode)
        if rc:
            raise EnvironmentError(rc, "Cannot issue set request")

    def insert(self, objects):
        """Insert objects into DSS"""
        rc = self._generic_set(objects, DSS_SET_INSERT)
        if rc:
            raise EnvironmentError(rc, "Cannot insert objects")

    def update(self, objects):
        """Update objects in DSS"""
        rc = self._generic_set(objects, DSS_SET_UPDATE)
        if rc:
            raise EnvironmentError(rc, "Cannot update objects")

    def delete(self, objects):
        """Delete objects from DSS"""
        rc = self._generic_set(objects, DSS_SET_DELETE)
        if rc:
            raise EnvironmentError(rc, "Cannot delete objects")

    def _generic_lock_unlock(self, objects, lock_c_func, err_message,
                             lock_owner):
        """Call a dss_<object>_{un,}lock c function on this list of objects.
        lock_c_func could for example be dss_media_lock.
        """
        obj_count = len(objects)
        obj_array = (self.wrapped_class * obj_count)(*objects)
        rc = lock_c_func(byref(self.client.handle), obj_array, obj_count,
                         lock_owner)
        if rc:
            raise EnvironmentError(rc, err_message)


class DeviceManager(BaseObjectManager):
    """Proxy to manipulate devices."""
    wrapped_class = DevInfo
    wrapped_ident = 'device'

    def add(self, device_type, device_path, locked=True):
        """Query device and insert information into DSS."""
        state = ldm_device_query(device_type, device_path)
        host = gethostname().split('.')[0]
        if locked:
            status = PHO_DEV_ADM_ST_LOCKED
        else:
            status = PHO_DEV_ADM_ST_UNLOCKED

        dev_info = DevInfo(state.lds_family, state.lds_model, device_path,
                           host, state.lds_serial, status)

        self.insert([dev_info])

        self.logger.info("Device '%s:%s' successfully added: " \
                         "model=%s serial=%s (%s)",
                         dev_info.host, device_path, dev_info.model,
                         dev_info.serial, locked and "locked" or "unlocked")

    def _dss_get(self, hdl, qry_filter, res, res_cnt):
        """Invoke device-specific DSS get method."""
        return LIBPHOBOS.dss_device_get(hdl, qry_filter, res, res_cnt)

    def _dss_set(self, hdl, obj, obj_cnt, opcode):
        """Invoke device-specific DSS set method."""
        return LIBPHOBOS.dss_device_set(hdl, obj, obj_cnt, opcode)

class MediaManager(BaseObjectManager):
    """Proxy to manipulate media."""
    wrapped_class = MediaInfo
    wrapped_ident = 'media'
    lock_owner_count = 0

    def __init__(self, *args, **kwargs):
        super(MediaManager, self).__init__(*args, **kwargs)
        self.lock_owner = "py-%.210s:%.8x:%.16x:%.16x" % (
            gethostname(), os.getpid(), int(time.time()), self.lock_owner_count
        )
        self.lock_owner_count += 1

    def add(self, mtype, fstype, model, label, tags=None, locked=False):
        """Insert media into DSS."""
        media = MediaInfo()
        media.id = MediaId(mtype, label)
        media.fs.type = str2fs_type(fstype)
        media.model = model
        media.addr_type = PHO_ADDR_HASH1
        media.tags = tags or []
        if locked:
            media.adm_status = PHO_MDA_ADM_ST_LOCKED
        else:
            media.adm_status = PHO_MDA_ADM_ST_UNLOCKED

        media.stats = MediaStats()

        self.insert([media])

        self.logger.debug("Media '%s' successfully added: "\
                          "model=%s fs=%s (%s)",
                          label, model, fstype,
                          locked and "locked" or "unlocked")

    def _dss_get(self, hdl, qry_filter, res, res_cnt):
        """Invoke media-specific DSS get method."""
        return LIBPHOBOS.dss_media_get(hdl, qry_filter, res, res_cnt)

    def _dss_set(self, hdl, obj, obj_cnt, opcode):
        """Invoke media-specific DSS set method."""
        return LIBPHOBOS.dss_media_set(hdl, obj, obj_cnt, opcode)

    def lock(self, objects):
        """Lock all the media associated with a given list of MediaInfo"""
        self._generic_lock_unlock(
            objects, LIBPHOBOS.dss_media_lock, "Media locking failed",
            self.lock_owner,
        )

    def unlock(self, objects, force=False):
        """Unlock all the media associated with a given list of MediaInfo.
        If `force` is True, unlock the objects even if they were not locked
        by this instance.
        """
        self._generic_lock_unlock(
            objects, LIBPHOBOS.dss_media_unlock, "Media unlocking failed",
            None if force else self.lock_owner,
        )

class DSSHandle(Structure):
    """
    Wrap connection to the backend. Absolutely opaque and propagated everywhere.
    """
    _fields_ = [
        ('dh_conn', c_void_p)
    ]

class Client(object):
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

    def connect(self, **kwargs):
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
