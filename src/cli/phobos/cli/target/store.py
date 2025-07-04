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

from phobos.cli.action.delete import DeleteOptHandler
from phobos.cli.action.get import GetOptHandler
from phobos.cli.action.getmd import GetMDOptHandler
from phobos.cli.action.locate import LocateOptHandler
from phobos.cli.action.mput import MPutOptHandler
from phobos.cli.action.put import PutOptHandler
from phobos.cli.action.rename import RenameOptHandler
from phobos.cli.action.undelete import UndeleteOptHandler
from phobos.cli.common import (BaseResourceOptHandler, env_error_format,
                               XferOptHandler)
from phobos.cli.common.utils import (attr_convert, create_put_params,
                                     get_scope, mput_file_line_parser)
from phobos.core.store import attrs_as_dict, DelParams, UtilClient


class ObjectDeleteOptHandler(DeleteOptHandler):
    """Option handler for object delete action"""
    label = 'delete'
    descr = 'remove an object from phobos namespace and '\
            'add it to deprecated objects'

    @classmethod
    def add_options(cls, parser):
        """Add command options."""
        super(ObjectDeleteOptHandler, cls).add_options(parser)
        parser.add_argument('oids', nargs='+',
                            help='Object IDs to delete')
        parser.add_argument('--hard', action='store_true',
                            help='Require a hardware remove of the object')
        parser.set_defaults(verb=cls.label)


class StoreDeleteOptHandler(BaseResourceOptHandler):
    """Delete objects handler."""
    label = 'delete'
    alias = ['del']
    descr = 'remove an object from phobos namespace and '\
            'add it to deprecated objects'

    @classmethod
    def add_options(cls, parser):
        """Add command options."""
        ObjectDeleteOptHandler(cls).add_options(parser)

    def exec_delete(self):
        """Delete objects."""
        client = UtilClient()

        deprec = self.params.get('deprecated')
        deprec_only = self.params.get('deprecated_only')
        oids = self.params.get('oids')
        uuid = self.params.get('uuid')
        version = self.params.get('version')

        if not self.params.get('hard') and (deprec or deprec_only):
            self.logger.error("--deprecated or --deprecated-only can only be "
                              "used with the --hard option")
            sys.exit(os.EX_USAGE)

        if len(oids) > 1 and (uuid is not None or version is not None):
            self.logger.error("Only one oid can be provided with the --uuid or "
                              "--version option")
            sys.exit(os.EX_USAGE)

        scope = get_scope(deprec, deprec_only)

        try:
            client.object_delete(oids, uuid, version,
                                 DelParams(copy_name=None, scope=scope),
                                 self.params.get('hard'))
        except EnvironmentError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))


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


class StoreLocateOptHandler(BaseResourceOptHandler):
    """Locate object handler."""

    label = 'locate'
    descr = 'Find the hostname which has the best access to an object if any'

    @classmethod
    def add_options(cls, parser):
        """Add command options."""
        LocateOptHandler(cls).add_options(parser)

    def exec_locate(self):
        """Locate object"""
        client = UtilClient()
        try:
            hostname, _ = client.object_locate(
                self.params.get('oid'),
                self.params.get('uuid'),
                self.params.get('version'),
                self.params.get('focus_host'),
                self.params.get('copy_name'))

            print(hostname)
        except EnvironmentError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))


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

class StoreRenameOptHandler(BaseResourceOptHandler):
    """Rename object handler"""
    label = "rename"
    descr = 'Change the oid of an object generation, '\
            'among living or deprecated objects tables.'

    @classmethod
    def add_options(cls, parser):
        """Add command options for object rename."""
        RenameOptHandler(cls).add_options(parser)

    def exec_rename(self):
        """Rename object"""
        client = UtilClient()
        old_oid = self.params.get('oid')
        uuid = self.params.get('uuid')
        new_oid = self.params.get('new_oid')

        try:
            client.object_rename(old_oid, uuid, new_oid)
        except EnvironmentError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))


class StoreUndeleteOptHandler(BaseResourceOptHandler):
    """Undelete objects handler."""
    label = 'undelete'
    alias = ['undel']
    descr = 'Move back deprecated objects into phobos namespace'

    @classmethod
    def add_options(cls, parser):
        """Add command options for undelete object."""
        UndeleteOptHandler(cls).add_options(parser)

    def exec_undelete(self):
        """Undelete objetc"""
        client = UtilClient()
        oids = self.params.get('oids')
        uuid = self.params.get('uuid')

        if len(oids) > 1 and uuid is not None:
            self.logger.error("Only one oid can be provided with the --uuid "
                              "option")
            sys.exit(os.EX_USAGE)

        try:
            client.object_undelete(oids, uuid)
        except EnvironmentError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))
