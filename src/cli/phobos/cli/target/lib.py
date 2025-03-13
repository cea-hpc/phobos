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
Lib target for Phobos CLI
"""

import sys

import phobos.core.cfg as cfg

from phobos.cli.action import ActionOptHandler
from phobos.cli.common import BaseResourceOptHandler
from phobos.core.admin import Client as AdminClient
from phobos.core.const import PHO_LIB_SCSI # pylint: disable=no-name-in-module
from phobos.core.ffi import ResourceFamily

class LibRefreshOptHandler(ActionOptHandler):
    """Refresh the library internal cache"""
    label = 'refresh'
    descr = 'Refresh the library internal cache'

    @classmethod
    def add_options(cls, parser):
        """Add resource-specific options."""
        super(LibRefreshOptHandler, cls).add_options(parser)
        parser.add_argument('res', nargs='*',
                            help="Library(ies) to refresh. If not given, will "
                                 "fetch instead the default tape library from "
                                 "configuration")


class LibScanOptHandler(ActionOptHandler):
    """Scan a library and display retrieved information."""
    label = 'scan'
    descr = 'Display the status of the library'

    @classmethod
    def add_options(cls, parser):
        """Add resource-specific options."""
        super(LibScanOptHandler, cls).add_options(parser)
        parser.add_argument('res', nargs='*',
                            help="Library(ies) to scan. If not given, will "
                                 "fetch instead the default tape library from "
                                 "configuration")
        parser.add_argument('--refresh', action='store_true',
                            help="The library module refreshes its internal "
                                 "cache before sending the data")


class LibOptHandler(BaseResourceOptHandler):
    """Tape library options and actions."""
    label = 'lib'
    descr = 'handle tape libraries'
    # for now, this class in only used for tape library actions, but in case
    # it is used for other families, set family as None
    family = ResourceFamily(ResourceFamily.RSC_TAPE)
    verbs = [
        LibScanOptHandler,
        LibRefreshOptHandler,
    ]

    def exec_scan(self):
        """Scan this lib and display the result"""
        libs = self.params.get("res")
        if not libs:
            libs = [cfg.get_default_library(ResourceFamily.RSC_TAPE)]

        with_refresh = self.params.get('refresh')
        try:
            with AdminClient(lrs_required=False) as adm:
                for lib in libs:
                    lib_data = adm.lib_scan(PHO_LIB_SCSI, lib, with_refresh)
                    # FIXME: can't use dump_object_list yet as it does not play
                    # well with unstructured dict-like data (relies on getattr)
                    self._print_lib_data(lib_data)
        except EnvironmentError as err:
            self.logger.error("%s, will abort 'lib scan'", err)
            sys.exit(abs(err.errno))

    def exec_refresh(self):
        """Refresh the library internal cache"""
        libs = self.params.get("res")
        if not libs:
            libs = [cfg.get_default_library(ResourceFamily.RSC_TAPE)]

        try:
            with AdminClient(lrs_required=False) as adm:
                for lib in libs:
                    adm.lib_refresh(PHO_LIB_SCSI, lib)
        except EnvironmentError as err:
            self.logger.error("%s, will abort 'lib refresh'", err)
            sys.exit(abs(err.errno))

    @staticmethod
    def _print_lib_data(lib_data):
        """Print library data in a human readable format"""
        for elt in lib_data:
            done = set(["type"])
            line = []
            flags = []
            line.append("%s:" % (elt.get("type", "element"),))
            for key in "address", "source_address":
                if key in elt:
                    line.append("%s=%#x" % (key, elt[key]))
                    done.add(key)

            for key in sorted(elt):
                value = elt[key]
                if key in done:
                    continue
                if isinstance(value, bool):
                    if value:
                        # Flags are printed after the other properties
                        flags.append(key)
                else:
                    line.append("%s=%r" % (key, str(value)))
            line.extend(flags)
            print(" ".join(line))
