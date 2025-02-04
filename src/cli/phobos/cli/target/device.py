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
Device target for Phobos CLI
"""

import sys

from phobos.cli.action.list import ListOptHandler
from phobos.cli.action.lock import LockOptHandler
from phobos.cli.action.resource_delete import ResourceDeleteOptHandler
from phobos.cli.action.unlock import UnlockOptHandler
from phobos.cli.common import (BaseResourceOptHandler, env_error_format)
from phobos.cli.common.utils import set_library
from phobos.core.admin import Client as AdminClient

class DeviceLockOptHandler(LockOptHandler):
    """Lock device."""

    @classmethod
    def add_options(cls, parser):
        """Add resource-specific options."""
        super(DeviceLockOptHandler, cls).add_options(parser)
        parser.add_argument('--wait', action='store_true',
                            help='wait for any deamon releasing the device')


class DeviceOptHandler(BaseResourceOptHandler):
    """Shared interface for devices."""
    verbs = [
        DeviceLockOptHandler,
        ListOptHandler,
        ResourceDeleteOptHandler,
        UnlockOptHandler
    ]
    library = None

    def filter(self, ident, **kwargs):
        """
        Return a list of devices that match the identifier for either serial or
        path. You may call it a bug but this is a feature intended to let admins
        transparently address devices using one or the other scheme.
        """
        dev = self.client.devices.get(family=self.family, serial=ident,
                                      **kwargs)
        if not dev:
            # 2nd attempt, by path...
            dev = self.client.devices.get(family=self.family, path=ident,
                                          **kwargs)
        return dev

    def exec_add(self):
        """Add a new device"""
        resources = self.params.get('res')
        set_library(self)

        try:
            with AdminClient(lrs_required=False) as adm:
                adm.device_add(self.family, resources,
                               not self.params.get('unlock'), self.library)

        except EnvironmentError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))

        self.logger.info("Added %d device(s) successfully", len(resources))

    def exec_lock(self):
        """Device lock"""
        names = self.params.get('res')
        set_library(self)

        try:
            with AdminClient(lrs_required=False) as adm:
                adm.device_lock(self.family, names, self.library,
                                self.params.get('wait'))

        except EnvironmentError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))

        self.logger.info("%d device(s) locked", len(names))

    def exec_unlock(self):
        """Device unlock"""
        names = self.params.get('res')
        set_library(self)

        try:
            with AdminClient(lrs_required=False) as adm:
                adm.device_unlock(self.family, names, self.library,
                                  self.params.get('force'))

        except EnvironmentError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))

        self.logger.info("%d device(s) unlocked", len(names))
