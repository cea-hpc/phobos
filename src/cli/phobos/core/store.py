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
High level interface to access the object store.
"""

import logging

from ctypes import *

from phobos.core.ffi import LIBPHOBOS
from phobos.core.const import PHO_XFER_OBJ_GETATTR


AttrsForeachCBType = CFUNCTYPE(c_int, c_char_p, c_char_p, c_void_p)

class PhoAttrs(Structure):
    """Embedded hashtable, typically exposed as python dict here."""
    _fields_ = [
        ('attr_set', c_void_p)
    ]

def attrs_from_dict(dct):
    """Fill up from a python dictionary"""
    attrs = PhoAttrs()
    for k, v in dvt.iteritems():
        LIBPHOBOS.pho_attr_set(byref(attrs), str(k), str(v))
    return attrs

def attrs_as_dict(attrs):
    """Return a python dictionary containing the attributes"""
    def inner_attrs_mapper(key, val, c_data):
        """
        Not for direct use.
        Add attribute of a PhoAttr structure into a python dict.
        """
        data = cast(c_data, POINTER(py_object)).contents.value
        data[key] = val
        return 0
    res = {}
    cb = AttrsForeachCBType(inner_attrs_mapper)
    c_res = cast(pointer(py_object(res)), c_void_p)
    LIBPHOBOS.pho_attrs_foreach(byref(attrs), cb, c_res)
    return res

class XferDescriptor(Structure):
    """phobos struct xfer_descriptor."""
    _fields_ = [
        ("xd_objid", c_char_p),
        ("xd_fpath", c_char_p),
        ("xd_attrs", POINTER(PhoAttrs)),
        ("xd_flags", c_int)
    ]

XferCompletionCBType = CFUNCTYPE(None, c_void_p, POINTER(XferDescriptor), c_int)

class Store(object):
    def __init__(self, *args, **kwargs):
        """Initialize new instance."""
        super(Store, self).__init__(*args, **kwargs)
        # Keep references to CFUNCTYPES objects as long as they can be
        # called by the underlying C code, so that they do not get GC'd
        self._get_cb = None
        self._put_cb = None

    def xfer_desc_convert(self, xfer_descriptors):
        """
        Internal conversion method to turn a python list into an array of
        struct xfer_descriptor as expected by phobos_{get,put} functions.
        """
        XferArrayType = XferDescriptor * len(xfer_descriptors)
        xfr = XferArrayType()
        for i, x in enumerate(xfer_descriptors):
            xfr[i].xd_objid = x[0]
            xfr[i].xd_fpath = x[1]
            xfr[i].xd_flags = x[3]
            if x[2]:
                attrs = PhoAttrs()
                for k, v in x[2].iteritems():
                    LIBPHOBOS.pho_attr_set(byref(attrs), str(k), str(v))
                xfr[i].xd_attrs = pointer(attrs)
        return xfr

    def xfer_desc_release(self, xfer):
        """Release memory associated to xfer_descriptors."""
        for xd in xfer:
            if xd.xd_attrs:
                LIBPHOBOS.pho_attrs_free(xd.xd_attrs)

    def compl_cb_convert(self, compl_cb):
        """
        Internal conversion method to turn a python callable into a C
        completion callback as expected by phobos_{get,put} functions.
        """
        if not callable(compl_cb):
            raise TypeError("Completion handler must be callable")

        return XferCompletionCBType(compl_cb)

    def get(self, xfer_descriptors, compl_cb):
        xfer = self.xfer_desc_convert(xfer_descriptors)
        n = len(xfer_descriptors)
        self._get_cb = self.compl_cb_convert(compl_cb)
        rc = LIBPHOBOS.phobos_get(xfer, n, self._get_cb, None)
        self.xfer_desc_release(xfer)
        return rc

    def put(self, xfer_descriptors, compl_cb):
        xfer = self.xfer_desc_convert(xfer_descriptors)
        n = len(xfer_descriptors)
        self._put_cb = self.compl_cb_convert(compl_cb)
        rc = LIBPHOBOS.phobos_put(xfer, n, self._put_cb, None)
        self.xfer_desc_release(xfer)
        return rc

class Client(object):
    """Main class: issue data transfers with the object store."""
    def __init__(self, **kwargs):
        """Initialize a new instance."""
        super(Client, self).__init__(**kwargs)
        self.logger = logging.getLogger(__name__)
        self._store = Store()
        self.get_session = []
        self.put_session = []

    def noop_compl_cb(self, *args, **kwargs):
        """Default, empty transfer completion handler."""
        pass

    def get_register(self, oid, data_path, md_only=False, attrs=None):
        """Enqueue a GET or GETATTR transfer."""
        flags = 0
        if md_only:
            flags |= PHO_XFER_OBJ_GETATTR
        self.get_session.append((oid, data_path, attrs, flags))

    def put_register(self, oid, data_path, attrs=None):
        """Enqueue a PUT transfert."""
        self.put_session.append((oid, data_path, attrs, 0))

    def clear(self):
        """Release resources associated to the current queues."""
        self.get_session = []
        self._get_cb = None
        self.put_session = []
        self._put_cb = None

    def run(self, compl_cb=None, **kwargs):
        """Execute all registered transfer orders."""
        if compl_cb is None:
            compl_cb = self.noop_compl_cb

        if self.get_session:
            rc = self._store.get(self.get_session, compl_cb)
            if rc:
                raise IOError(rc, "Cannot retrieve objects")

        if self.put_session:
            rc = self._store.put(self.put_session, compl_cb)
            if rc:
                raise IOError(rc, "Cannot retrieve objects")

        self.clear()
