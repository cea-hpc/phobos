#!/usr/bin/python

# Copyright CEA/DAM 2016
# This file is part of the Phobos project

"""
High level interface to access the object store.
"""

import logging
import os.path

import phobos.capi.store as cstore


class Client(object):
    """Main class: issue data transfers with the object store."""
    def __init__(self, **kwargs):
        """Initialize a new instance."""
        super(Client, self).__init__(**kwargs)
        self.logger = logging.getLogger(__name__)
        self.session = []

    def xfer_register(self, object_id, data_path, **kwargs):
        """Enqueue a new transfer."""
        xfer = cstore.pho_xfer_desc()

        xfer.xd_objid = object_id
        xfer.xd_fpath = data_path
        xfer.xd_flags = 0; # XXX No flag supported for now

        attributes = kwargs.get('attrs')
        if attributes:
            xfer.xd_attrs = cstore.pho_attrs()
            for k, v in attributes.iteritems():
                cstore.pho_attr_set(xfer.xd_attrs, str(k), str(v))

        self.session.append(xfer)

    def session_clear(self):
        """Release resources associated to the current session."""
        for xdesc in self.session:
            if xdesc.xd_attrs:
                cstore.pho_attrs_free(xdesc.xd_attrs)
        self.session = []

    def put(self, **kwargs):
        """Execute all registered transfer orders as PUTs."""
        rc = cstore.phobos_mput(self.session)
        self.session_clear()
        return rc

    def get(self, **kwargs):
        """Retrieve object from backend."""
        if len(self.session) > 1:
            raise NotImplementedError("Bulk GET not implemented")

        rc = cstore.phobos_get(self.session[0])
        self.session_clear()
        return rc
