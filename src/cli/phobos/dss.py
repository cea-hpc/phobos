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
import os.path
from ctypes import *
from socket import gethostname
from abc import ABCMeta, abstractmethod, abstractproperty

from phobos.capi.const import str2fs_type
from phobos.capi.const import PHO_ADDR_HASH1
from phobos.capi.const import PHO_MDA_ADM_ST_LOCKED, PHO_MDA_ADM_ST_UNLOCKED
from phobos.capi.const import PHO_DEV_ADM_ST_LOCKED, PHO_DEV_ADM_ST_UNLOCKED
from phobos.capi.const import DSS_SET_INSERT, DSS_SET_UPDATE, DSS_SET_DELETE

from phobos.types import DevInfo, MediaInfo, MediaId, MediaStats, UnionId

from phobos.ffi import LibPhobos, GenericError
from phobos.ldm import LDM

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
    'extent': 'DSS::EXT::',
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
            kname, comp = key[:len(sufx):], comp_enum
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
        if comp is None:
            # Implicit equal
            criteria.append({key: val})
        else:
            criteria.append({comp: {key: val}})

    assert len(criteria) > 0

    if len(criteria) == 1:
        filt_str = json.dumps(criteria[0])
    else:
        filt_str = json.dumps({'$AND': criteria})

    dl = LibPhobos()
    rc = dl.libphobos.dss_filter_build(byref(filt), filt_str)
    if rc:
        raise GenericError("Invalid filter criteria")

    return filt

class BaseObjectManager(LibPhobos):
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

        filt = dss_filter(self.wrapped_ident, **kwargs)
        if filt is not None:
            fref = byref(filt)
        else:
            fref = None

        rc = self._dss_get(byref(self.client.handle), fref, byref(res),
                           byref(res_cnt))
        if rc:
            self.logger.error("Cannot issue get request")
            return None

        ret_items = []
        for i in range(res_cnt.value):
            ret_items.append(res[i])

        return ret_items

    def _generic_set(self, objects, opcode):
        obj_cnt = len(objects)
        obj = (self.wrapped_class * obj_cnt)()
        for i, o in enumerate(objects):
            obj[i] = o

        rc = self._dss_set(byref(self.client.handle), obj, obj_cnt, opcode)
        if rc:
            self.logger.error("Cannot issue set request")
            return rc

        return 0

    def insert(self, objects):
        """Insert objects into DSS"""
        return self._generic_set(objects, DSS_SET_INSERT)

    def update(self, objects):
        """Update objects in DSS"""
        return self._generic_set(objects, DSS_SET_UPDATE)

    def delete(self, objects):
        """Delete objects from DSS"""
        return self._generic_set(objects, DSS_SET_DELETE)


class DeviceManager(BaseObjectManager):
    """Proxy to manipulate devices."""
    wrapped_class = DevInfo
    wrapped_ident = 'device'

    def add(self, device_type, device_path, locked=True):
        """Query device and insert information into DSS."""
        rc, state = LDM().device_query(device_type, device_path)
        if rc:
            return rc

        host = gethostname().split('.')[0]
        if locked:
            status = PHO_DEV_ADM_ST_LOCKED
        else:
            status = PHO_DEV_ADM_ST_UNLOCKED

        dev_info = DevInfo(state.lds_family, state.lds_model, device_path,
                           host, state.lds_serial, status)

        rc = self.insert([dev_info])
        if rc != 0:
            self.logger.error("Cannot insert dev info for '%s'" % device_path)
            return rc

        self.logger.info("Device '%s:%s' successfully added: " \
                         "model=%s serial=%s (%s)",
                         dev_info.host, device_path, dev_info.model,
                         dev_info.serial, locked and "locked" or "unlocked")
        return 0

    def _dss_get(self, hdl, qry_filter, res, res_cnt):
        """Invoke device-specific DSS get method."""
        return self.libphobos.dss_device_get(hdl, qry_filter, res, res_cnt)

    def _dss_set(self, hdl, obj, obj_cnt, opcode):
        """Invoke device-specific DSS set method."""
        return self.libphobos.dss_device_set(hdl, obj, obj_cnt, opcode)

class MediaManager(BaseObjectManager):
    """Proxy to manipulate media."""
    wrapped_class = MediaInfo
    wrapped_ident = 'media'

    def add(self, mtype, fstype, model, label, locked=False):
        """Insert media into DSS."""
        media = MediaInfo()
        media.id = MediaId(mtype, UnionId(label))
        media.fs.type = str2fs_type(fstype)
        media.model = model
        media.addr_type = PHO_ADDR_HASH1
        if locked:
            media.adm_status = PHO_MDA_ADM_ST_LOCKED
        else:
            media.adm_status = PHO_MDA_ADM_ST_UNLOCKED

        media.stats = MediaStats()

        rc = self.insert([media])
        if rc != 0:
            self.logger.error("Cannot insert media info for '%s'" % label)
            return rc

        self.logger.debug("Media '%s' successfully added: "\
                          "model=%s fs=%s (%s)",
                          label, model, fstype,
                          locked and "locked" or "unlocked")
        return 0

    def _dss_get(self, hdl, qry_filter, res, res_cnt):
        """Invoke media-specific DSS get method."""
        return self.libphobos.dss_media_get(hdl, qry_filter, res, res_cnt)

    def _dss_set(self, hdl, obj, obj_cnt, opcode):
        """Invoke media-specific DSS set method."""
        return self.libphobos.dss_media_set(hdl, obj, obj_cnt, opcode)

class DSSHandle(Structure):
    """
    Wrap connection to the backend. Absolutely opaque and propagated everywhere.
    """
    _fields_ = [
        ('dh_conn', c_void_p)
    ]

class Client(LibPhobos):
    """High-level, object-oriented, double-keyworded DSS wrappers for the CLI"""
    def __init__(self, *args, **kwargs):
        """Initialize a new DSS context."""
        super(Client, self).__init__(*args, **kwargs)
        self.handle = None
        self.media = MediaManager(self)
        self.devices = DeviceManager(self)

    def connect(self, **kwargs):
        """ Establish a fresh connection or renew a stalled one if needed."""
        if self.handle is not None:
            self.disconnect()

        self.handle = DSSHandle()
        rcode = self.libphobos.dss_init(byref(self.handle))
        if rcode != 0:
            raise GenericError('DSS initialization failed')

    def disconnect(self):
        """Disconnect from DSS and reset handle."""
        if self.handle is not None:
            self.libphobos.dss_fini(byref(self.handle))
            self.handle = None
