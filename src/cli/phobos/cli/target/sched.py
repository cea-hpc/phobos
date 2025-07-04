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
Sched target for Phobos CLI
"""

import sys

from phobos.cli.action.fair_share import FairShareOptHandler
from phobos.cli.common import BaseResourceOptHandler, env_error_format
from phobos.cli.common.utils import ConfigItem
from phobos.core.admin import Client as AdminClient

class SchedOptHandler(BaseResourceOptHandler):
    """Handler of sched commands"""
    label = "sched"
    descr = "update configuration elements of the local scheduler"
    verbs = [
        FairShareOptHandler
    ]

    def exec_fair_share(self):
        """Run the fair share command"""
        _type = self.params.get("type")
        mins = self.params.get("min")
        maxs = self.params.get("max")

        config_items = []

        if mins:
            config_items.append(ConfigItem("io_sched_tape",
                                           "fair_share_" + _type + "_min",
                                           mins))
        if maxs:
            config_items.append(ConfigItem("io_sched_tape",
                                           "fair_share_" + _type + "_max",
                                           maxs))

        try:
            with AdminClient(lrs_required=True) as adm:
                if not mins and not maxs:
                    config_items.append(
                        ConfigItem("io_sched_tape",
                                   "fair_share_" + _type + "_min",
                                   None))
                    config_items.append(
                        ConfigItem("io_sched_tape",
                                   "fair_share_" + _type + "_max",
                                   None))
                    adm.sched_conf_get(config_items)
                else:
                    adm.sched_conf_set(config_items)
        except EnvironmentError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))
