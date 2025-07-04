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
Copy target for Phobos CLI
"""

import os
import sys

from phobos.cli.action.create import CreateOptHandler
from phobos.cli.action.delete import DeleteOptHandler
from phobos.cli.action.list import ListOptHandler
from phobos.cli.common import env_error_format, XferOptHandler
from phobos.cli.common.args import add_put_arguments, add_object_arguments
from phobos.cli.common.utils import (check_output_attributes, create_put_params,
                                     get_params_status, get_scope)
from phobos.core.const import DSS_OBJ_ALIVE #pylint: disable=no-name-in-module
from phobos.core.ffi import CopyInfo
from phobos.core.store import (DelParams, GetParams, UtilClient, XferPutParams,
                               XferGetParams, XferCopyParams)
from phobos.output import dump_object_list


class CopyCreateOptHandler(CreateOptHandler):
    """Option handler for create action of copy target"""
    descr = 'create copy of object'

    @classmethod
    def add_options(cls, parser):
        super(CopyCreateOptHandler, cls).add_options(parser)
        parser.add_argument('oid', help='targeted object')
        parser.add_argument('copy', help='copy name')
        parser.add_argument('-c', '--copy-name',
                            help='Copy name of the object to copy')
        add_put_arguments(parser)


class CopyDeleteOptHandler(DeleteOptHandler):
    """Option handler for delete action of copy target"""
    descr = 'delete copy of object'

    @classmethod
    def add_options(cls, parser):
        super(CopyDeleteOptHandler, cls).add_options(parser)
        parser.add_argument('oid', help='targeted object')
        parser.add_argument('copy', help='copy name')


class CopyListOptHandler(ListOptHandler):
    """Option handler for list action of copy target"""
    descr = 'list copies of objects'

    @classmethod
    def add_options(cls, parser):
        super(CopyListOptHandler, cls).add_options(parser)

        attrs = list(CopyInfo().get_display_dict().keys())
        attrs.sort()

        add_object_arguments(parser)
        parser.add_argument('-c', '--copy-name', help='copy name')
        parser.add_argument('-o', '--output', type=lambda t: t.split(','),
                            default='copy_name',
                            help=("attributes to output, comma-separated, "
                                  "choose from {" + " ".join(attrs) + "} "
                                  "default: %(default)s"))
        parser.add_argument('-s', '--status', action='store',
                            help="filter copies according to their "
                                 "copy_status, choose one or multiple letters "
                                 "from {i r c} for respectively: incomplete, "
                                 "readable and complete")
        parser.epilog = """About the status of the copy:
        incomplete: the copy cannot be rebuilt because it lacks some of its
                    extents,
        readable:   the copy can be rebuilt, however some of its extents were
                    not found,
        complete:   the copy is complete."""


class CopyOptHandler(XferOptHandler):
    """Option handler for copy target"""
    label = 'copy'
    descr = 'manage copies of objects'
    verbs = [
        CopyCreateOptHandler,
        CopyDeleteOptHandler,
        CopyListOptHandler,
    ]

    def exec_create(self):
        """Copy creation"""
        copy_to_get = self.params.get('copy_name')
        copy_to_put = self.params.get('copy')
        oid = self.params.get('oid')

        put_params = XferPutParams(create_put_params(self, copy_to_put))
        get_params = XferGetParams(GetParams(copy_name=copy_to_get,
                                             node_name=None,
                                             scope=DSS_OBJ_ALIVE))
        copy_params = XferCopyParams(get_params, put_params)

        self.client.copy_register(oid, copy_params)
        try:
            self.client.run()

        except IOError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))

        self.logger.info("Object '%s' successfully copied as '%s'", oid,
                         copy_to_put)

    def exec_delete(self):
        """Copy deletion"""
        client = UtilClient()

        copy = self.params.get('copy')
        deprec = self.params.get('deprecated')
        deprec_only = self.params.get('deprecated_only')
        oid = self.params.get('oid')
        uuid = self.params.get('uuid')
        version = self.params.get('version')

        scope = get_scope(deprec, deprec_only)

        try:
            client.copy_delete(oid, uuid, version,
                               DelParams(copy_name=copy, scope=scope))
        except EnvironmentError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))

    def exec_list(self):
        """Copy listing"""
        attrs = list(CopyInfo().get_display_dict().keys())
        check_output_attributes(attrs, self.params.get('output'), self.logger)

        copy = self.params.get('copy_name')
        deprecated = self.params.get('deprecated')
        deprecated_only = self.params.get('deprecated_only')
        oids = self.params.get('res')
        status_number = get_params_status(self.params.get('status'),
                                          self.logger)
        uuid = self.params.get('uuid')
        version = self.params.get('version')

        if len(oids) != 1 and uuid is not None:
            self.logger.error("only one oid can be provided with the --uuid "
                              "option")
            sys.exit(os.EX_USAGE)

        client = UtilClient()

        scope = get_scope(deprecated, deprecated_only)

        try:
            copies = client.copy_list(oids, uuid, version, copy, scope,
                                      status_number)

            if copies:
                dump_object_list(copies, attr=self.params.get('output'),
                                 fmt=self.params.get('format'))
            client.list_cpy_free(copies, len(copies))

        except EnvironmentError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))
