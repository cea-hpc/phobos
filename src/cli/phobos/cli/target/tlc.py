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
TLC target for Phobos CLI
"""

import sys

from phobos.cli.action.ping import PingOptHandler
from phobos.cli.common import BaseResourceOptHandler, env_error_format
from phobos.cli.common.utils import set_library
from phobos.core.admin import Client as AdminClient
from phobos.core.ffi import ResourceFamily

class TLCPingOptHandler(PingOptHandler):
    """TLC ping"""
    label = 'ping'
    descr = 'ping the Tape Library Controler'

    @classmethod
    def add_options(cls, parser):
        """Add resource-specific options."""
        super(TLCPingOptHandler, cls).add_options(parser)
        parser.add_argument('--library', help="Library of the TLC to ping")


class TLCOptHandler(BaseResourceOptHandler):
    """TLC options and actions."""
    label = 'tlc'
    descr = 'handle TLC'
    verbs = [
        TLCPingOptHandler,
    ]
    family = None
    library = None

    def exec_ping(self):
        """Ping the TLC daemon to check if it is online."""
        self.family = ResourceFamily(ResourceFamily.RSC_TAPE)
        set_library(self)

        try:
            with AdminClient(lrs_required=False) as adm:
                adm.ping_tlc(self.library)

        except EnvironmentError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))

        self.logger.info("Ping sent to TLC %s successfully", self.library)
