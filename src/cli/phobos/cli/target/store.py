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
Store target for Phobos CLI
"""

import os
import sys

from phobos.cli.action.get import GetOptHandler
from phobos.cli.action.getmd import GetMDOptHandler
from phobos.cli.action.mput import MPutOptHandler
from phobos.cli.action.put import PutOptHandler
from phobos.cli.common import env_error_format, XferOptHandler
from phobos.cli.common.utils import (attr_convert, create_put_params,
                                     mput_file_line_parser)
from phobos.core.store import attrs_as_dict

class StoreGetMDOptHandler(XferOptHandler):
    """Retrieve object from backend."""
    label = 'getmd'
    descr = 'retrieve object metadata from backend'

    @classmethod
    def add_options(cls, parser):
        """Add command options."""
        GetMDOptHandler(cls).add_options(parser)

    @staticmethod
    def _compl_notify(data, xfr, err_code): # pylint: disable=unused-argument
        """Custom completion handler to display metadata."""
        if err_code != 0:
            return

        res = []
        itm = attrs_as_dict(xfr.contents.xd_targets[0].xt_attrs)
        if not itm:
            return

        for key, value in sorted(itm.items()):
            res.append('%s=%s' % (key, value))

        print(','.join(res))

    def exec_getmd(self):
        """Retrieve an object attributes from backend."""
        oid = self.params.get('object_id')
        self.logger.debug("Retrieving attrs for 'objid:%s'", oid)
        self.client.getmd_register(oid, None)
        try:
            self.client.run(compl_cb=self._compl_notify)
        except IOError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))


class StoreGetOptHandler(XferOptHandler):
    """Retrieve object from backend."""
    label = 'get'
    descr = 'retrieve object from backend'

    @classmethod
    def add_options(cls, parser):
        """Add command options."""
        GetOptHandler(cls).add_options(parser)

    def exec_get(self):
        """Retrieve an object from backend."""
        oid = self.params.get('object_id')
        dst = self.params.get('dest_file')
        version = self.params.get('version')
        uuid = self.params.get('uuid')
        best_host = self.params.get('best_host')
        copy_name = self.params.get('copy_name')
        self.logger.debug("Retrieving object 'objid:%s' to '%s'", oid, dst)
        self.client.get_register(oid, dst, (uuid, version, copy_name),
                                 best_host)
        try:
            self.client.run()

        except IOError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))

        if version != 0:
            if uuid is not None:
                self.logger.info("Object '%s@%d' with uuid '%s' "
                                 "successfully retrieved",
                                 oid, version, uuid)
            else:
                self.logger.info("Object '%s@%d' successfully retrieved",
                                 oid, version)
        else:
            if uuid is not None:
                self.logger.info("Object '%s' with uuid '%s' "
                                 "successfully retrieved",
                                 oid, uuid)
            else:
                self.logger.info("Object '%s' successfully retrieved", oid)


class StorePutOptHandler(XferOptHandler):
    """Insert objects into backend."""
    label = "put"
    descr = 'insert object into backend. Either "--file" or "src_file" and '\
            '"oid" must be provided.'

    @classmethod
    def add_options(cls, parser):
        """Add options for the PUT command."""
        PutOptHandler(cls).add_options(parser)

    def register_multi_puts(self, mput_file, put_params):
        """Register the put requests from the given file"""
        if mput_file == '-':
            fin = sys.stdin
        else:
            fin = open(mput_file)

        mput_list = []
        for i, line in enumerate(fin):
            # Skip empty lines and comments
            line = line.strip()
            if not line or line.startswith('#'):
                continue

            try:
                match = mput_file_line_parser(line)
            except ValueError as err:
                self.logger.error("Format error on line %d: %s: %s", i + 1,
                                  str(err), line)
                sys.exit(os.EX_DATAERR)

            src = match[0]
            oid = match[1]
            attrs = match[2]

            if attrs == '-':
                attrs = None
            else:
                attrs = attr_convert(attrs)
                self.logger.debug("Loaded attributes set %r", attrs)

            self.logger.debug("Inserting object '%s' to 'objid:%s'", src, oid)
            # A xfer with N objects will be built in order to group the alloc
            mput_list.append((oid, src, attrs))

        self.client.mput_register(mput_list, put_params=put_params)

        if fin is not sys.stdin:
            fin.close()


    def exec_put(self):
        """Insert an object into backend."""
        src = self.params.get('src_file')
        oid = self.params.get('object_id')
        mput_file = self.params.get('file')

        if not mput_file and (not src and not oid):
            self.logger.error("either '--file' or 'src_file'/'oid' must be "
                              "provided")
            sys.exit(os.EX_USAGE)
        elif mput_file and (src or oid):
            self.logger.error("only one of '--file' or 'src_file'/'oid' must "
                              "be provided")
            sys.exit(os.EX_USAGE)

        if not mput_file and not (src and oid):
            self.logger.error("both src and oid must be provided")
            sys.exit(os.EX_USAGE)

        attrs = self.params.get('metadata')
        if attrs is not None:
            attrs = attr_convert(attrs)
            self.logger.debug("Loaded attributes set %r", attrs)

        put_params = create_put_params(self, self.params.get('copy_name'))

        if mput_file:
            self.register_multi_puts(mput_file, put_params)
        else:
            self.client.put_register(oid, src, attrs=attrs,
                                     put_params=put_params)
            self.logger.debug("Inserting object '%s' to 'objid:%s'", src, oid)

        try:
            self.client.run()
        except IOError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))


class StoreMPutOptHandler(StorePutOptHandler):
    """Deprecated, use 'put --file' instead."""
    label = 'mput'
    descr = "Deprecated, use 'put --file' instead."

    @classmethod
    def add_options(cls, parser):
        """Add options for the PUT command."""
        MPutOptHandler(cls).add_options(parser)

    def exec_mput(self):
        """Deprecated, use 'put --file' instead."""
        self.logger.error("'mput' is a deprecated command, use 'put --file' "
                          "instead")
        sys.exit(os.EX_USAGE)
