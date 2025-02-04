#!/usr/bin/env python3
# pylint: disable=too-many-lines

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

from ClusterShell.NodeSet import NodeSet

from phobos.core.admin import Client as AdminClient
import phobos.core.cfg as cfg
from phobos.core.const import (PHO_LIB_SCSI, rsc_family2str, # pylint: disable=no-name-in-module
                               PHO_RSC_ADM_ST_LOCKED, PHO_RSC_ADM_ST_UNLOCKED,
                               ADM_STATUS, TAGS, PUT_ACCESS, GET_ACCESS,
                               DELETE_ACCESS, PHO_RSC_TAPE, PHO_RSC_NONE,
                               PHO_OPERATION_INVALID, fs_type2str,
                               str2operation_type, DSS_STATUS_FILTER_ALL,
                               DSS_STATUS_FILTER_COMPLETE,
                               DSS_STATUS_FILTER_INCOMPLETE,
                               DSS_STATUS_FILTER_READABLE, DSS_MEDIA,
                               DSS_OBJ_ALIVE, DSS_OBJ_DEPRECATED,
                               DSS_OBJ_DEPRECATED_ONLY,
                               PHO_RSC_RADOS_POOL)
from phobos.core.ffi import (DeprecatedObjectInfo, DevInfo, DriveStatus,
                             LayoutInfo, MediaInfo, ObjectInfo, ResourceFamily,
                             CLIManagedResourceMixin, FSType, Id, LogFilter,
                             Timeval)
from phobos.core.store import XferClient, UtilClient, attrs_as_dict, PutParams
from phobos.output import dump_object_list


from phobos.cli.action.add import AddOptHandler
from phobos.cli.action.format import FormatOptHandler
from phobos.cli.action.lock import LockOptHandler
from phobos.cli.action.resource_delete import ResourceDeleteOptHandler
from phobos.cli.action.status import StatusOptHandler
from phobos.cli.action.unlock import UnlockOptHandler
from phobos.cli.common import (BaseOptHandler, PhobosActionContext,
                               DSSInteractHandler, BaseResourceOptHandler,
                               env_error_format, XferOptHandler)
from phobos.cli.common.args import (add_list_arguments, add_put_arguments)
from phobos.cli.common.exec import (exec_add_dir_rados, exec_delete_dir_rados,
                                    exec_delete_medium_device)
from phobos.cli.common.utils import (check_output_attributes, get_params_status,
                                     handle_sort_option, setaccess_epilog,
                                     set_library, uncase_fstype)
from phobos.cli.target.copy import CopyOptHandler
from phobos.cli.target.dir import DirOptHandler
from phobos.cli.target.drive import DriveOptHandler
from phobos.cli.target.rados import RadosPoolOptHandler
from phobos.cli.target.tape import TapeOptHandler

def attr_convert(usr_attr):
    """Convert k/v pairs as expressed by the user into a dictionnary."""
    tkn_iter = shlex(usr_attr, posix=True)
    tkn_iter.whitespace = '=,'
    tkn_iter.whitespace_split = True

    kv_pairs = list(tkn_iter) # [k0, v0, k1, v1...]

    if len(kv_pairs) % 2 != 0:
        print(kv_pairs)
        raise ValueError("Invalid attribute string")

    return dict(zip(kv_pairs[0::2], kv_pairs[1::2]))

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
        no_split = self.params.get('no_split')

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

        lyt_attrs = self.params.get('layout_params')
        if lyt_attrs is not None:
            lyt_attrs = attr_convert(lyt_attrs)
            self.logger.debug("Loaded layout params set %r", lyt_attrs)

        put_params = PutParams(profile=self.params.get('profile'),
                               copy_name=self.params.get('copy_name'),
                               grouping=self.params.get('grouping'),
                               family=self.params.get('family'),
                               library=self.params.get('library'),
                               layout=self.params.get('layout'),
                               lyt_params=lyt_attrs,
                               no_split=no_split,
                               overwrite=self.params.get('overwrite'),
                               tags=self.params.get('tags', []))

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


class ListOptHandler(DSSInteractHandler):
    """List items of a specific type."""
    label = 'list'
    descr = 'list all entries of the kind'

    @classmethod
    def add_options(cls, parser):
        """Add resource-specific options."""
        super(ListOptHandler, cls).add_options(parser)
        parser.add_argument('res', nargs='*', help='resource(s) to list')
        parser.add_argument('-f', '--format', default='human',
                            help="output format human/xml/json/csv/yaml " \
                                 "(default: human)")


class LibScanOptHandler(BaseOptHandler):
    """Scan a library and display retrieved information."""
    label = 'scan'
    descr = 'Display the status of the library'

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        pass

    @classmethod
    def add_options(cls, parser):
        """Add resource-specific options."""
        super(LibScanOptHandler, cls).add_options(parser)
        parser.add_argument('res', nargs='*',
                            help="Library(ies) to scan. If not given, will "
                                 "fetch instead the default tape library from "
                                 "configuration")
        parser.add_argument('--refresh', action='store_true',
                            help="The library module refreshes its internal "
                                 "cache before sending the data")

class LibRefreshOptHandler(BaseOptHandler):
    """Refresh the library internal cache"""
    label = 'refresh'
    descr = 'Refresh the library internal cache'

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        pass

    @classmethod
    def add_options(cls, parser):
        """Add resource-specific options."""
        super(LibRefreshOptHandler, cls).add_options(parser)
        parser.add_argument('res', nargs='*',
                            help="Library(ies) to refresh. If not given, will "
                                 "fetch instead the default tape library from "
                                 "configuration")


def check_max_width_is_valid(value):
    """Check that the width 'value' is greater than the one of '...}'"""
    ivalue = int(value)
    if ivalue <= len("...}"):
        raise argparse.ArgumentTypeError("%s is an invalid positive "
                                         "int value" % value)
    return ivalue

class ObjectListOptHandler(ListOptHandler):
    """
    Specific version of the 'list' command for object, with a couple
    extra-options.
    """
    descr = 'list all objects'

    @classmethod
    def add_options(cls, parser):
        """Add object-specific options."""
        super(ObjectListOptHandler, cls).add_options(parser)

        base_attrs = list(ObjectInfo().get_display_dict().keys())
        base_attrs.sort()
        ext_attrs = list(DeprecatedObjectInfo().get_display_dict().keys() -
                         ObjectInfo().get_display_dict().keys())
        ext_attrs.sort()
        add_list_arguments(parser, base_attrs, "oid", True)
        parser.add_argument('-d', '--deprecated', action='store_true',
                            help="print deprecated objects, allowing those "
                                 "attributes for the 'output' option "
                                 "{" + " ".join(ext_attrs) + "}")
        parser.add_argument('-m', '--metadata', type=lambda t: t.split(','),
                            help="filter items containing every given "
                                 "metadata, comma-separated "
                                 "'key=value' parameters")
        parser.add_argument('-p', '--pattern', action='store_true',
                            help="filter using POSIX regexp instead of exact "
                                 "objid")
        parser.add_argument('-t', '--no-trunc', action='store_true',
                            help="do not truncate the user_md column (takes "
                                 "precedency over the 'max-width' argument)")
        parser.add_argument('-w', '--max-width', default=30,
                            type=check_max_width_is_valid,
                            help="max width of the user_md column keys and "
                                 "values, must be greater or equal to 5"
                                 "(default is 30)")


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


class ExtentListOptHandler(ListOptHandler):
    """
    Specific version of the 'list' command for extent, with a couple
    extra-options.
    """
    descr = "list all extents"

    @classmethod
    def add_options(cls, parser):
        """Add extent-specific options."""
        super(ExtentListOptHandler, cls).add_options(parser)

        attr = list(LayoutInfo().get_display_dict().keys())
        attr.sort()
        parser.add_argument('-c', '--copy-name',
                            help="Copy's name of the extents to list")
        parser.add_argument('-o', '--output', type=lambda t: t.split(','),
                            default='oid',
                            help=("attributes to output, comma-separated, "
                                  "choose from {" + " ".join(attr) + "} "
                                  "default: %(default)s)"))
        parser.add_argument('-n', '--name',
                            help="filter on one medium name")
        parser.add_argument('--degroup', action='store_true',
                            help="used to list by extent, not by object")
        parser.add_argument('-p', '--pattern', action='store_true',
                            help="filter using POSIX regexp instead of "
                                 "exact extent")
        attr_sort = list(LayoutInfo().get_sort_fields().keys())
        attr_sort.sort()
        parser.add_argument('--sort',
                            help=("sort the output with, choose from {"
                                  + " ".join(attr_sort) + "} "))
        parser.add_argument('--rsort',
                            help=("sort the output in descending "
                                  "order, choose from {" + " ".join(attr_sort)
                                  + "} "))
        parser.add_argument('--library',
                            help="filter the output by library name")


class MediumLocateOptHandler(DSSInteractHandler):
    """Locate a medium into the system."""
    label = 'locate'
    descr = 'locate a medium'

    @classmethod
    def add_options(cls, parser):
        super(MediumLocateOptHandler, cls).add_options(parser)
        parser.add_argument('res', help='medium to locate')
        parser.add_argument('--library',
                            help="Library containing the medium to locate")




class ObjectOptHandler(BaseResourceOptHandler):
    """Shared interface for objects."""
    label = 'object'
    descr = 'handle objects'
    verbs = [
        ObjectListOptHandler,
    ]

    def exec_list(self):
        """List objects."""
        attrs = list(DeprecatedObjectInfo().get_display_dict().keys()
                     if self.params.get('deprecated')
                     else ObjectInfo().get_display_dict().keys())
        check_output_attributes(attrs, self.params.get('output'), self.logger)

        metadata = []
        if self.params.get('metadata'):
            metadata = self.params.get('metadata')
            for elt in metadata:
                if '=' not in elt:
                    self.logger.error("Metadata parameter '%s' must be a "
                                      "'key=value'", elt)
                    sys.exit(os.EX_USAGE)

        kwargs = {}
        if self.params.get('deprecated'):
            kwargs = handle_sort_option(self.params, DeprecatedObjectInfo(),
                                        self.logger, **kwargs)
        else:
            kwargs = handle_sort_option(self.params, ObjectInfo(), self.logger,
                                        **kwargs)

        client = UtilClient()

        try:
            objs = client.object_list(self.params.get('res'),
                                      self.params.get('pattern'),
                                      metadata,
                                      self.params.get('deprecated'),
                                      **kwargs)

            if objs:
                max_width = (None if self.params.get('no_trunc')
                             else self.params.get('max_width'))

                dump_object_list(objs, attr=self.params.get('output'),
                                 max_width=max_width,
                                 fmt=self.params.get('format'))

            client.list_obj_free(objs, len(objs))
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


class ExtentOptHandler(BaseResourceOptHandler):
    """Shared interface for extents."""
    label = 'extent'
    descr = 'handle extents'
    verbs = [
        ExtentListOptHandler
    ]

    def exec_list(self):
        """List extents."""
        attrs = list(LayoutInfo().get_display_dict().keys())
        check_output_attributes(attrs, self.params.get('output'), self.logger)

        kwargs = {}
        if self.params.get('library'):
            kwargs['library'] = self.params.get('library')

        if self.params.get('copy_name'):
            kwargs['copy_name'] = self.params.get('copy_name')

        kwargs = handle_sort_option(self.params, LayoutInfo(), self.logger,
                                    **kwargs)

        try:
            with AdminClient(lrs_required=False) as adm:
                obj_list, p_objs, n_objs = adm.layout_list(
                    self.params.get('res'),
                    self.params.get('pattern'),
                    self.params.get('name'),
                    self.params.get('degroup'),
                    **kwargs)

                if len(obj_list) > 0:
                    dump_object_list(obj_list, attr=self.params.get('output'),
                                     fmt=self.params.get('format'))

                adm.layout_list_free(p_objs, n_objs)

        except EnvironmentError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))

class LibOptHandler(BaseResourceOptHandler):
    """Tape library options and actions."""
    label = 'lib'
    descr = 'handle tape libraries'
    # for now, this class in only used for tape library actions, but in case
    # it is used for other families, set family as None
    family = ResourceFamily(ResourceFamily.RSC_TAPE)
    verbs = [
        LibScanOptHandler,
        LibRefreshOptHandler,
    ]

    def exec_scan(self):
        """Scan this lib and display the result"""
        libs = self.params.get("res")
        if not libs:
            libs = [cfg.get_default_library(ResourceFamily.RSC_TAPE)]

        with_refresh = self.params.get('refresh')
        try:
            with AdminClient(lrs_required=False) as adm:
                for lib in libs:
                    lib_data = adm.lib_scan(PHO_LIB_SCSI, lib, with_refresh)
                    # FIXME: can't use dump_object_list yet as it does not play
                    # well with unstructured dict-like data (relies on getattr)
                    self._print_lib_data(lib_data)
        except EnvironmentError as err:
            self.logger.error("%s, will abort 'lib scan'", err)
            sys.exit(abs(err.errno))

    def exec_refresh(self):
        """Refresh the library internal cache"""
        libs = self.params.get("res")
        if not libs:
            libs = [cfg.get_default_library(ResourceFamily.RSC_TAPE)]

        try:
            with AdminClient(lrs_required=False) as adm:
                for lib in libs:
                    adm.lib_refresh(PHO_LIB_SCSI, lib)
        except EnvironmentError as err:
            self.logger.error("%s, will abort 'lib refresh'", err)
            sys.exit(abs(err.errno))

    @staticmethod
    def _print_lib_data(lib_data):
        """Print library data in a human readable format"""
        for elt in lib_data:
            done = set(["type"])
            line = []
            flags = []
            line.append("%s:" % (elt.get("type", "element"),))
            for key in "address", "source_address":
                if key in elt:
                    line.append("%s=%#x" % (key, elt[key]))
                    done.add(key)

            for key in sorted(elt):
                value = elt[key]
                if key in done:
                    continue
                if isinstance(value, bool):
                    if value:
                        # Flags are printed after the other properties
                        flags.append(key)
                else:
                    line.append("%s=%r" % (key, str(value)))
            line.extend(flags)
            print(" ".join(line))

class CleanOptHandler(BaseOptHandler):
    """Clean locks"""
    label = 'clean'
    descr = 'clean locks options'

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        pass

    @classmethod
    def add_options(cls, parser):
        """Add resource-specific clean options."""
        super(CleanOptHandler, cls).add_options(parser)
        parser.add_argument('--global', action='store_true',
                            help='target all locks (can only be used '
                                 'with --force). If not set, '
                                 'only target localhost locks')
        parser.add_argument('--force', action='store_true',
                            help='clean locks even if phobosd is on')
        parser.add_argument('-t', '--type',
                            help='lock type to clean, between [device, media, '
                                 'object, media_update]',
                            choices=["device", "media",
                                     "object", "media_update"])
        parser.add_argument('-f', '--family',
                            help='Family of locked ressources to clean, '
                                 'between [dir, tape]; object type '
                                 'is not supported with this option',
                            choices=["dir", "tape"])
        parser.add_argument('-i', '--ids', nargs='+', help='lock id(s)')

class LocksOptHandler(BaseOptHandler):
    """Locks table actions and options"""
    label = 'lock'
    descr = 'handle lock table'
    verbs = [CleanOptHandler]

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        pass

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
