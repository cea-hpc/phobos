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
Logs target for Phobos CLI
"""

import os
import sys

from phobos.cli.action.clear import ClearOptHandler
from phobos.cli.action.dump import DumpOptHandler
from phobos.cli.common import BaseResourceOptHandler, env_error_format
from phobos.cli.common.utils import create_log_filter, set_library
from phobos.core.admin import Client as AdminClient
from phobos.core.ffi import ResourceFamily

class LogsOptHandler(BaseResourceOptHandler):
    """Handler of logs commands"""
    label = "logs"
    descr = "interact with the persistent logs recorded by Phobos"
    verbs = [
        DumpOptHandler,
        ClearOptHandler
    ]
    family = ResourceFamily(ResourceFamily.RSC_TAPE)
    library = None

    def exec_dump(self):
        """Dump logs to stdout"""
        device = self.params.get('drive')
        medium = self.params.get('tape')
        _errno = self.params.get('errno')
        cause = self.params.get('cause')
        start = self.params.get('start')
        end = self.params.get('end')
        errors = self.params.get('errors')
        if (device or medium):
            #if we have a resource we need the library otion or the default
            set_library(self)
        else:
            #if we have no resource the library could be None if not set
            self.library = self.params.get('library')

        if _errno and errors:
            self.logger.error("only one of '--errors' or '--errno' must be "
                              "provided")
            sys.exit(os.EX_USAGE)

        log_filter = create_log_filter(self.library, device, medium, _errno,
                                       cause, start, end, errors)
        try:
            with AdminClient(lrs_required=False) as adm:
                adm.dump_logs(sys.stdout.fileno(), log_filter)

        except EnvironmentError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))

    def exec_clear(self):
        """Clear logs"""
        device = self.params.get('drive')
        medium = self.params.get('tape')
        _errno = self.params.get('errno')
        cause = self.params.get('cause')
        start = self.params.get('start')
        end = self.params.get('end')
        errors = self.params.get('errors')
        if (device or medium):
            #if we have a resource we need the library otion or the default
            set_library(self)
        else:
            #if we have no resource the library could be None if not set
            self.library = self.params.get('library')

        if _errno and errors:
            self.logger.error("only one of '--errors' or '--errno' must be "
                              "provided")
            sys.exit(os.EX_USAGE)

        log_filter = create_log_filter(self.library, device, medium, _errno,
                                       cause, start, end, errors)
        try:
            with AdminClient(lrs_required=False) as adm:
                adm.clear_logs(log_filter, self.params.get('clear_all'))

        except EnvironmentError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))
