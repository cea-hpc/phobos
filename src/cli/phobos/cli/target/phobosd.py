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
Phobosd target for Phobos CLI
"""

import sys

from phobos.cli.action.ping import PingOptHandler
from phobos.cli.common import BaseResourceOptHandler, env_error_format
from phobos.core.admin import Client as AdminClient


class PhobosdOptHandler(BaseResourceOptHandler):
    """Phobosd options and actions."""
    label = 'phobosd'
    descr = 'handle phobosd'
    verbs = [
        PingOptHandler,
    ]
    family = None
    library = None

    def exec_ping(self):
        """Ping the phobosd to check if it is online."""
        try:
            with AdminClient(lrs_required=True) as adm:
                adm.ping_lrs()

        except EnvironmentError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))

        self.logger.info("Ping sent to phobosd successfully")
