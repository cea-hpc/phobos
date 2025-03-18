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

import argparse
import json
from shlex import shlex
import sys
import datetime

import os
import os.path

from ctypes import (c_int, c_long, byref, pointer, c_bool)

from phobos.core.admin import Client as AdminClient
from phobos.core.const import (rsc_family2str, # pylint: disable=no-name-in-module
                               DSS_OBJ_ALIVE, DSS_OBJ_DEPRECATED,
                               DSS_OBJ_DEPRECATED_ONLY,
                               PHO_RSC_TAPE, PHO_RSC_NONE,
                               PHO_OPERATION_INVALID,
                               str2operation_type)
from phobos.core.ffi import (DevInfo, MediaInfo, ResourceFamily,
                             Id, LogFilter, Timeval)
from phobos.core.store import XferClient, UtilClient, attrs_as_dict, PutParams

from phobos.cli.common import (BaseOptHandler, PhobosActionContext,
                               DSSInteractHandler, BaseResourceOptHandler,
                               env_error_format, XferOptHandler)
from phobos.cli.common.args import add_put_arguments
from phobos.cli.common.utils import set_library, create_put_params, attr_convert
from phobos.cli.target.copy import CopyOptHandler
from phobos.cli.target.dir import DirOptHandler
from phobos.cli.target.drive import DriveOptHandler
from phobos.cli.target.extent import ExtentOptHandler
from phobos.cli.target.lib import LibOptHandler
from phobos.cli.target.lock import LocksOptHandler
from phobos.cli.target.object import ObjectOptHandler
from phobos.cli.target.rados import RadosPoolOptHandler
from phobos.cli.target.tape import TapeOptHandler


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


class StoreGetMDHandler(XferOptHandler):
    """Retrieve object from backend."""
    label = 'getmd'
    descr = 'retrieve object metadata from backend'

    @classmethod
    def add_options(cls, parser):
        """Add options for the GETMD command."""
        super(StoreGetMDHandler, cls).add_options(parser)
        parser.add_argument('object_id', help='Object to target')

    @staticmethod
    def _compl_notify(data, xfr, err_code): # pylint: disable=unused-argument
        """Custom completion handler to display metadata."""
        if err_code != 0:
            return

        res = []
        itm = attrs_as_dict(xfr.contents.xd_targets[0].xt_attrs)
        if not itm:
            return

        for key, value in sorted(itm.items()):
            res.append('%s=%s' % (key, value))

        print(','.join(res))

    def exec_getmd(self):
        """Retrieve an object attributes from backend."""
        oid = self.params.get('object_id')
        self.logger.debug("Retrieving attrs for 'objid:%s'", oid)
        self.client.getmd_register(oid, None)
        try:
            self.client.run(compl_cb=self._compl_notify)
        except IOError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))


class StoreGetHandler(XferOptHandler):
    """Retrieve object from backend."""
    label = 'get'
    descr = 'retrieve object from backend'

    @classmethod
    def add_options(cls, parser):
        """Add options for the GET command."""
        super(StoreGetHandler, cls).add_options(parser)
        parser.add_argument('object_id', help='Object to retrieve')
        parser.add_argument('dest_file', help='Destination file')
        parser.add_argument('--version', help='Version of the object',
                            type=int, default=0)
        parser.add_argument('--uuid', help='UUID of the object')
        parser.add_argument('--best-host', action='store_true',
                            help="Only get object if the current host is the "
                                 "most optimal one or if the object can be "
                                 "accessed from any node, else return the best "
                                 "hostname to get this object")

    def exec_get(self):
        """Retrieve an object from backend."""
        oid = self.params.get('object_id')
        dst = self.params.get('dest_file')
        version = self.params.get('version')
        uuid = self.params.get('uuid')
        best_host = self.params.get('best_host')
        self.logger.debug("Retrieving object 'objid:%s' to '%s'", oid, dst)
        self.client.get_register(oid, dst, (uuid, version), best_host)
        try:
            self.client.run()
        except IOError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))

        if version != 0:
            if uuid is not None:
                self.logger.info("Object '%s@%d' with uuid '%s' "
                                 "successfully retrieved",
                                 oid, version, uuid)
            else:
                self.logger.info("Object '%s@%d' successfully retrieved",
                                 oid, version)
        else:
            if uuid is not None:
                self.logger.info("Object '%s' with uuid '%s' "
                                 "successfully retrieved",
                                 oid, uuid)
            else:
                self.logger.info("Object '%s' successfully retrieved", oid)

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
            # If no_split option is set, a xfer with N objects will be built in
            # order to group the alloc. Otherwise, N xfers with 1 object
            # will be built.
            if put_params.no_split:
                mput_list.append((oid, src, attrs))
            else:
                self.client.put_register(oid, src, attrs, put_params=put_params)

        if put_params.no_split:
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
        parser.add_argument('--uuid', help='UUID of the object to delete')
        parser.add_argument('--version', type=int,
                            help='Version of the objec to delete')
        deprec_group = parser.add_mutually_exclusive_group()
        deprec_group.add_argument('-d', '--deprecated', action='store_true',
                                  help=('allow to delete also deprecated '
                                        'objects, can only be used with '
                                        '--hard option'))
        deprec_group.add_argument('-D', '--deprecated-only',
                                  action='store_true',
                                  help=('allow to delete only deprecated '
                                        'objects, can only be used with '
                                        '--hard option'))
        parser.add_argument('--hard', action='store_true',
                            help='Require a hardware remove of the object')
        parser.set_defaults(verb=cls.label)

    def exec_delete(self):
        """Delete objects."""
        client = UtilClient()

        deprec = self.params.get('deprecated')
        deprec_only = self.params.get('deprecated_only')
        oids = self.params.get('oids')
        uuid = self.params.get('uuid')
        version = self.params.get('version')
        scope = DSS_OBJ_ALIVE

        if not self.params.get('hard') and (deprec or deprec_only):
            self.logger.error("--deprecated or --deprecated-only can only be "
                              "used with the --hard option")
            sys.exit(os.EX_USAGE)

        if len(oids) > 1 and (uuid is not None or version is not None):
            self.logger.error("Only one oid can be provided with the --uuid or "
                              "--version option")
            sys.exit(os.EX_USAGE)

        if deprec:
            scope = DSS_OBJ_DEPRECATED
        elif deprec_only:
            scope = DSS_OBJ_DEPRECATED_ONLY

        try:
            client.object_delete(oids, uuid, version, scope,
                                 self.params.get('hard'))
        except EnvironmentError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))

class UuidOptHandler(BaseOptHandler):
    """Handler to select objects with a list of uuids"""

    label = 'uuid'
    descr = 'select uuids'

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        pass

    @classmethod
    def add_options(cls, parser):
        """Add command options."""
        super(UuidOptHandler, cls).add_options(parser)

        parser.add_argument('uuids', nargs='+',
                            help='Object UUIDs to undelete')

class OidOptHandler(BaseOptHandler):
    """Handler to select objects with a list of oids"""

    label = 'oid'
    descr = 'select oids'

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        pass

    @classmethod
    def add_options(cls, parser):
        """Add command options."""
        super(OidOptHandler, cls).add_options(parser)

        parser.add_argument('oids', nargs='+',
                            help='Object OIDs to undelete')

class UndeleteOptHandler(BaseOptHandler):
    """Undelete objects handler."""

    label = 'undelete'
    alias = ['undel']
    descr = 'Move back deprecated objects into phobos namespace'
    verbs = [
        UuidOptHandler,
        OidOptHandler,
    ]

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        pass

    def exec_uuid(self):
        """Undelete by uuids"""
        client = UtilClient()
        try:
            client.object_undelete((), self.params.get('uuids'))
        except EnvironmentError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))

    def exec_oid(self):
        """Undelete by oids"""
        client = UtilClient()
        try:
            client.object_undelete(self.params.get('oids'), ())
        except EnvironmentError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))

class RenameOptHandler(BaseOptHandler):
    """Rename object handler"""

    label = 'rename'
    descr = 'Change the oid of an object generation, '\
            'among living or deprecated objects tables. At least one of '\
            '\'--oid\' or \'--uid\' must be specified.'

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        pass

    @classmethod
    def add_options(cls, parser):
        """Add command options for object rename."""
        super(RenameOptHandler, cls).add_options(parser)
        parser.set_defaults(verb=cls.label)
        parser.add_argument('--oid', help='Object ID to be renamed')
        parser.add_argument('--uuid', help='UUID of the object to rename')
        parser.add_argument('new_oid', help='New name for the object')

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


    def exec_locate(self):
        """Locate object"""
        client = UtilClient()
        try:
            hostname, _ = client.object_locate(
                self.params.get('oid'),
                self.params.get('uuid'),
                self.params.get('version'),
                self.params.get('focus_host'))

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


def str_to_timestamp(value):
    """
    Check that the date 'value' has a correct format and return it as
    timestamp
    """
    try:
        if value.__contains__(' '):
            element = datetime.datetime.strptime(value, "%Y-%m-%d %H:%M:%S")
        elif value.__contains__('-'):
            element = datetime.datetime.strptime(value, "%Y-%m-%d")
        else:
            raise ValueError()
    except ValueError:
        raise argparse.ArgumentTypeError("%s is not a valid date format" %
                                         value)

    return datetime.datetime.timestamp(element)


class LogsDumpOptHandler(BaseOptHandler):
    """Handler for persistent logs dumping"""
    label = "dump"
    descr = "handler for persistent logs dumping"
    epilog = """Will dump persistent logs recorded by Phobos to stdout or a file
    if provided, according to given filters."""

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        pass

    @classmethod
    def add_options(cls, parser):
        super(LogsDumpOptHandler, cls).add_options(parser)
        parser.add_argument('-D', '--drive',
                            help='drive ID of the logs to dump')
        parser.add_argument('-T', '--tape',
                            help='tape ID of the logs to dump')
        parser.add_argument('--library',
                            help="Library containing the target drive and tape")
        parser.add_argument('-e', '--errno', type=int,
                            help='error number of the logs to dump')
        parser.add_argument('--errors', action='store_true',
                            help='dump all errors')
        parser.add_argument('-c', '--cause',
                            help='cause of the logs to dump',
                            choices=["library_scan", "library_open",
                                     "device_lookup", "medium_lookup",
                                     "device_load", "device_unload",
                                     "ltfs_mount", "ltfs_umount", "ltfs_format",
                                     "ltfs_df", "ltfs_sync"])
        parser.add_argument('--start', type=str_to_timestamp, default=0,
                            help="timestamp of the most recent logs to dump,"
                                 "in format YYYY-MM-DD [hh:mm:ss]")
        parser.add_argument('--end', type=str_to_timestamp, default=0,
                            help="timestamp of the oldest logs to dump,"
                                 "in format 'YYYY-MM-DD [hh:mm:ss]'")


class LogsClearOptHandler(BaseOptHandler):
    """Handler for persistent logs clearing"""
    label = "clear"
    descr = "handler for persistent logs clearing"
    epilog = """Will clear persistent logs recorded by Phobos, according to
    given filters."""

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        pass

    @classmethod
    def add_options(cls, parser):
        super(LogsClearOptHandler, cls).add_options(parser)
        parser.add_argument('-D', '--drive',
                            help='drive ID of the logs to dump')
        parser.add_argument('-T', '--tape',
                            help='tape ID of the logs to dump')
        parser.add_argument('--library',
                            help="Library containing the target drive and tape")
        parser.add_argument('-e', '--errno', type=int,
                            help='error number of the logs to dump')
        parser.add_argument('--errors', action='store_true',
                            help='clear all errors')
        parser.add_argument('-c', '--cause',
                            help='cause of the logs to dump',
                            choices=["library_scan", "library_open",
                                     "device_lookup", "medium_lookup",
                                     "device_load", "device_unload",
                                     "ltfs_mount", "ltfs_umount", "ltfs_format",
                                     "ltfs_df", "ltfs_sync"])
        parser.add_argument('--start', type=str_to_timestamp, default=0,
                            help="timestamp of the most recent logs to dump,"
                                 "in format YYYY-MM-DD [hh:mm:ss]")
        parser.add_argument('--end', type=str_to_timestamp, default=0,
                            help="timestamp of the oldest logs to dump,"
                                 "in format 'YYYY-MM-DD [hh:mm:ss]'")
        parser.add_argument('--clear-all', action='store_true',
                            help='must be specified to clear all logs, will '
                                 'have no effect, if any of the other '
                                 'arguments is specified')

def create_log_filter(library, device, medium, _errno, cause, start, end, \
                      errors): # pylint: disable=too-many-arguments
    """Create a log filter structure with the given parameters."""
    device_id = Id(PHO_RSC_TAPE if (device or library) else PHO_RSC_NONE,
                   name=device if device else "",
                   library=library if library else "")
    medium_id = Id(PHO_RSC_TAPE if (medium or library) else PHO_RSC_NONE,
                   name=medium if medium else "",
                   library=library if library else "")
    c_errno = (pointer(c_int(int(_errno))) if _errno else None)
    c_cause = (c_int(str2operation_type(cause)) if cause else
               c_int(PHO_OPERATION_INVALID))
    c_start = Timeval(c_long(int(start)), 0)
    c_end = Timeval(c_long(int(end)), 999999)
    c_errors = (c_bool(errors) if errors else None)

    return (byref(LogFilter(device_id, medium_id, c_errno, c_cause, c_start,
                            c_end, c_errors))
            if device or medium or library or _errno or cause or start or
            end or errors else None)


class LogsOptHandler(BaseOptHandler):
    """Handler of logs commands"""
    label = "logs"
    descr = "interact with the persistent logs recorded by Phobos"
    verbs = [
        LogsDumpOptHandler,
        LogsClearOptHandler
    ]
    family = ResourceFamily(ResourceFamily.RSC_TAPE)
    library = None

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        pass

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


class PhobosdPingOptHandler(BaseOptHandler):
    """Phobosd ping"""
    label = 'phobosd'
    descr = 'ping local phobosd process'

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        pass

class TLCPingOptHandler(BaseOptHandler):
    """TLC ping"""
    label = 'tlc'
    descr = 'ping the Tape Library Controler'

    @classmethod
    def add_options(cls, parser):
        """Add resource-specific options."""
        super(TLCPingOptHandler, cls).add_options(parser)
        parser.add_argument('--library', help="Library of the TLC to ping")

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        pass

class PingOptHandler(BaseOptHandler):
    """Ping phobos daemon"""
    label = 'ping'
    descr = 'ping the phobos daemons'
    verbs = [
        PhobosdPingOptHandler,
        TLCPingOptHandler,
    ]
    family = None
    library = None

    @classmethod
    def add_options(cls, parser):
        """Add ping option to the parser"""
        super(PingOptHandler, cls).add_options(parser)
        parser.set_defaults(verb=cls.label)

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        pass

    def exec_phobosd(self):
        """Ping the lrs daemon to check if it is online."""
        try:
            with AdminClient(lrs_required=True) as adm:
                adm.ping_lrs()

        except EnvironmentError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))

        self.logger.info("Ping sent to phobosd successfully")

    def exec_tlc(self):
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
    PingOptHandler,
    RadosPoolOptHandler,
    RenameOptHandler,
    SchedOptHandler,
    TapeOptHandler,
    UndeleteOptHandler,

    # Store command interfaces
    StoreGetHandler,
    StoreGetMDHandler,
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
