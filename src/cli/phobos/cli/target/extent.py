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
Extent target for Phobos CLI
"""

import sys

from phobos.cli.action.list import ListOptHandler
from phobos.cli.common import BaseResourceOptHandler, env_error_format
from phobos.cli.common.args import add_list_arguments
from phobos.cli.common.utils import (check_output_attributes,
                                     handle_sort_option)
from phobos.core.admin import Client as AdminClient
from phobos.core.const import (extent_state2str, PHO_EXT_ST_ORPHAN,  # pylint: disable=no-name-in-module
                               PHO_EXT_ST_PENDING, PHO_EXT_ST_SYNC,
                               str2extent_state)
from phobos.core.ffi import LayoutInfo, ExtentInfo
from phobos.output import dump_object_list

class ExtentListOptHandler(ListOptHandler):
    """
    Specific version of the 'list' command for extent, with a couple
    extra-options.
    """
    descr = "list all extents"

    @classmethod
    def add_options(cls, parser):
        """Add extent-specific options."""
        super(ExtentListOptHandler, cls).add_options(parser)

        attr = list(LayoutInfo().get_display_dict().keys())
        attr.sort()

        add_list_arguments(parser, attr, "oid", sort_option=True,
                           lib_option=True, status_option=False)
        parser.add_argument('-c', '--copy-name',
                            help="Copy's name of the extents to list")
        parser.add_argument('-n', '--name',
                            help="filter on one medium name")
        parser.add_argument('--degroup', action='store_true',
                            help="used to list by extent, not by object")
        parser.add_argument('-p', '--pattern', action='store_true',
                            help="filter using POSIX regexp instead of "
                                 "exact extent")
        attr = list(ExtentInfo().get_display_dict().keys())
        attr.sort()
        parser.add_argument('-s', '--state',
                            help=("filter on extent state. If the state is " +
                                  extent_state2str(PHO_EXT_ST_ORPHAN) + " only "
                                  "these attributes are available: {" +
                                  " ".join(attr) + "}"),
                            choices=[extent_state2str(PHO_EXT_ST_PENDING),
                                     extent_state2str(PHO_EXT_ST_SYNC),
                                     extent_state2str(PHO_EXT_ST_ORPHAN)])


class ExtentOptHandler(BaseResourceOptHandler):
    """Shared interface for extents."""
    label = 'extent'
    descr = 'handle extents'
    verbs = [
        ExtentListOptHandler
    ]

    def exec_list(self):
        """List extents."""
        attrs = list(LayoutInfo().get_display_dict().keys())
        check_output_attributes(attrs, self.params.get('output'), self.logger)

        kwargs = {}
        if self.params.get('library'):
            kwargs['library'] = self.params.get('library')

        if self.params.get('copy_name'):
            kwargs['copy_name'] = self.params.get('copy_name')

        kwargs['state'] = str2extent_state(self.params.get('state') \
                                           if self.params.get('state') else '')

        kwargs = handle_sort_option(
                        self.params,
                        ExtentInfo() if kwargs['state'] == PHO_EXT_ST_ORPHAN \
                        else LayoutInfo(),
                        self.logger,
                        **kwargs)

        try:
            with AdminClient(lrs_required=False) as adm:
                if kwargs['state'] == PHO_EXT_ST_ORPHAN:
                    obj_list, p_objs, n_objs = adm.extent_list(
                                                    self.params.get('name'),
                                                    **kwargs)
                else:
                    obj_list, p_objs, n_objs = adm.layout_list(
                        self.params.get('res'),
                        self.params.get('pattern'),
                        self.params.get('name'),
                        self.params.get('degroup'),
                        **kwargs)

                if len(obj_list) > 0:
                    dump_object_list(obj_list, attr=self.params.get('output'),
                                     fmt=self.params.get('format'))

                adm.dss_res_free(p_objs, n_objs)

        except EnvironmentError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))
