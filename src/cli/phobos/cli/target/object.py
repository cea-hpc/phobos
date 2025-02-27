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
Object target for Phobos CLI
"""

import os
import sys

from phobos.cli.action.list import ListOptHandler
from phobos.cli.common import BaseResourceOptHandler, env_error_format
from phobos.cli.common.args import add_list_arguments
from phobos.cli.common.utils import (check_max_width_is_valid,
                                     check_output_attributes,
                                     handle_sort_option)
from phobos.core.const import DSS_OBJ_ALIVE, DSS_OBJ_DEPRECATED_ONLY # pylint: disable=no-name-in-module
from phobos.core.ffi import DeprecatedObjectInfo, ObjectInfo
from phobos.core.store import UtilClient
from phobos.output import dump_object_list


class ObjectListOptHandler(ListOptHandler):
    """
    Specific version of the 'list' command for object, with a couple
    extra-options.
    """
    descr = 'list all objects'

    @classmethod
    def add_options(cls, parser):
        """Add object-specific options."""
        super(ObjectListOptHandler, cls).add_options(parser)

        base_attrs = list(ObjectInfo().get_display_dict().keys())
        base_attrs.sort()
        ext_attrs = list(DeprecatedObjectInfo().get_display_dict().keys() -
                         ObjectInfo().get_display_dict().keys())
        ext_attrs.sort()
        add_list_arguments(parser, base_attrs, "oid", sort_option=True,
                           lib_option=False, status_option=False)
        parser.add_argument('-d', '--deprecated', action='store_true',
                            help="print deprecated objects, allowing those "
                                 "attributes for the 'output' option "
                                 "{" + " ".join(ext_attrs) + "}")
        parser.add_argument('-m', '--metadata', type=lambda t: t.split(','),
                            help="filter items containing every given "
                                 "metadata, comma-separated "
                                 "'key=value' parameters")
        parser.add_argument('-p', '--pattern', action='store_true',
                            help="filter using POSIX regexp instead of exact "
                                 "objid")
        parser.add_argument('-t', '--no-trunc', action='store_true',
                            help="do not truncate the user_md column (takes "
                                 "precedency over the 'max-width' argument)")
        parser.add_argument('-w', '--max-width', default=30,
                            type=check_max_width_is_valid,
                            help="max width of the user_md column keys and "
                                 "values, must be greater or equal to 5"
                                 "(default is 30)")


class ObjectOptHandler(BaseResourceOptHandler):
    """Shared interface for objects."""
    label = 'object'
    descr = 'handle objects'
    verbs = [
        ObjectListOptHandler,
    ]

    def exec_list(self):
        """List objects."""
        attrs = list(DeprecatedObjectInfo().get_display_dict().keys()
                     if self.params.get('deprecated')
                     else ObjectInfo().get_display_dict().keys())
        check_output_attributes(attrs, self.params.get('output'), self.logger)

        metadata = []
        if self.params.get('metadata'):
            metadata = self.params.get('metadata')
            for elt in metadata:
                if '=' not in elt:
                    self.logger.error("Metadata parameter '%s' must be a "
                                      "'key=value'", elt)
                    sys.exit(os.EX_USAGE)

        kwargs = {}
        if self.params.get('deprecated'):
            kwargs = handle_sort_option(self.params, DeprecatedObjectInfo(),
                                        self.logger, **kwargs)
        else:
            kwargs = handle_sort_option(self.params, ObjectInfo(), self.logger,
                                        **kwargs)

        client = UtilClient()

        scope = DSS_OBJ_DEPRECATED_ONLY if self.params.get('deprecated') \
                else DSS_OBJ_ALIVE

        try:
            objs = client.object_list(self.params.get('res'),
                                      self.params.get('pattern'),
                                      metadata, scope,
                                      **kwargs)

            if objs:
                max_width = (None if self.params.get('no_trunc')
                             else self.params.get('max_width'))

                dump_object_list(objs, attr=self.params.get('output'),
                                 max_width=max_width,
                                 fmt=self.params.get('format'))

            client.list_obj_free(objs, len(objs))
        except EnvironmentError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))
