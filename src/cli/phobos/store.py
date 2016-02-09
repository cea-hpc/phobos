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


class Client(object):
    """Main class: issue data transfers with the object store."""
    _OP_GET = 0
    _OP_PUT = 1

    def __init__(self, **kwargs):
        """Initialize a new instance."""
        super(Client, self).__init__(**kwargs)
        self.logger = logging.getLogger(__name__)
        self.session = []
        self._optype = None

    def noop_compl_cb(self, *args, **kwargs):
        """Default, empty transfer completion handler."""
        pass

    def get_register(self, object_id, data_path, md_only=False, **kwargs):
        """Enqueue a GET or GETATTR transfer."""
        if self._optype is None:
            self._optype = self._OP_GET
        elif self._optype != self._OP_GET:
            raise RuntimeError("Cannot mix GET and PUT operations")

        flags = 0
        if md_only:
            flags |= cstore.PHO_XFER_OBJ_GETATTR

        attributes = kwargs.get('attrs')
        self._xfer_register(object_id, data_path, flags, attributes)

    def put_register(self, object_id, data_path, **kwargs):
        """Enqueue a PUT transfert."""
        if self._optype is None:
            self._optype = self._OP_PUT
        elif self._optype != self._OP_PUT:
            raise RuntimeError("Cannot mix GET and PUT operations")

        attributes = kwargs.get('attrs')
        self._xfer_register(object_id, data_path, 0, attributes)

    def _xfer_register(self, oid, path, flags, attrs):
        """Register a generic transfer order."""
        xfer = cstore.pho_xfer_desc()

        xfer.xd_objid = oid
        xfer.xd_fpath = path
        xfer.xd_flags = flags
        if attrs:
            xfer.xd_attrs = cstore.pho_attrs()
            for k, v in attrs.iteritems():
                cstore.pho_attr_set(xfer.xd_attrs, str(k), str(v))

        self.session.append(xfer)

    def session_clear(self):
        """Release resources associated to the current session."""
        for xdesc in self.session:
            if xdesc.xd_attrs:
                cstore.pho_attrs_free(xdesc.xd_attrs)
        self.session = []
        self._optype = None

    def run(self, compl_cb=None, **kwargs):
        """Execute all registered transfer orders."""
        if compl_cb is None:
            compl_cb = self.noop_compl_cb

        if not callable(compl_cb):
            raise TypeError("Completion handler must be callable")

        if self._optype is None:
            self.logger.warning("No operation registered")
            return 0
        elif self._optype == self._OP_GET:
            rc = cstore.phobos_get(self.session[0], compl_cb)
        elif self._optype == self._OP_PUT:
            rc = cstore.phobos_mput(self.session, compl_cb)
        else:
            self.logger.error("Unknown operation type %x" % self._optype)
            return -errno.EINVAL

        self.session_clear()
        return rc
