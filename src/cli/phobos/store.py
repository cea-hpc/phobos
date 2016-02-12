#!/usr/bin/python

# Copyright CEA/DAM 2016
# This file is part of the Phobos project

"""
High level interface to access the object store.
"""

import logging
import os.path
import errno

import phobos.capi.store as cstore


def xfer_descriptor(oid, path, flags, attrs):
    """Instanciate and initialize a new xfer_descriptor."""
    xfer = cstore.pho_xfer_desc()
    xfer.xd_objid = oid
    xfer.xd_fpath = path
    xfer.xd_flags = flags
    if attrs:
        xfer.xd_attrs = cstore.pho_attrs()
        for k, v in attrs.iteritems():
            cstore.pho_attr_set(xfer.xd_attrs, str(k), str(v))
    return xfer


class Client(object):
    """Main class: issue data transfers with the object store."""
    def __init__(self, **kwargs):
        """Initialize a new instance."""
        super(Client, self).__init__(**kwargs)
        self.logger = logging.getLogger(__name__)
        self.get_session = []
        self.put_session = []

    def noop_compl_cb(self, *args, **kwargs):
        """Default, empty transfer completion handler."""
        pass

    def get_register(self, object_id, data_path, md_only=False, attrs=None):
        """Enqueue a GET or GETATTR transfer."""
        flags = 0
        if md_only:
            flags |= cstore.PHO_XFER_OBJ_GETATTR
        desc = xfer_descriptor(object_id, data_path, flags, attrs)
        self.get_session.append(desc)

    def put_register(self, object_id, data_path, attrs=None):
        """Enqueue a PUT transfert."""
        desc = xfer_descriptor(object_id, data_path, 0, attrs)
        self.put_session.append(desc)

    def clear(self):
        """Release resources associated to the current queues."""
        for desc in self.get_session:
            cstore.pho_attrs_free(desc.xd_attrs)
        self.get_session = []
        for desc in self.put_session:
            cstore.pho_attrs_free(desc.xd_attrs)
        self.put_session = []

    def run(self, compl_cb=None, **kwargs):
        """Execute all registered transfer orders."""
        if compl_cb is None:
            compl_cb = self.noop_compl_cb

        if not callable(compl_cb):
            raise TypeError("Completion handler must be callable")

        if self.get_session:
            rc = cstore.phobos_get(self.get_session, compl_cb)
            if rc:
                return rc

        if self.put_session:
            rc = cstore.phobos_put(self.put_session, compl_cb)
            if rc:
                return rc

        self.clear()
        return rc
