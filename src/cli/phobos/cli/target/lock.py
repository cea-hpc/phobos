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
Lock target for Phobos CLI
"""

import sys

from phobos.cli.action.clean import CleanOptHandler
from phobos.cli.common import BaseResourceOptHandler, env_error_format
from phobos.core.admin import Client as AdminClient

class LocksOptHandler(BaseResourceOptHandler):
    """Locks table actions and options"""
    label = 'lock'
    descr = 'handle lock table'
    verbs = [CleanOptHandler]

    def exec_clean(self):
        """Release locks with given arguments"""
        try:
            with AdminClient(lrs_required=False) as adm:
                adm.clean_locks(self.params.get('global'),
                                self.params.get('force'),
                                self.params.get('type'),
                                self.params.get('family'),
                                self.params.get('ids'))

        except EnvironmentError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))

        self.logger.info("Clean command executed successfully")
