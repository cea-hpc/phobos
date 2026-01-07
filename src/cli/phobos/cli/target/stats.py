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
Stats target for Phobos CLI
"""

import sys
import phobos.core.cfg as cfg

from phobos.cli.common import (BaseOptHandler, env_error_format)
from phobos.core.admin import Client as AdminClient
from phobos.core.ffi import ResourceFamily

class StatsOptHandler(BaseOptHandler):
    """Stats handler."""

    label = 'stats'
    descr = 'Query and display stats from Phobos daemons'
    family = None
    library = None

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        pass

    @classmethod
    def add_options(cls, parser):
        """Add command options."""
        super(StatsOptHandler, cls).add_options(parser)
        parser.set_defaults(verb=cls.label)
        parser.add_argument('namespace_name', nargs='?', default=None,
                            help='Optional namespace or namespace.name filter')
        parser.add_argument('--tags',
                            help='Optional filter on tags '
                            '(format: "key1=value1,key2=value2...")')
        parser.add_argument('--format', choices=['lines', 'json'],
                            default='lines',
                            help='Output format: lines or json')
        parser.add_argument('--tlc', nargs='?', const='default_tlc',
                            default=None, help='Optional TLC name')

    def exec_stats(self):
        """Stats call"""
        try:
            tlc = self.params.get('tlc')
            with AdminClient(lrs_required=(tlc is None)) \
                as adm:
                if tlc is not None:
                    self.family = ResourceFamily(ResourceFamily.RSC_TAPE)
                    self.library = cfg.get_default_library(self.family) \
                        if tlc == 'default_tlc' else tlc
                    json = adm.tlc_stats(self.library,
                                         self.params.get('namespace_name'),
                                         self.params.get('tags'))
                else:
                    json = adm.stats(self.params.get('namespace_name'),
                                     self.params.get('tags'))

        except EnvironmentError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))

        if self.params.get('format') == 'json':
            print(json)
        else:
            for entry in json:
                tags = entry.get('tags', '')
                name = entry['name']
                value = entry['value']
                print(f"{tags} {name}={value}")
