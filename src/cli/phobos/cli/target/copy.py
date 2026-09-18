#
#  All rights reserved (c) 2014-2026 CEA/DAM.
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

from argparse import ArgumentTypeError
import os
import sys
from typing import Optional

from phobos.cli.action.create import CreateOptHandler
from phobos.cli.action.delete import DeleteOptHandler
from phobos.cli.action.list import ListOptHandler
from phobos.cli.action.rebuild import RebuildOptHandler
from phobos.cli.common import env_error_format, XferOptHandler
from phobos.cli.common.args import add_put_arguments, add_object_arguments
from phobos.cli.common.utils import (check_output_attributes, create_put_params,
                                     get_params_status, get_scope,
                                     positive_int_list)
from phobos.core.const import DSS_OBJ_ALIVE #pylint: disable=no-name-in-module
from phobos.core.ffi import CopyInfo
from phobos.core.store import (DelParams, GetParams, UtilClient, XferPutParams,
                               XferGetParams, XferCopyParams, XferRebuildParams)
from phobos.output import dump_object_list


class CopyCreateOptHandler(CreateOptHandler):
    """Option handler for create action of copy target"""
    descr = 'create copy of object'

    @classmethod
    def add_options(cls, parser):
        super(CopyCreateOptHandler, cls).add_options(parser)
        parser.add_argument('oid', help='targeted object')
        parser.add_argument('copy', help='name of the new copy to create')
        parser.add_argument('-c', '--copy-name',
                            help='name of the copy to read from')
        add_put_arguments(parser)

class CopyDeleteOptHandler(DeleteOptHandler):
    """Option handler for delete action of copy target"""
    descr = 'delete copy of object'

    @classmethod
    def add_options(cls, parser):
        super(CopyDeleteOptHandler, cls).add_options(parser)
        parser.add_argument('oid', nargs='?',
                            help="targeted object, mandatory unless "
                                 "--all-incomplete is set")
        parser.add_argument('copy', nargs='?',
                            help="copy name, mandatory unless "
                                 "--all-incomplete is set")
        all_incomplete_group = parser.add_mutually_exclusive_group()
        all_incomplete_group.add_argument("--all-incomplete",
                                          action="store_true",
                                          help="Delete all incomplete copies "
                                               "that could be located on this "
                                               "node. If set, all other "
                                               "options are ignored.")
        all_incomplete_group.add_argument("--all-incomplete-dry-run",
                                          action="store_true",
                                          help="Logs delete actions of all "
                                               "incomplete copies that could "
                                               "be located on this node. No "
                                               "delete is done. If set, all "
                                               "other options are ignored.")
        parser.add_argument('--all-incomplete-delay-second',
                            type=lambda x: int(x) if int(x) >= 0 else
                                     ArgumentTypeError(
                                         "--all-incomplete-delay-second "
                                         "must be a positive integer"),
                            help="(only taken into account if --all-incomplete "
                                 "or --all-incomplete-dry-run option are set) "
                                 "Delay in second before an incomplete copy "
                                 "can be cleaned. If not set, the config "
                                 "\"delete_incomplete_delay_second\" parameter "
                                 "value is used.")

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


class CopyRebuildHandler(RebuildOptHandler):
    """Option handler for copy rebuild"""
    descr = 'rebuild a copy'

    @classmethod
    def add_options(cls, parser):
        super(CopyRebuildHandler, cls).add_options(parser)

        parser.add_argument('oid', help='targeted object')
        parser.add_argument('copy', help='name of the copy to rebuild')
        parser.add_argument('-e', '--extent-idx',
                            type=positive_int_list,
                            help='comma-separated list of the indexes of the '
                                 'extents to rebuild (e.g. 0,2). If not set, '
                                 'all the missing extents of the copy are '
                                 'rebuilt')
        add_object_arguments(parser)


class CopyOptHandler(XferOptHandler):
    """Option handler for copy target"""
    label = 'copy'
    descr = 'manage copies of objects'
    verbs = [
        CopyCreateOptHandler,
        CopyDeleteOptHandler,
        CopyListOptHandler,
        CopyRebuildHandler,
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

    def delete_incomplete(self, dry_run, delay_second: Optional[int] = None):
        """Incomplete copies deletion"""
        client = UtilClient()

        try:
            client.delete_incomplete_copy(dry_run, delay_second)
        except EnvironmentError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))

    def exec_delete(self):
        """Copy deletion"""
        if self.params.get('all_incomplete'):
            self.delete_incomplete(
               False,
               self.params.get('all_incomplete_delay_second'))
            return

        if self.params.get('all_incomplete_dry_run'):
            self.delete_incomplete(
                True,
                self.params.get('all_incomplete_delay_second'))
            return

        client = UtilClient()

        copy = self.params.get('copy')
        oid = self.params.get('oid')

        if not oid or not copy:
            self.logger.error("oid and copy are mandatory, "
                              "unless --all-incomplete is set")
            sys.exit(os.EX_USAGE)

        deprec = self.params.get('deprecated')
        deprec_only = self.params.get('deprecated_only')
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

        if len(oids) > 1 and uuid is not None:
            self.logger.error("The --uuid option is not compatible with more "
                              "than one oid")
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

    def exec_rebuild(self):
        """Copy rebuild"""
        oid = self.params.get('oid')
        copy = self.params.get('copy')
        deprec = self.params.get('deprecated')
        deprec_only = self.params.get('deprecated_only')
        extents_idx = self.params.get('extent_idx')
        uuid = self.params.get('uuid')
        version = self.params.get('version')

        scope = get_scope(deprec, deprec_only)

        client = UtilClient()

        put_params = XferPutParams(create_put_params(self, copy))
        get_params = XferGetParams(GetParams(copy_name=copy,
                                             node_name=None,
                                             scope=scope))
        params = XferRebuildParams(get_params, put_params, extents_idx)

        try:
            client.copy_rebuild(oid, uuid, version, params)
        except EnvironmentError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))

        self.logger.info("Copy '%s' successfully rebuilded", copy)
