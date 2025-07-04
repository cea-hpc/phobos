#!/usr/bin/env python3

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
High level interface to access the object store.
"""

import errno
import logging
import os

from collections import namedtuple
from ctypes import (byref, c_bool, c_char_p, c_int, c_ssize_t, c_void_p, cast,
                    CFUNCTYPE, pointer, POINTER, py_object, Structure, Union)

from phobos.core.ffi import (LIBPHOBOS, DeprecatedObjectInfo, ObjectInfo,
                             StringArray, CopyInfo)
from phobos.core.const import (PHO_XFER_OBJ_REPLACE, PHO_XFER_OBJ_BEST_HOST, # pylint: disable=no-name-in-module
                               PHO_XFER_OBJ_HARD_DEL, PHO_XFER_COPY_HARD_DEL,
                               PHO_XFER_OP_COPY, PHO_XFER_OP_GET,
                               PHO_XFER_OP_GETMD, PHO_XFER_OP_PUT,
                               PHO_RSC_INVAL, str2rsc_family, DSS_OBJ_ALIVE)
from phobos.core.dss import dss_sort

ATTRS_FOREACH_CB_TYPE = CFUNCTYPE(c_int, c_char_p, c_char_p, c_void_p)

class PhoAttrs(Structure): # pylint: disable=too-few-public-methods
    """Embedded hashtable, typically exposed as python dict here."""
    _fields_ = [
        ('attr_set', c_void_p)
    ]

    def __init__(self):
        super().__init__()
        self.attr_set = 0

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
    callback = ATTRS_FOREACH_CB_TYPE(inner_attrs_mapper)
    c_res = cast(pointer(py_object(res)), c_void_p)
    LIBPHOBOS.pho_attrs_foreach(byref(attrs), callback, c_res)
    res = {k.decode('utf-8'): v.decode('utf-8') for k, v in res.items()}
    return res

class PhoListFilters(Structure): # pylint: disable=too-few-public-methods, too-many-instance-attributes
    """Phobos list filters parameters."""
    _fields_ = [
        ("res", POINTER(c_char_p)),
        ("n_res", c_int),
        ("_uuid", c_char_p),
        ("version", c_int),
        ("is_pattern", c_bool),
        ("metadata", POINTER(c_char_p)),
        ("n_metadata", c_int),
        ("status_filter", c_int),
        ("_copy_name", c_char_p)
    ]

    def __init__(self, list_filters):
        super().__init__()
        if list_filters.res:
            enc_res = list([s.encode('utf-8')
                            for _, s in enumerate(list_filters.res)])
            self.res = (c_char_p * len(enc_res))(*enc_res)
        else:
            self.res = None

        self.n_res = list_filters.n_res
        self.uuid = list_filters.uuid
        self.version = list_filters.version
        self.is_pattern = list_filters.is_pattern

        if list_filters.metadata:
            enc_metadata = list([s.encode('utf-8')
                                 for _, s in enumerate(list_filters.metadata)])
            self.metadata = (c_char_p * len(enc_metadata))(*enc_metadata)
        else:
            self.metadata = None

        self.n_metadata = list_filters.n_metadata
        self.status_filter = list_filters.status_filter
        self.copy_name = list_filters.copy_name

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
        self._copy_name = val.encode('utf-8') if val else None


class ListFilters(namedtuple('ListFilters',
                             'res n_res uuid version is_pattern metadata '
                             'n_metadata status_filter copy_name')):
    """
    Transition data structure for listing filters between
    the CLI and the store.
    """
    __slots__ = ()
ListFilters.__new__.__defaults__ = (None,) * len(ListFilters._fields)

class XferPutParams(Structure): # pylint: disable=too-few-public-methods, too-many-instance-attributes
    """Phobos PUT parameters of the XferDescriptor."""
    _fields_ = [
        ("family", c_int),
        ("_grouping", c_char_p),
        ("_library", c_char_p),
        ("_layout_name", c_char_p),
        ("lyt_params", PhoAttrs),
        ("tags", StringArray),
        ("_profile", c_char_p),
        ("_copy_name", c_char_p),
        ("overwrite", c_bool),
        ("no_split", c_bool),
    ]

    def set_lyt_params(self, val):
        """Insert key/values attributes in Xfer's layout params"""
        if val:
            for k, v in val.items():
                LIBPHOBOS.pho_attr_set(byref(self.lyt_params),
                                       str(k).encode('utf8'),
                                       str(v).encode('utf8'))

    def __init__(self, put_params):
        super().__init__()
        self.grouping = put_params.grouping
        self.library = put_params.library
        self.layout_name = put_params.layout
        self.set_lyt_params(put_params.lyt_params)
        self.tags = StringArray(put_params.tags)
        self.profile = put_params.profile
        self.copy_name = put_params.copy_name
        self.overwrite = put_params.overwrite
        self.no_split = put_params.no_split

        if put_params.family is None:
            self.family = PHO_RSC_INVAL
        else:
            self.family = str2rsc_family(put_params.family)

    @property
    def grouping(self):
        """Wrapper to get grouping"""
        return self._grouping.decode('utf-8') if self._grouping else None

    @grouping.setter
    def grouping(self, val):
        """Wrapper to set grouping"""
        # pylint: disable=attribute-defined-outside-init
        self._grouping = val.encode('utf-8') if val else None

    @property
    def library(self):
        """Wrapper to get library"""
        return self._library.decode('utf-8') if self._library else None

    @library.setter
    def library(self, val):
        """Wrapper to set library"""
        # pylint: disable=attribute-defined-outside-init
        self._library = val.encode('utf-8') if val else None

    @property
    def layout_name(self):
        """Wrapper to get layout_name"""
        return self._layout_name.decode('utf-8') if self._layout_name else None

    @layout_name.setter
    def layout_name(self, val):
        """Wrapper to set layout_name"""
        # pylint: disable=attribute-defined-outside-init
        self._layout_name = val.encode('utf-8') if val else None

    @property
    def profile(self):
        """Wrapper to get profile"""
        return self._profile.decode('utf-8') if self._profile else None

    @profile.setter
    def profile(self, val):
        """Wrapper to set profile"""
        # pylint: disable=attribute-defined-outside-init
        self._profile = val.encode('utf-8') if val else None

    @property
    def copy_name(self):
        """Wrapper to get copy_name"""
        return self._copy_name.decode('utf-8') if self._copy_name else None

    @copy_name.setter
    def copy_name(self, val):
        """Wrapper to set copy_name"""
        # pylint: disable=attribute-defined-outside-init
        self._copy_name = val.encode('utf-8') if val else None

class PutParams(namedtuple('PutParams',
                           'profile copy_name family grouping library layout '
                           'lyt_params no_split overwrite tags')):
    """
    Transition data structure for put parameters between
    the CLI and the XFer data structure.
    """
    __slots__ = ()
PutParams.__new__.__defaults__ = (None,) * len(PutParams._fields)

class XferGetParams(Structure): # pylint: disable=too-few-public-methods, too-many-instance-attributes
    """Phobos GET parameters of the XferDescriptor."""
    _fields_ = [
        ("_copy_name", c_char_p),
        ("scope", c_int),
        ("_node_name", c_char_p),
    ]

    def __init__(self, get_params):
        super().__init__()
        self.copy_name = get_params.copy_name
        self.node_name = get_params.node_name
        if get_params.scope is None:
            self.scope = DSS_OBJ_ALIVE
        else:
            self.scope = get_params.scope

    @property
    def copy_name(self):
        """Wrapper to get copy_name"""
        return self._copy_name.decode('utf-8') if self._copy_name else None

    @copy_name.setter
    def copy_name(self, val):
        """Wrapper to set copy_name"""
        #pylint: disable=attribute-defined-outside-init
        self._copy_name = val.encode('utf-8') if val else None

    @property
    def node_name(self):
        """Wrapper to get node_name"""
        return self._node_name.decode('utf-8') if self._node_name else None

    @node_name.setter
    def node_name(self, val):
        """Wrapper to set node_name"""
        #pylint: disable=attribute-defined-outside-init
        self._node_name = val.encode('utf-8') if val else None


class GetParams(namedtuple('GetParams', 'copy_name node_name scope')):
    """
    Transition data structure for get parameters between
    the CLI and the XFer data structure.
    """
    __slots__ = ()
GetParams.__new__.__defaults__ = (None,) * len(GetParams._fields)

class XferDelParams(Structure): # pylint: disable=too-few-public-methods, too-many-instance-attributes
    """Phobos DEL parameters of the XferDescriptor."""
    _fields_ = [
        ("_copy_name", c_char_p),
        ("scope", c_int),
    ]

    def __init__(self, del_params):
        super().__init__()
        self.copy_name = del_params.copy_name
        if del_params.scope is None:
            self.scope = DSS_OBJ_ALIVE
        else:
            self.scope = del_params.scope

    @property
    def copy_name(self):
        """Wrapper to get copy_name"""
        return self._copy_name.decode('utf-8') if self._copy_name else None

    @copy_name.setter
    def copy_name(self, val):
        """Wrapper to set copy_name"""
        #pylint: disable=attribute-defined-outside-init
        self._copy_name = val.encode('utf-8') if val else None

class DelParams(namedtuple('DelParams', 'copy_name scope')):
    """
    Transition data structure for del parameters between
    the CLI and the XFer data structure.
    """
    __slots__ = ()
DelParams.__new__.__defaults__ = (None,) * len(DelParams._fields)

class XferCopyParams(Structure): # pylint: disable=too-few-public-methods, too-many-instance-attributes
    """Phobos COPY parameters of the XferDescriptor."""
    _fields_ = [
        ("get", XferGetParams),
        ("put", XferPutParams),
    ]

    def __init__(self, get, put):
        super().__init__()
        self.get = get
        self.put = put

class CopyParams(namedtuple('CopyParams', '')):
    """
    Transition data structure for copy parameters between
    the CLI and the XFer data structure.
    """
CopyParams.__new__.__defaults__ = (None,) * len(CopyParams._fields)

class XferOpParams(Union): # pylint: disable=too-few-public-methods
    """Phobos operation parameters of the XferDescriptor."""
    _fields_ = [
        ("put", XferPutParams),
        ("get", XferGetParams),
        ("delete", XferDelParams),
        ("copy", XferCopyParams),
    ]

class XferTarget(Structure): # pylint: disable=too-many-instance-attributes
    """phobos struct xfer_descriptor."""
    _fields_ = [
        ("_xt_objid", c_char_p),
        ("_xt_objuuid", c_char_p),
        ("xt_version", c_int),
        ("xt_fd", c_int),
        ("xt_attrs", PhoAttrs),
        ("xt_size", c_ssize_t),
        ("xt_rc", c_int),
    ]

    def __init__(self):
        super().__init__()
        self.xt_fd = -1
        self.xt_version = -1
        self.xt_size = -1
        self.xt_rc = 0

    @property
    def xt_objid(self):
        """Wrapper to get xt_objid"""
        return self._xt_objid.decode('utf-8') if self._xt_objid else None

    @xt_objid.setter
    def xt_objid(self, val):
        """Wrapper to set xt_objid"""
        # pylint: disable=attribute-defined-outside-init
        self._xt_objid = val.encode('utf-8') if val else None

    @property
    def xt_objuuid(self):
        """Wrapper to get xt_objuuid"""
        return self._xt_objuuid.decode('utf-8') if self._xt_objuuid else None

    @xt_objuuid.setter
    def xt_objuuid(self, val):
        """Wrapper to set xt_objuuid"""
        # pylint: disable=attribute-defined-outside-init
        self._xt_objuuid = val.encode('utf-8') if val else None

    def open_file(self, path, xfer_desc):
        """
        Retrieve the xt_fd field of the xfer_target, opening path with NOATIME
        if the xfer target has not been previously opened.
        Return the fd or raise OSError if the open failed or ValueError if
        the given path is not correct.
        """
        # in case of getmd, the file is not opened, return without exception
        if xfer_desc.xd_op == PHO_XFER_OP_GETMD:
            self.xt_fd = -1
            return

        if not path:
            raise ValueError("path must be a non empty string")

        if xfer_desc.xd_op == PHO_XFER_OP_GET:
            self.xt_fd = os.open(path, xfer_desc.creat_flags(), 0o666)
        else:
            try:
                self.xt_fd = os.open(path, os.O_RDONLY | os.O_NOATIME)
            except OSError as exc:
                # not allowed to open with NOATIME arg, try without
                if exc.errno != errno.EPERM:
                    raise exc
                self.xt_fd = os.open(path, os.O_RDONLY)

            self.xt_size = os.fstat(self.xt_fd).st_size

    def init_from_descriptor(self, desc, xfer_desc):
        """
        xfer initialization by using python descriptor.
        It opens the file descriptor of the given path. The descriptor is a
        tuple (id, path, attrs) describing the opened file.
        """
        self.xt_objid = desc[0]

        if desc[2]:
            for k, v in desc[2].items():
                LIBPHOBOS.pho_attr_set(byref(self.xt_attrs),
                                       str(k).encode('utf8'),
                                       str(v).encode('utf8'))

        if desc[1] is not None:
            self.open_file(desc[1], xfer_desc)

class XferDescriptor(Structure): # pylint: disable=too-many-instance-attributes
    """phobos struct xfer_descriptor."""
    _fields_ = [
        ("xd_op", c_int),
        ("xd_params", XferOpParams),
        ("xd_flags", c_int),
        ("xd_rc", c_int),
        ("xd_ntargets", c_int),
        ("xd_targets", POINTER(XferTarget))
    ]

    def __init__(self):
        super().__init__()
        self.xd_op = -1
        self.xd_flags = 0
        self.xd_rc = 0
        self.xd_ntargets = 0
        self.xd_targets = None

    def creat_flags(self):
        """
        Select the open flags depending on the operation made on the object
        when the file is created.
        Return the selected flags.
        """
        if self.xd_flags & PHO_XFER_OBJ_REPLACE:
            return os.O_CREAT | os.O_WRONLY | os.O_TRUNC

        return os.O_CREAT | os.O_WRONLY | os.O_EXCL

    def init_from_descriptor(self, desc):
        """
        xfer initialization by using python-list descriptor. The python-list
        contains the tuple
        ([(id, path, attrs), ...], flags, put or get parameters, op)
        describing the opened file.
        """
        self.xd_ntargets = len(desc[0])
        self.xd_flags = desc[1]
        self.xd_op = desc[3]
        self.xd_rc = 0

        xfer_item = XferTarget * self.xd_ntargets
        self.xd_targets = xfer_item()
        for idx, target in enumerate(desc[0]):
            self.xd_targets[idx].init_from_descriptor(target, self)

        if self.xd_op == PHO_XFER_OP_PUT:
            self.xd_params.put = XferPutParams(desc[2]) if \
                                 desc[2] is not None else \
                                 XferPutParams(PutParams())
        elif self.xd_op == PHO_XFER_OP_GET:
            # The CLI can only create a xfer with 1 target with a GET
            self.xd_targets[0].xt_objuuid = desc[2][0]
            self.xd_targets[0].xt_version = desc[2][1]
            self.xd_params.get = XferGetParams(GetParams(copy_name=desc[2][2]))
        elif self.xd_op == PHO_XFER_OP_COPY:
            self.xd_params.copy = desc[2]

XFER_COMPLETION_CB_TYPE = CFUNCTYPE(None, c_void_p, POINTER(XferDescriptor),
                                    c_int)

def compl_cb_convert(compl_cb):
    """
    Internal conversion method to turn a python callable into a C
    completion callback as expected by phobos_{get,put} functions.
    """
    if not callable(compl_cb):
        raise TypeError("Completion handler must be callable")

    return XFER_COMPLETION_CB_TYPE(compl_cb)

def xfer_targets2str(xfer_descriptors, index):
    """
    Convert one value of all targets in a xfer into a list of string
    """
    return [str(x[index]) for desc in xfer_descriptors for x in desc[0]]

class Store:
    """Store handler"""
    def __init__(self, *args, **kwargs):
        """Initialize new instance."""
        super(Store, self).__init__(*args, **kwargs)
        # Keep references to CFUNCTYPES objects as long as they can be
        # called by the underlying C code, so that they do not get GC'd
        self.logger = logging.getLogger(__name__)
        self._cb = None

    @staticmethod
    def xfer_desc_convert(xfer_descriptors):
        """
        Internal conversion method to turn a python list into an array of
        struct xfer_descriptor as expected by phobos_{get,put} functions.
        xfer_descriptors is a list of
        ([(id, path, attrs), ...], flags, tags, op).
        The element conversion is made by the xfer_descriptor initializer.
        """
        xfer_array_type = XferDescriptor * len(xfer_descriptors)
        xfr = xfer_array_type()
        for i, val in enumerate(xfer_descriptors):
            xfr[i].init_from_descriptor(val)

        return xfr

    @staticmethod
    def xfer_desc_release(xfer):
        """
        Release memory associated to xfer_descriptors in both phobos and cli.
        """
        for elt in xfer:
            for i in range(elt.xd_ntargets):
                if elt.xd_targets[i].xt_fd >= 0:
                    os.close(elt.xd_targets[i].xt_fd)

            LIBPHOBOS.pho_xfer_desc_clean(byref(elt))

    def phobos_xfer(self, action_func, xfer_descriptors, compl_cb):
        """Wrapper for phobos_xfer API calls."""
        xfer = self.xfer_desc_convert(xfer_descriptors)
        n_xfer = len(xfer_descriptors)
        self._cb = compl_cb_convert(compl_cb)
        rc = action_func(xfer, n_xfer, self._cb, None)
        node_name = (xfer[0].xd_params.get.node_name
                     if xfer[0].xd_op == PHO_XFER_OP_GET
                     else None)
        self.xfer_desc_release(xfer)
        return rc, node_name

class XferClient: # pylint: disable=too-many-instance-attributes
    """Main class: issue data transfers with the object store."""
    def __init__(self, **kwargs):
        """Initialize a new instance."""
        super(XferClient, self).__init__(**kwargs)
        self.logger = logging.getLogger(__name__)
        self._store = Store()
        self.getmd_session = []
        self.get_session = []
        self.put_session = []
        self.copy_session = []
        self._getmd_cb = None
        self._get_cb = None
        self._put_cb = None
        self._copy_cb = None

    def noop_compl_cb(self, *args, **kwargs):
        """Default, empty transfer completion handler."""

    def getmd_register(self, oid, data_path, attrs=None):
        """Enqueue a GETMD transfer."""
        self.getmd_session.append(([(oid, data_path, attrs)], 0, None,
                                   PHO_XFER_OP_GETMD))

    def get_register(self, oid, data_path, get_args, best_host, attrs=None):
        # pylint: disable=too-many-arguments
        """Enqueue a GET transfer."""
        flags = PHO_XFER_OBJ_BEST_HOST if best_host else 0
        self.get_session.append(([(oid, data_path, attrs)], flags, get_args,
                                 PHO_XFER_OP_GET))

    def put_register(self, oid, data_path, attrs=None, put_params=PutParams()):
        """Enqueue a PUT transfert."""
        self.put_session.append(([(oid, data_path, attrs)], 0, put_params,
                                 PHO_XFER_OP_PUT))

    def mput_register(self, mput_list, put_params=PutParams()):
        """Enqueue a MPUT transfert."""
        self.put_session.append((mput_list, 0, put_params, PHO_XFER_OP_PUT))

    def copy_register(self, oid, copy_params, attrs=None):
        """Enqueue a COPY transfert."""
        self.copy_session.append(([(oid, None, attrs)], 0, copy_params,
                                  PHO_XFER_OP_COPY))

    def clear(self):
        """Release resources associated to the current queues."""
        self.getmd_session = []
        self._getmd_cb = None
        self.get_session = []
        self._get_cb = None
        self.put_session = []
        self._put_cb = None
        self.copy_session = []
        self._copy_cb = None

    def run(self, compl_cb=None):
        """Execute all registered transfer orders."""
        if compl_cb is None:
            compl_cb = self.noop_compl_cb

        if self.getmd_session:
            rc, _ = self._store.phobos_xfer(LIBPHOBOS.phobos_getmd,
                                            self.getmd_session, compl_cb)
            if rc:
                full_oids = ", ".join(xfer_targets2str(self.getmd_session, 0))
                raise IOError(rc, "Cannot GETMD for objid(s) '%s'" % full_oids)

        if self.get_session:
            rc, node_name = self._store.phobos_xfer(LIBPHOBOS.phobos_get,
                                                    self.get_session,
                                                    compl_cb)

            if node_name is not None:
                print("Current host is not the best to get this object, try " +
                      "on these other nodes, '" +
                      ", ".join(xfer_targets2str(self.get_session, 0)) +
                      "' : '" + node_name + "'")

            if rc:
                for desc in self.get_session:
                    for target in desc[0]:
                        os.remove(target[1])

                full_oids = ", ".join(xfer_targets2str(self.get_session, 0))
                full_paths = ", ".join(xfer_targets2str(self.get_session, 1))
                raise IOError(rc, "Cannot GET objid(s) '%s' to '%s'" %
                              (full_oids, full_paths))

        if self.put_session:
            rc, _ = self._store.phobos_xfer(LIBPHOBOS.phobos_put,
                                            self.put_session, compl_cb)
            if rc:
                full_oids = ", ".join(xfer_targets2str(self.put_session, 0))
                full_paths = ", ".join(xfer_targets2str(self.put_session, 1))
                raise IOError(rc, "Cannot PUT '%s' to objid(s) '%s'" %
                              (full_paths, full_oids))

        if self.copy_session:
            rc, _ = self._store.phobos_xfer(LIBPHOBOS.phobos_copy,
                                            self.copy_session, compl_cb)
            if rc:
                full_oids = ", ".join(xfer_targets2str(self.copy_session, 0))
                raise IOError(rc, "Cannot COPY '%s'" % full_oids)

        self.clear()

class UtilClient:
    """Secondary class: issue user commands without data transfers."""
    def __init__(self, **kwargs):
        """Initialize a new instance."""
        super(UtilClient, self).__init__(**kwargs)
        self.logger = logging.getLogger(__name__)

    @staticmethod
    def object_delete(oids, uuid, version, del_params, hard_delete):
        """Delete objects."""
        n_xfers = c_int(len(oids))
        xfer_array_type = XferDescriptor * len(oids)
        xfers = xfer_array_type()

        for i, oid in enumerate(oids):
            xfers[i].xd_ntargets = 1
            target = XferTarget * 1
            xfers[i].xd_targets = target()
            xfers[i].xd_targets[0].xt_objid = oid
            if uuid is not None:
                xfers[i].xd_targets[0].xt_objuuid = uuid
            if version is not None:
                xfers[i].xd_targets[0].xt_version = version
            xfers[i].xd_flags = PHO_XFER_OBJ_HARD_DEL if hard_delete else 0
            xfers[i].xd_params.delete = XferDelParams(del_params)

        rc = LIBPHOBOS.phobos_delete(xfers, n_xfers)
        if rc:
            raise EnvironmentError(rc, "Failed to delete objects '%s'" % oids)

    @staticmethod
    def object_undelete(oids, uuid):
        """Undelete objects."""
        n_xfers = c_int(len(oids))
        xfer_array_type = XferDescriptor * (len(oids))
        xfers = xfer_array_type()

        for i, oid in enumerate(oids):
            xfers[i].xd_ntargets = 1
            target = XferTarget * 1
            xfers[i].xd_targets = target()
            xfers[i].xd_targets[0].xt_objid = oid
            if uuid is not None:
                xfers[i].xd_targets[0].xt_objuuid = uuid

        rc = LIBPHOBOS.phobos_undelete(xfers, n_xfers)
        if rc:
            raise EnvironmentError(rc, "Failed to undelete objects '%s'" %
                                   oids)

    @staticmethod
    def object_list(res, is_pattern, metadata, uuid, version, scope, **kwargs): # pylint: disable=too-many-arguments,too-many-locals
        """List objects."""
        n_objs = c_int(0)
        obj_type = ObjectInfo if scope == DSS_OBJ_ALIVE else \
                        DeprecatedObjectInfo
        objs = POINTER(obj_type)()

        sort, kwargs = dss_sort('object', **kwargs)
        sref = byref(sort) if sort else None

        list_filters = ListFilters(res=res, n_res=len(res), uuid=uuid,
                                   version=version, is_pattern=is_pattern,
                                   metadata=metadata, n_metadata=len(metadata),
                                   status_filter=0, copy_name=None)

        filters_ref = byref(PhoListFilters(list_filters))
        rc = LIBPHOBOS.phobos_store_object_list(filters_ref,
                                                scope, byref(objs),
                                                byref(n_objs), sref)

        if rc:
            raise EnvironmentError(rc, "Failed to list %s" %
                                   ("object(s) '%s'" % res
                                    if res else "all objects"))

        objs = (obj_type * n_objs.value).from_address(
            cast(objs, c_void_p).value)

        return objs

    @staticmethod
    def list_obj_free(objs, n_objs):
        """Free a previously obtained object list."""
        LIBPHOBOS.phobos_store_object_list_free(objs, n_objs)

    @staticmethod
    def object_rename(old_oid, uuid, new_oid):
        """Rename an object"""
        rc = LIBPHOBOS.phobos_rename(
            old_oid.encode('utf-8'),
            uuid.encode('utf-8') if uuid else None,
            new_oid.encode('utf-8'))
        if rc:
            raise EnvironmentError(rc,
                                   f"Failed to rename object '{old_oid}' to"
                                   "'{new_oid}'")

    @staticmethod
    def object_locate(oid, uuid, version, focus_host, copy_name):
        """Locate an object"""
        hostname = c_char_p(None)
        nb_new_lock = c_int(0)
        rc = LIBPHOBOS.phobos_locate(
            oid.encode('utf-8'),
            uuid.encode('utf-8') if uuid else None,
            version,
            focus_host.encode('utf-8') if focus_host else None,
            copy_name.encode('utf-8') if copy_name else None,
            byref(hostname), byref(nb_new_lock))
        if rc:
            raise EnvironmentError(rc, "Failed to locate object by %s '%s'" %
                                   ("oid" if oid else "uuid",
                                    oid if oid else uuid))

        return (hostname.value.decode('utf-8') if hostname.value else "",
                nb_new_lock)

    @staticmethod
    def copy_list(res, uuid, version, copy_name, scope, status_number): # pylint: disable=too-many-arguments
        """List copies."""
        n_copy = c_int(0)
        copy = POINTER(CopyInfo)()

        list_filters = ListFilters(res=res, n_res=len(res), uuid=uuid,
                                   version=version, is_pattern=False,
                                   metadata=None, n_metadata=0,
                                   status_filter=status_number,
                                   copy_name=copy_name)

        filters_ref = byref(PhoListFilters(list_filters))
        rc = LIBPHOBOS.phobos_store_copy_list(filters_ref,
                                              scope, byref(copy), byref(n_copy),
                                              None)

        if rc:
            raise EnvironmentError(rc, "Failed to list %s" %
                                   ("copies '%s'" % res
                                    if res else "all copies"))

        copy = (CopyInfo * n_copy.value).from_address(
            cast(copy, c_void_p).value)

        return copy

    @staticmethod
    def list_cpy_free(copy, n_copy):
        """Free a previously obtained copy list."""
        LIBPHOBOS.phobos_store_copy_list_free(copy, n_copy)

    @staticmethod
    def copy_delete(oid, uuid, version, del_params):
        """Delete copies."""
        n_xfers = c_int(1)
        xfer_array_type = XferDescriptor * 1
        xfers = xfer_array_type()

        xfers[0].xd_ntargets = 1
        target = XferTarget * 1
        xfers[0].xd_targets = target()
        xfers[0].xd_targets[0].xt_objid = oid
        if uuid is not None:
            xfers[0].xd_targets[0].xt_objuuid = uuid
        if version is not None:
            xfers[0].xd_targets[0].xt_version = version
        xfers[0].xd_params.delete = XferDelParams(del_params)
        xfers[0].xd_flags = PHO_XFER_COPY_HARD_DEL

        rc = LIBPHOBOS.phobos_copy_delete(xfers, n_xfers)

        if rc:
            raise EnvironmentError(rc, "Failed to delete '%s''s copy '%s' " %
                                   (oid, del_params.copy_name))
