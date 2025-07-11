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

from shlex import shlex
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
from phobos.cli.target.store import (StoreGetOptHandler, StoreGetMDOptHandler)
from phobos.cli.target.tape import TapeOptHandler
from phobos.cli.target.tlc import TLCOptHandler


def mput_file_line_parser(line):
    """Convert a mput file line into the 3 values needed for each put."""
    line_parser = shlex(line, posix=True)
    line_parser.whitespace = ' '
    line_parser.whitespace_split = True

    file_entry = list(line_parser) # [src_file, oid, user_md]

    if len(file_entry) != 3:
        raise ValueError("expecting 3 elements (src_file, oid, metadata), got "
                         + str(len(file_entry)))

    return file_entry


class StorePutHandler(XferOptHandler):
    """Insert objects into backend."""
    label = 'put'
    descr = 'insert object into backend. Either "--file" or "src_file" and '\
            '"oid" must be provided.'

    @classmethod
    def add_options(cls, parser):
        """Add options for the PUT command."""
        super(StorePutHandler, cls).add_options(parser)
        # The type argument allows to transform 'a,b,c' into ['a', 'b', 'c'] at
        # parse time rather than post processing it
        add_put_arguments(parser)
        parser.add_argument('--grouping',
                            help='Set the grouping of the new objects')
        parser.add_argument('-c', '--copy-name',
                            help='Copy name for this object instance')

        parser.add_argument('-m', '--metadata',
                            help='Comma-separated list of key=value')
        parser.add_argument('--overwrite', action='store_true',
                            help='Allow object update')
        parser.add_argument('--file',
                            help='File containing lines like: '\
                                 '<src_file>  <object_id>  <metadata|->')
        parser.add_argument('--no-split', action='store_true',
                            help='Prevent splitting object over multiple '
                            'media.')
        parser.add_argument('src_file', help='File to insert', nargs='?')
        parser.add_argument('object_id', help='Desired object ID', nargs='?')

    def register_multi_puts(self, mput_file, put_params):
        """Register the put requests from the given file"""
        if mput_file == '-':
            fin = sys.stdin
        else:
            fin = open(mput_file)

        mput_list = []
        for i, line in enumerate(fin):
            # Skip empty lines and comments
            line = line.strip()
            if not line or line.startswith('#'):
                continue

            try:
                match = mput_file_line_parser(line)
            except ValueError as err:
                self.logger.error("Format error on line %d: %s: %s", i + 1,
                                  str(err), line)
                sys.exit(os.EX_DATAERR)

            src = match[0]
            oid = match[1]
            attrs = match[2]

            if attrs == '-':
                attrs = None
            else:
                attrs = attr_convert(attrs)
                self.logger.debug("Loaded attributes set %r", attrs)

            self.logger.debug("Inserting object '%s' to 'objid:%s'", src, oid)
            # A xfer with N objects will be built in order to group the alloc
            mput_list.append((oid, src, attrs))

        self.client.mput_register(mput_list, put_params=put_params)

        if fin is not sys.stdin:
            fin.close()


    def exec_put(self):
        """Insert an object into backend."""
        src = self.params.get('src_file')
        oid = self.params.get('object_id')
        mput_file = self.params.get('file')

        if not mput_file and (not src and not oid):
            self.logger.error("either '--file' or 'src_file'/'oid' must be "
                              "provided")
            sys.exit(os.EX_USAGE)
        elif mput_file and (src or oid):
            self.logger.error("only one of '--file' or 'src_file'/'oid' must "
                              "be provided")
            sys.exit(os.EX_USAGE)

        if not mput_file and not (src and oid):
            self.logger.error("both src and oid must be provided")
            sys.exit(os.EX_USAGE)

        attrs = self.params.get('metadata')
        if attrs is not None:
            attrs = attr_convert(attrs)
            self.logger.debug("Loaded attributes set %r", attrs)

        put_params = create_put_params(self, self.params.get('copy_name'))

        if mput_file:
            self.register_multi_puts(mput_file, put_params)
        else:
            self.client.put_register(oid, src, attrs=attrs,
                                     put_params=put_params)
            self.logger.debug("Inserting object '%s' to 'objid:%s'", src, oid)

        try:
            self.client.run()
        except IOError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))


class StoreMPutHandler(StorePutHandler):
    """Deprecated, use 'put --file' instead."""
    label = 'mput'
    descr = "Deprecated, use 'put --file' instead."

    @classmethod
    def add_options(cls, parser):
        """Add options for the PUT command."""
        #super(StoreMPutHandler, cls).add_options(parser)
        parser.description = "Deprecated, use 'put --file' instead."

    def exec_mput(self):
        """Deprecated, use 'put --file' instead."""
        self.logger.error("'mput' is a deprecated command, use 'put --file' "
                          "instead")
        sys.exit(os.EX_USAGE)


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
    StoreMPutHandler,
    StorePutHandler,
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
