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
Config target for Phobos CLI
"""

import errno
import sys

from phobos.cli.action import ActionOptHandler
from phobos.cli.common import BaseOptHandler, env_error_format
from phobos.core import cfg


class ConfigGetOptHandler(ActionOptHandler):
    """Get the value of a configuration parameter."""
    label = 'get'
    descr = 'get the value of a configuration parameter'

    @classmethod
    def add_options(cls, parser):
        """Add options to the config get command."""
        super(ConfigGetOptHandler, cls).add_options(parser)
        parser.add_argument('section',
                            help='name of the section of the parameter')
        parser.add_argument('param',
                            help='name of the parameter in the section')


class ConfigOptHandler(BaseOptHandler):
    """Handler of config commands."""
    label = 'config'
    descr = 'get values of the local configuration'
    verbs = [
        ConfigGetOptHandler
    ]

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        pass

    def exec_get(self):
        """Get the value of a configuration parameter."""
        section = self.params.get('section')
        param = self.params.get('param')
        try:
            value = cfg.get_val(section, param)
        except KeyError:
            self.logger.error("No value in cfg for section '%s', key '%s'",
                              section, param)
            sys.exit(errno.ENODATA)
        except EnvironmentError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))
        print(value)
