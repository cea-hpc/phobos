#!/usr/bin/env python3

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
Phobos command-line interface utilities.

Phobos action handlers (AH). AHs are descriptors of the objects that phobos can
manipulate. They expose both command line subparsers, to retrieve and validate
specific command line parameters and the API entry points for phobos to trigger
actions.
"""

import sys

import os

from phobos.core.admin import Client as AdminClient
from phobos.core.store import (attrs_as_dict, DelParams, UtilClient)
from phobos.cli.common import (BaseOptHandler, PhobosActionContext,
                               env_error_format, XferOptHandler)
from phobos.cli.common.args import add_put_arguments, add_object_arguments
from phobos.cli.common.utils import (attr_convert, create_put_params,
                                     set_library, get_scope)
from phobos.cli.target.copy import CopyOptHandler
from phobos.cli.target.dir import DirOptHandler
from phobos.cli.target.drive import DriveOptHandler
from phobos.cli.target.extent import ExtentOptHandler
from phobos.cli.target.lib import LibOptHandler
from phobos.cli.target.lock import LocksOptHandler
from phobos.cli.target.logs import LogsOptHandler
from phobos.cli.target.object import ObjectOptHandler
from phobos.cli.target.phobosd import PhobosdOptHandler
from phobos.cli.target.rados import RadosPoolOptHandler
from phobos.cli.target.store import (StoreGetOptHandler, StoreGetMDOptHandler,
                                     StoreMPutOptHandler, StorePutOptHandler)
from phobos.cli.target.tape import TapeOptHandler
from phobos.cli.target.tlc import TLCOptHandler


class DeleteOptHandler(BaseOptHandler):
    """Delete objects handler."""

    label = 'delete'
    alias = ['del']
    descr = 'remove an object from phobos namespace and '\
            'add it to deprecated objects'

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        pass

    @classmethod
    def add_options(cls, parser):
        """Add command options."""
        super(DeleteOptHandler, cls).add_options(parser)

        parser.add_argument('oids', nargs='+',
                            help='Object IDs to delete')
        parser.add_argument('--hard', action='store_true',
                            help='Require a hardware remove of the object')
        add_object_arguments(parser)
        parser.set_defaults(verb=cls.label)

    def exec_delete(self):
        """Delete objects."""
        client = UtilClient()

        deprec = self.params.get('deprecated')
        deprec_only = self.params.get('deprecated_only')
        oids = self.params.get('oids')
        uuid = self.params.get('uuid')
        version = self.params.get('version')

        if not self.params.get('hard') and (deprec or deprec_only):
            self.logger.error("--deprecated or --deprecated-only can only be "
                              "used with the --hard option")
            sys.exit(os.EX_USAGE)

        if len(oids) > 1 and (uuid is not None or version is not None):
            self.logger.error("Only one oid can be provided with the --uuid or "
                              "--version option")
            sys.exit(os.EX_USAGE)

        scope = get_scope(deprec, deprec_only)

        try:
            client.object_delete(oids, uuid, version,
                                 DelParams(copy_name=None, scope=scope),
                                 self.params.get('hard'))
        except EnvironmentError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))


class UndeleteOptHandler(BaseOptHandler):
    """Undelete objects handler."""

    label = 'undelete'
    alias = ['undel']
    descr = 'Move back deprecated objects into phobos namespace'

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        pass

    @classmethod
    def add_options(cls, parser):
        """Add command options for undelete object."""
        super(UndeleteOptHandler, cls).add_options(parser)
        parser.set_defaults(verb=cls.label)
        parser.add_argument('oids', nargs='+', help='Object OIDs to undelete')
        parser.add_argument('--uuid', help='UUID of the object')

    def exec_undelete(self):
        """Undelete objetc"""
        client = UtilClient()
        oids = self.params.get('oids')
        uuid = self.params.get('uuid')

        if len(oids) > 1 and uuid is not None:
            self.logger.error("Only one oid can be provided with the --uuid "
                              "option")
            sys.exit(os.EX_USAGE)

        try:
            client.object_undelete(oids, uuid)
        except EnvironmentError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))


class RenameOptHandler(BaseOptHandler):
    """Rename object handler"""

    label = 'rename'
    descr = 'Change the oid of an object generation, '\
            'among living or deprecated objects tables.'

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        pass

    @classmethod
    def add_options(cls, parser):
        """Add command options for object rename."""
        super(RenameOptHandler, cls).add_options(parser)
        parser.set_defaults(verb=cls.label)
        parser.add_argument('oid', help='Object ID to be renamed')
        parser.add_argument('new_oid', help='New name for the object')
        parser.add_argument('--uuid', help='UUID of the object to rename')

    def exec_rename(self):
        """Rename object"""
        client = UtilClient()
        old_oid = self.params.get('oid')
        uuid = self.params.get('uuid')
        new_oid = self.params.get('new_oid')

        try:
            client.object_rename(old_oid, uuid, new_oid)
        except EnvironmentError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))

class LocateOptHandler(BaseOptHandler):
    """Locate object handler."""

    label = 'locate'
    descr = 'Find the hostname which has the best access to an object if any'

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        pass

    @classmethod
    def add_options(cls, parser):
        """Add command options."""
        super(LocateOptHandler, cls).add_options(parser)
        parser.set_defaults(verb=cls.label)
        parser.add_argument('oid', help='Object ID to locate')
        parser.add_argument('--uuid', help='UUID of the object')
        parser.add_argument('--version', help='Version of the object',
                            type=int, default=0)
        parser.add_argument('--focus-host',
                            help='Suggested hostname for early locking')
        parser.add_argument('-c', '--copy-name',
                            help='Copy of the object to locate')


    def exec_locate(self):
        """Locate object"""
        client = UtilClient()
        try:
            hostname, _ = client.object_locate(
                self.params.get('oid'),
                self.params.get('uuid'),
                self.params.get('version'),
                self.params.get('focus_host'),
                self.params.get('copy_name'))

            print(hostname)
        except EnvironmentError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))


class FairShareOptHandler(BaseOptHandler):
    """Handler for fair_share configuration parameters"""
    label = "fair_share"
    descr = "handler for fair_share configuration parameters"
    epilog = """If neither --min nor --max are set, this command will query the
    configuration information from the local daemon and output them to
    stdout."""

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        pass

    @classmethod
    def add_options(cls, parser):
        super(FairShareOptHandler, cls).add_options(parser)
        parser.add_argument('-t', '--type', required=True,
                            help="tape technology")
        parser.add_argument('--min',
                            help="of the form r,w,f where r, w and f are "
                            "integers representing the minimum number of "
                            "devices that ought to be allocated to the read, "
                            "write and format schedulers respectively")
        parser.add_argument('--max',
                            help="of the form r,w,f where r, w and f are "
                            "integers representing the maximum number of "
                            "devices that ought to be allocated to the read, "
                            "write and format schedulers respectively")

class ConfigItem:
    """Element in Phobos' configuration"""

    def __init__(self, section, key, value):
        self.section = section
        self.key = key
        self.value = value


class SchedOptHandler(BaseOptHandler):
    """Handler of sched commands"""
    label = "sched"
    descr = "update configuration elements of the local scheduler"
    verbs = [
        FairShareOptHandler
    ]

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        pass

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


HANDLERS = [
    # Resource interfaces
    CopyOptHandler,
    DeleteOptHandler,
    DirOptHandler,
    DriveOptHandler,
    ExtentOptHandler,
    LibOptHandler,
    LocateOptHandler,
    LocksOptHandler,
    LogsOptHandler,
    ObjectOptHandler,
    PhobosdOptHandler,
    RadosPoolOptHandler,
    RenameOptHandler,
    SchedOptHandler,
    TapeOptHandler,
    TLCOptHandler,
    UndeleteOptHandler,

    # Store command interfaces
    StoreGetOptHandler,
    StoreGetMDOptHandler,
    StoreMPutOptHandler,
    StorePutOptHandler,
]

def phobos_main(args=None):
    """
    Entry point for the `phobos' command. Indirectly provides
    argument parsing and execution of the appropriate actions.
    """
    with PhobosActionContext(HANDLERS,
                             args if args is not None else sys.argv[1::]
                            ) as pac:
        pac.run()
