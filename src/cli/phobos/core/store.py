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

import errno
import logging
import os

from ctypes import *

from phobos.core.ffi import LIBPHOBOS, Tags
from phobos.core.const import (PHO_XFER_OBJ_REPLACE, PHO_XFER_OP_GET,
                               PHO_XFER_OP_GETMD, PHO_XFER_OP_PUT)

AttrsForeachCBType = CFUNCTYPE(c_int, c_char_p, c_char_p, c_void_p)

class PhoAttrs(Structure):
    """Embedded hashtable, typically exposed as python dict here."""
    _fields_ = [
        ('attr_set', c_void_p)
    ]

    def __init__(self):
        self.attr_set = 0

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
        ("xd_op", c_int),
        ("xd_fd", c_int),
        ("xd_size", c_ssize_t),
        ("xd_layout_name", c_char_p),
        ("xd_attrs", PhoAttrs),
        ("xd_flags", c_int),
        ("xd_tags", Tags),
        ("xd_rc", c_int),
    ]

    def creat_flags(self):
        """
        Select the open flags depending on the operation made on the object
        when the file is created.
        Return the selected flags.
        """
        if self.xd_flags & PHO_XFER_OBJ_REPLACE:
            return os.O_CREAT | os.O_WRONLY | os.O_TRUNC

        return os.O_CREAT | os.O_WRONLY | os.O_EXCL

    def open_file(self, path):
        """
        Retrieve the xd_fd field of the xfer, opening path with NOATIME if
        the xfer has not been previously opened.
        Return the fd or raise OSError if the open failed or ValueError if
        the given path is not correct.
        """
        # in case of getmd, the file is not opened, return without exception
        if self.xd_op == PHO_XFER_OP_GETMD:
            self.xd_fd = -1
            self.xd_size = -1
            return

        if not path:
            raise ValueError("path must be a non empty string")

        if self.xd_op == PHO_XFER_OP_GET:
            self.xd_fd = os.open(path, self.creat_flags(), 0666)
        else:
            try:
                self.xd_fd = os.open(path, os.O_RDONLY | os.O_NOATIME)
            except OSError as e:
                # not allowed to open with NOATIME arg, try without
                if e.errno != errno.EPERM:
                    raise e
                self.xd_fd = os.open(path, os.O_RDONLY)

        self.xd_size = os.fstat(self.xd_fd).st_size

    def init_from_descriptor(self, desc):
        """
        xfer_descriptor initialization by using python-list descriptor.
        It opens the file descriptor of the given path. The python-list
        contains the tuple (id, path, attrs, flags, tags, op) describing
        the opened file.
        """
        self.xd_objid = desc[0]
        self.xd_op = desc[5]
        self.xd_layout_name = 0
        self.xd_flags = desc[3]
        self.xd_tags = Tags(desc[4])
        self.xd_rc = 0

        if desc[2]:
            for k, v in desc[2].iteritems():
                rc = LIBPHOBOS.pho_attr_set(byref(self.xd_attrs),
                                            str(k), str(v))
                if rc:
                    raise EnvironmentError(
                        rc, "Cannot add attr to xfer objid:'%s'" % (desc[0],))

        self.open_file(desc[1])

XferCompletionCBType = CFUNCTYPE(None, c_void_p, POINTER(XferDescriptor), c_int)

class Store(object):
    def __init__(self, *args, **kwargs):
        """Initialize new instance."""
        super(Store, self).__init__(*args, **kwargs)
        # Keep references to CFUNCTYPES objects as long as they can be
        # called by the underlying C code, so that they do not get GC'd
        self.logger = logging.getLogger(__name__)
        self._cb = None

    def xfer_desc_convert(self, xfer_descriptors):
        """
        Internal conversion method to turn a python list into an array of
        struct xfer_descriptor as expected by phobos_{get,put} functions.
        xfer_descriptors is a list of (id, path, attrs, flags, tags, op).
        The element conversion is made by the xfer_descriptor initializer.
        """
        XferArrayType = XferDescriptor * len(xfer_descriptors)
        xfr = XferArrayType()
        for i, x in enumerate(xfer_descriptors):
            xfr[i].init_from_descriptor(x)

        return xfr

    def xfer_desc_release(self, xfer):
        """
        Release memory associated to xfer_descriptors in both phobos and cli.
        """
        for xd in xfer:
            if xd.xd_fd >= 0:
                os.close (xd.xd_fd)

            LIBPHOBOS.pho_xfer_desc_destroy(byref(xfer))

    def compl_cb_convert(self, compl_cb):
        """
        Internal conversion method to turn a python callable into a C
        completion callback as expected by phobos_{get,put} functions.
        """
        if not callable(compl_cb):
            raise TypeError("Completion handler must be callable")

        return XferCompletionCBType(compl_cb)

    def phobos_xfer(self, action_func, xfer_descriptors, compl_cb):
        xfer = self.xfer_desc_convert(xfer_descriptors)
        n = len(xfer_descriptors)
        self._cb = self.compl_cb_convert(compl_cb)
        rc = action_func(xfer, n, self._cb, None)
        self.xfer_desc_release(xfer)
        return rc

class Client(object):
    """Main class: issue data transfers with the object store."""
    def __init__(self, **kwargs):
        """Initialize a new instance."""
        super(Client, self).__init__(**kwargs)
        self.logger = logging.getLogger(__name__)
        self._store = Store()
        self.getmd_session = []
        self.get_session = []
        self.put_session = []

    def noop_compl_cb(self, *args, **kwargs):
        """Default, empty transfer completion handler."""
        pass

    def getmd_register(self, oid, data_path, attrs=None):
        """Enqueue a GETMD transfer."""
        self.getmd_session.append((oid, data_path, attrs, 0, None,
                                 PHO_XFER_OP_GETMD))

    def get_register(self, oid, data_path, attrs=None):
        """Enqueue a GET transfer."""
        self.get_session.append((oid, data_path, attrs, 0, None,
                                 PHO_XFER_OP_GET))

    def put_register(self, oid, data_path, attrs=None, tags=None):
        """Enqueue a PUT transfert."""
        self.put_session.append((oid, data_path, attrs, 0, tags,
                                 PHO_XFER_OP_PUT))

    def clear(self):
        """Release resources associated to the current queues."""
        self.getmd_session = []
        self._getmd_cb = None
        self.get_session = []
        self._get_cb = None
        self.put_session = []
        self._put_cb = None

    # TODO: in case phobos_xfer is called from phobos instead of
    # phobos_{getmd,get,put}, merge the sessions into one attribute.
    def run(self, compl_cb=None, **kwargs):
        """Execute all registered transfer orders."""
        if compl_cb is None:
            compl_cb = self.noop_compl_cb

        if self.getmd_session:
            rc = self._store.phobos_xfer(LIBPHOBOS.phobos_getmd,
                                         self.getmd_session, compl_cb)
            if rc:
                raise IOError(rc, "Cannot get md on objects")

        if self.get_session:
            rc = self._store.phobos_xfer(LIBPHOBOS.phobos_get,
                                         self.get_session, compl_cb)
            if rc:
                raise IOError(rc, "Cannot retrieve objects")

        if self.put_session:
            rc = self._store.phobos_xfer(LIBPHOBOS.phobos_put,
                                         self.put_session, compl_cb)
            if rc:
                raise IOError(rc, "Cannot store objects")

        self.clear()
