#!/usr/bin/env python3
# pylint: disable=too-many-lines

#
#  All rights reserved (c) 2014-2024 CEA/DAM.
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
                               DSS_STATUS_FILTER_READABLE)
from phobos.core.ffi import (DeprecatedObjectInfo, DevInfo, LayoutInfo,
                             MediaInfo, ObjectInfo, ResourceFamily,
                             CLIManagedResourceMixin, FSType, Id, LogFilter,
                             Timeval)
from phobos.core.store import XferClient, UtilClient, attrs_as_dict, PutParams
from phobos.output import dump_object_list

from phobos.cli.common import (BaseOptHandler, PhobosActionContext,
                               DSSInteractHandler, BaseResourceOptHandler,
                               env_error_format)
from phobos.cli.common.args import (add_put_arguments, check_output_attributes,
                                    get_params_status)
from phobos.cli.target.copy import CopyOptHandler

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


class XferOptHandler(BaseOptHandler):
    """Option handler for actions that do data transfers."""
    def __init__(self, params, **kwargs):
        """Initialize a store client."""
        super(XferOptHandler, self).__init__(params, **kwargs)
        self.client = None

    @classmethod
    def add_options(cls, parser):
        """
        Add options for the given command. We register the class label as the
        verb for the special case of data xfer commands.
        """
        super(XferOptHandler, cls).add_options(parser)
        parser.set_defaults(verb=cls.label)

    def __enter__(self):
        """Initialize a client for data movements."""
        self.client = XferClient()
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        """Release resources associated to a XferClient."""
        self.client.clear()


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

class AddOptHandler(DSSInteractHandler):
    """Insert a new resource into the system."""
    label = 'add'
    descr = 'insert new resource(s) to the system'

    @classmethod
    def add_options(cls, parser):
        super(AddOptHandler, cls).add_options(parser)
        parser.add_argument('--unlock', action='store_true',
                            help='Unlock resource on success')
        parser.add_argument('--library',
                            help="Library containing added resources")
        parser.add_argument('res', nargs='+', help='Resource(s) to add')


class MediaAddOptHandler(AddOptHandler):
    """Insert a new media into the system."""
    descr = 'insert new media to the system'

    @classmethod
    def add_options(cls, parser):
        super(MediaAddOptHandler, cls).add_options(parser)
        # The type argument allows to transform 'a,b,c' into ['a', 'b', 'c'] at
        # parse time rather than post processing it
        parser.add_argument('-T', '--tags', type=lambda t: t.split(','),
                            help='tags to associate with this media (comma-'
                                 'separated: foo,bar)')

class ResourceDeleteOptHandler(DSSInteractHandler):
    """Remove a resource from the system."""
    label = 'delete'
    alias = ['del']
    descr = 'remove resource(s) from the system'
    epilog = "Resources are only removed from the database, and so will not "\
             "be available through Phobos anymore. No other operations are "\
             "executed."

    @classmethod
    def add_options(cls, parser):
        super(ResourceDeleteOptHandler, cls).add_options(parser)
        parser.add_argument('--library',
                            help="Library containing deleted resources")
        parser.add_argument('res', nargs='+', help='Resource(s) to remove')
        parser.set_defaults(verb=cls.label)

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

class LockOptHandler(DSSInteractHandler):
    """Lock resource."""
    label = 'lock'
    descr = 'lock resource(s)'

    @classmethod
    def add_options(cls, parser):
        """Add resource-specific options."""
        super(LockOptHandler, cls).add_options(parser)
        parser.add_argument('res', nargs='+',
                            help='Resource(s) to lock (for a device, could be '
                                 'the path or the id name)')
        parser.add_argument('--library',
                            help="Library containing resources to lock")

class DeviceLockOptHandler(LockOptHandler):
    """Lock device."""

    @classmethod
    def add_options(cls, parser):
        """Add resource-specific options."""
        super(DeviceLockOptHandler, cls).add_options(parser)
        parser.add_argument('--wait', action='store_true',
                            help='wait for any deamon releasing the device')


class UnlockOptHandler(DSSInteractHandler):
    """Unlock resource."""
    label = 'unlock'
    descr = 'unlock resource(s)'

    @classmethod
    def add_options(cls, parser):
        """Add resource-specific options."""
        super(UnlockOptHandler, cls).add_options(parser)
        parser.add_argument('res', nargs='+', help='Resource(s) to unlock')
        parser.add_argument('--force', action='store_true',
                            help='Do not check the current lock state')
        parser.add_argument('--library',
                            help="Library containing resources to unlock")

class StatusOptHandler(BaseOptHandler):
    """Display I/O and drive status"""
    label = 'status'
    descr = 'display I/O and drive status'

    @classmethod
    def add_options(cls, parser):
        """Add resource-specific options."""
        super(StatusOptHandler, cls).add_options(parser)
        attr = list(DriveStatus().get_display_fields().keys())
        attr.sort()
        parser.add_argument('-o', '--output', type=lambda t: t.split(','),
                            default='all',
                            help=("attributes to output, comma-separated, "
                                  "choose from {" + " ".join(attr) + "} "
                                  "(default: %(default)s)"))

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        pass


OP_NAME_FROM_LETTER = {'P': 'put', 'G': 'get', 'D': 'delete'}
def parse_set_access_flags(flags):
    """From [+|-]PGD flags, return a dict with media operations to set

    Unchanged operation are absent from the returned dict.
    Dict key are among {'put', 'get', 'delete'}.
    Dict values are among {True, False}.
    """
    res = {}
    if not flags:
        return {}

    if flags[0] == '+':
        target = True
        flags = flags[1:]
    elif flags[0] == '-':
        target = False
        flags = flags[1:]
    else:
        # present operations will be enabled
        target = True
        # absent operations will be disabled: preset all to False
        for op_name in OP_NAME_FROM_LETTER.values():
            res[op_name] = False

    for op_letter in flags:
        if op_letter not in OP_NAME_FROM_LETTER:
            raise argparse.ArgumentTypeError(f'{op_letter} is not a valid '
                                             'media operation flags')

        res[OP_NAME_FROM_LETTER[op_letter]] = target

    return res

class MediaSetAccessOptHandler(DSSInteractHandler):
    """Set media operation flags."""
    label = 'set-access'
    descr = 'set media operation flags'

    @classmethod
    def add_options(cls, parser):
        """Add resource-specific options."""
        super(MediaSetAccessOptHandler, cls).add_options(parser)
        parser.add_argument(
            'flags',
            type=parse_set_access_flags,
            metavar='FLAGS',
            help='[+|-]LIST, where LIST is made of capital letters among PGD,'
                 ' P: put, G: get, D: delete',
        )
        parser.add_argument('res', nargs='+', metavar='RESOURCE',
                            help='Resource(s) to update access mode')
        parser.formatter_class = argparse.RawDescriptionHelpFormatter

def setaccess_epilog(family):
    """Generic epilog"""
    return """Examples:
    phobos %s set-access GD      # allow get and delete, forbid put
    phobos %s set-access +PG     # allow put, get (other flags are unchanged)
    phobos %s set-access -- -P   # forbid put (other flags are unchanged)
    (Warning: use the '--' separator to use the -PGD flags syntax)
    """ % (family, family, family)

class DirSetAccessOptHandler(MediaSetAccessOptHandler):
    """Set media operation flags to directory media."""
    epilog = setaccess_epilog("dir")

class TapeSetAccessOptHandler(MediaSetAccessOptHandler):
    """Set media operation flags to tape media."""
    epilog = setaccess_epilog("tape")

class RadosPoolSetAccessOptHandler(MediaSetAccessOptHandler):
    """Set media operation flags to rados_pool media."""
    epilog = setaccess_epilog("rados_pool")

class LibScanOptHandler(BaseOptHandler):
    """Scan a physical resource and display retrieved information."""
    label = 'scan'
    descr = 'Display the physical resources status of the library'

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        pass

    @classmethod
    def add_options(cls, parser):
        """Add resource-specific options."""
        super(LibScanOptHandler, cls).add_options(parser)
        parser.add_argument('res', nargs='*',
                            help="Resource(s) or device(s) to scan. If not "
                                 "given, will fetch 'lib_device' path in "
                                 "configuration file instead")
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
                            help="Resource(s) or device(s) to refresh. If not "
                                 "given, will fetch 'lib_device' path in "
                                 "configuration file instead")

class DriveListOptHandler(ListOptHandler):
    """
    Specific version of the 'list' command for tape drives, with a couple
    extra-options.
    """
    descr = 'list all drives'

    @classmethod
    def add_options(cls, parser):
        """Add resource-specific options."""
        super(DriveListOptHandler, cls).add_options(parser)
        parser.add_argument('-m', '--model', help='filter on model')

        attr = list(DevInfo().get_display_dict().keys())
        attr.sort()
        parser.add_argument('-o', '--output', type=lambda t: t.split(','),
                            default='name',
                            help=("attributes to output, comma-separated, "
                                  "choose from {" + " ".join(attr) + "} "
                                  "(default: %(default)s)"))
        parser.add_argument('--sort',
                            help=("attribute to sort the output with, "
                                  "choose from {" + " ".join(attr) + "} "))
        parser.add_argument('--rsort',
                            help=("attribute to sort the output in descending "
                                  "order, choose from {" + " ".join(attr) + "} "
                                  ))
        parser.add_argument('--library',
                            help="attribute to filter the output by library"
                                 "name")
        parser.add_argument('--status',
                            help="filter the output by status name, "
                                 " {locked, unlocked, failed} ")

class MediaListOptHandler(ListOptHandler):
    """
    Specific version of the 'list' command for media, with a couple
    extra-options.
    """
    descr = "list all media"

    @classmethod
    def add_options(cls, parser):
        """Add resource-specific options."""
        super(MediaListOptHandler, cls).add_options(parser)
        parser.add_argument('-T', '--tags', type=lambda t: t.split(','),
                            help='filter on tags (comma-separated: foo,bar)')

        attr = list(MediaInfo().get_display_dict().keys())
        attr.sort()
        parser.add_argument('-o', '--output', type=lambda t: t.split(','),
                            default='name',
                            help=("attributes to output, comma-separated, "
                                  "choose from {" + " ".join(attr) + "} "
                                  "(default: %(default)s)"))
        parser.add_argument('--sort',
                            help=("attribute to sort the output with, "
                                  "choose from {" + " ".join(attr) + "} "))
        parser.add_argument('--rsort',
                            help=("attribute to sort the output in descending "
                                  "order, choose from {" + " ".join(attr) + "} "
                                 ))
        parser.add_argument('--library',
                            help="filter the output by library name")
        parser.add_argument('--status',
                            help="filter the output by status name, choose from"
                                 " {locked, unlocked, failed} ")
        parser.formatter_class = argparse.RawDescriptionHelpFormatter
        parser.epilog = """About file system status `fs.status`:
    blank: medium is not formatted
    empty: medium is formatted, no data written to it
    used: medium contains data
    full: medium is full, no more data can be written to it"""

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
        parser.add_argument('-d', '--deprecated', action='store_true',
                            help="print deprecated objects, allowing those "
                                 "attributes for the 'output' option "
                                 "{" + " ".join(ext_attrs) + "}")
        parser.add_argument('-m', '--metadata', type=lambda t: t.split(','),
                            help="filter items containing every given "
                                 "metadata, comma-separated "
                                 "'key=value' parameters")
        parser.add_argument('-o', '--output', type=lambda t: t.split(','),
                            default='oid',
                            help=("attributes to output, comma-separated, "
                                  "choose from {" + " ".join(base_attrs) + "} "
                                  "default: %(default)s"))
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
        parser.add_argument('--sort',
                            help=("attribute to sort the output with, "
                                  "choose from {" + " ".join(base_attrs) + "} "
                                  ))
        parser.add_argument('--rsort',
                            help=("attribute to sort the output in descending "
                                  "order, choose from "
                                  "{" + " ".join(base_attrs)+ "} "))

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
        parser.set_defaults(verb=cls.label)

    def exec_delete(self):
        """Delete objects."""
        client = UtilClient()
        try:
            client.object_delete(self.params.get('oids'),
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

class RepackOptHandler(BaseOptHandler):
    """Repack a medium."""
    label = 'repack'
    descr = 'Repack a medium, by copying its alive extents to another one'

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        pass

    @classmethod
    def add_options(cls, parser):
        super(RepackOptHandler, cls).add_options(parser)
        parser.add_argument('-T', '--tags', type=lambda t: t.split(','),
                            help='Only use a medium that contain this set of '
                                 'tags (comma-separated: foo,bar)')
        parser.add_argument('res', help='Medium to repack')
        parser.add_argument('--library',
                            help="Library containing the medium to repack")

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

def uncase_fstype(choices):
    """Check if an uncase FS type is a valid choice."""
    def find_choice(choice):
        """Return the uppercase choice if valid, the input choice if not."""
        for key, item in enumerate([elt.upper() for elt in choices]):
            if choice.upper() == item:
                return choices[key]
        return choice
    return find_choice

class TapeAddOptHandler(MediaAddOptHandler):
    """Specific version of the 'add' command for tapes, with extra-options."""
    descr = "insert new tape to the system"

    @classmethod
    def add_options(cls, parser):
        """Add resource-specific options."""
        super(TapeAddOptHandler, cls).add_options(parser)
        parser.add_argument('-t', '--type', required=True,
                            help='tape technology')
        parser.add_argument('--fs', default="LTFS",
                            choices=list(map(fs_type2str, FSType)),
                            type=uncase_fstype(list(map(fs_type2str, FSType))),
                            help='Filesystem type (default: LTFS)')

class MediumImportOptHandler(XferOptHandler):
    """Import a medium into the system"""
    label = 'import'
    descr = 'import existing media'

    @classmethod
    def add_options(cls, parser):
        super(MediumImportOptHandler, cls).add_options(parser)
        parser.add_argument('media', nargs='+',
                            help="name of the media to import")
        parser.add_argument('--check-hash', action='store_true',
                            help="recalculates hashes and compares them "
                                 "with the hashes of the extent")
        parser.add_argument('--unlock', action='store_true',
                            help="unlocks the tape after the import")
        parser.add_argument('--library', help="Library containing each medium")

class TapeImportOptHandler(MediumImportOptHandler):
    """Specific version of the 'import' command for tapes"""
    descr = "import existing tape"

    @classmethod
    def add_options(cls, parser):
        super(TapeImportOptHandler, cls).add_options(parser)
        parser.add_argument('-t', '--type', required=True,
                            help='tape technology')
        parser.add_argument('--fs', default="LTFS",
                            choices=list(map(fs_type2str, FSType)),
                            type=uncase_fstype(list(map(fs_type2str, FSType))),
                            help='Filesystem type (default: LTFS)')

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

class MediaUpdateOptHandler(DSSInteractHandler):
    """Update an existing media"""
    label = 'update'
    descr = 'update existing media properties'

    @classmethod
    def add_options(cls, parser):
        """Add resource-specific options."""
        super(MediaUpdateOptHandler, cls).add_options(parser)
        parser.add_argument('-T', '--tags', type=lambda t: t.split(','),
                            help='New tags for this media (comma-separated, '
                                 'e.g. "-T foo,bar"), empty string to clear '
                                 'tags, new tags list overwrite current tags')
        parser.add_argument('res', nargs='+', help='Resource(s) to update')

class MediaRenameOptHandler(DSSInteractHandler):
    """Rename an existing media"""
    label = 'rename'
    descr = ('for now, change only the library of an existing medium '
             '(only the DSS is modified)')

    @classmethod
    def add_options(cls, parser):
        """Add resource-specific options."""
        super(MediaRenameOptHandler, cls).add_options(parser)
        parser.add_argument('--library',
                            help="Library containing the medium to rename")
        parser.add_argument('--new-library',
                            help="New library for these medium(s)")
        parser.add_argument('res', nargs='+', help="Resource(s) to rename")

class DriveMigrateOptHandler(DSSInteractHandler):
    """Migrate an existing drive"""
    label = 'migrate'
    descr = ('migrate existing drive to another host or library '
             '(only the DSS is modified)')

    @classmethod
    def add_options(cls, parser):
        """Add resource-specific options."""
        super(DriveMigrateOptHandler, cls).add_options(parser)

        parser.add_argument('res', nargs='+', help='Drive(s) to update')
        parser.add_argument('--host', help='New host for these drives')
        parser.add_argument('--new-library',
                            help='New library for these drives')
        parser.add_argument('--library',
                            help="Library containing migrated drive(s) "
                                 "(This option is to target the good drive(s) "
                                 "among the existing libraries, not to set a "
                                 "new library.)")

class DriveScsiReleaseOptHandler(DSSInteractHandler):
    """Release the SCSI reservation of an existing drive"""
    label = 'scsi_release'
    descr = 'release the SCSI reservation of an existing drive'

    @classmethod
    def add_options(cls, parser):
        """Add resource-specific options."""
        super(DriveScsiReleaseOptHandler, cls).add_options(parser)
        parser.add_argument('res', nargs='+',
                            help="Drive(s) with a SCSI reservation to release"
                                 "and a mounted tape into.")
        parser.add_argument('--library',
                            help="Library containing the drives to release")

class FormatOptHandler(DSSInteractHandler):
    """Format a resource."""
    label = 'format'
    descr = 'format a media'

    @classmethod
    def add_options(cls, parser):
        """Add resource-specific options."""
        super(FormatOptHandler, cls).add_options(parser)
        parser.add_argument('-n', '--nb-streams', metavar='STREAMS', type=int,
                            default=0,
                            help='Max number of parallel formatting operations,'
                                 ' 0 means no limitation (default is 0)')
        parser.add_argument('--unlock', action='store_true',
                            help='Unlock media once it is ready to be written')
        parser.add_argument('--library',
                            help="Library containing added resources")
        parser.add_argument('res', nargs='+', help='Resource(s) to format')

class TapeFormatOptHandler(FormatOptHandler):
    """Format a tape."""
    descr = "format a tape"

    @classmethod
    def add_options(cls, parser):
        """Add resource-specific options."""
        super(TapeFormatOptHandler, cls).add_options(parser)
        parser.add_argument('--fs', default='ltfs',
                            choices=list(map(fs_type2str, FSType)),
                            type=uncase_fstype(list(map(fs_type2str, FSType))),
                            help='Filesystem type')
        parser.add_argument('--force', action='store_true',
                            help='Format the medium whatever its status')

class DirFormatOptHandler(FormatOptHandler):
    """Format a directory."""
    descr = "format a directory"

    @classmethod
    def add_options(cls, parser):
        """Add resource-specific options."""
        super(DirFormatOptHandler, cls).add_options(parser)
        parser.add_argument('--fs', default='POSIX',
                            choices=list(map(fs_type2str, FSType)),
                            type=uncase_fstype(list(map(fs_type2str, FSType))),
                            help='Filesystem type')

class RadosPoolFormatOptHandler(FormatOptHandler):
    """Format a RADOS pool."""
    descr = "format a RADOS pool"

    @classmethod
    def add_options(cls, parser):
        """Add resource-specific options."""
        super(RadosPoolFormatOptHandler, cls).add_options(parser)
        # invisible fs argument in help because it is not useful
        parser.add_argument('--fs', default='RADOS',
                            choices=list(map(fs_type2str, FSType)),
                            type=uncase_fstype(list(map(fs_type2str, FSType))),
                            help=argparse.SUPPRESS)

def handle_sort_option(params, resource, logger, **kwargs):
    """Handle the sort/rsort option"""

    if params.get('sort') and params.get('rsort'):
        logger.error("The option --sort and --rsort cannot be used together")
        sys.exit(os.EX_USAGE)

    for key in ['sort', 'rsort']:
        if params.get(key):
            if isinstance(resource, LayoutInfo):
                attrs_sort = list(resource.get_sort_fields().keys())
            else:
                attrs_sort = list(resource.get_display_dict().keys())

            sort_attr = params.get(key)
            if sort_attr not in attrs_sort:
                if isinstance(resource, LayoutInfo):
                    attrs = list(resource.get_display_dict().keys())
                    if sort_attr in attrs:
                        logger.error("Sorting attributes not supported: %s",
                                     sort_attr)
                        sys.exit(os.EX_USAGE)
                logger.error("Bad sorting attributes: %s", sort_attr)
                sys.exit(os.EX_USAGE)
            kwargs[key] = sort_attr
    return kwargs


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


def set_library(obj):
    """Set the library of obj first from its 'library' param, then its family"""
    obj.library = obj.params.get('library')
    if not obj.library:
        obj.library = cfg.get_default_library(obj.family)

class DeviceOptHandler(BaseResourceOptHandler):
    """Shared interface for devices."""
    verbs = [
        ResourceDeleteOptHandler,
        ListOptHandler,
        DeviceLockOptHandler,
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

class MediaOptHandler(BaseResourceOptHandler):
    """Shared interface for media."""
    verbs = [
        MediaAddOptHandler,
        MediaUpdateOptHandler,
        FormatOptHandler,
        MediaListOptHandler,
        LockOptHandler,
        UnlockOptHandler,
        MediaSetAccessOptHandler,
        MediumLocateOptHandler,
        ResourceDeleteOptHandler,
        MediaRenameOptHandler,
    ]
    library = None

    def add_medium(self, adm, medium, tags):
        """Add media method"""

    def add_medium_and_device(self):
        """Add a new medium and a new device at once"""
        resources = self.params.get('res')
        keep_locked = not self.params.get('unlock')
        tags = self.params.get('tags', [])
        set_library(self)
        valid_count = 0
        rc = 0

        try:
            with AdminClient(lrs_required=False) as adm:
                for path in resources:
                    medium_is_added = False

                    try:
                        medium = MediaInfo(family=self.family, name=path,
                                           model=None,
                                           is_adm_locked=keep_locked,
                                           library=self.library)

                        self.add_medium(adm, [medium], tags)
                        medium_is_added = True
                        adm.device_add(self.family, [path], False,
                                       self.library)
                        valid_count += 1
                    except EnvironmentError as err:
                        self.logger.error(env_error_format(err))
                        rc = (err.errno if not rc else rc)
                        if medium_is_added:
                            self.client.media.remove(self.family, path,
                                                     self.library)
                        continue

        except EnvironmentError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))

        return (rc, len(resources), valid_count)

    def exec_add(self):
        """Add new media."""
        names = NodeSet.fromlist(self.params.get('res'))
        fstype = self.params.get('fs').upper()
        techno = self.params.get('type', '').upper()
        tags = self.params.get('tags', [])
        keep_locked = not self.params.get('unlock')
        set_library(self)

        media = [MediaInfo(family=self.family, name=name,
                           model=techno, is_adm_locked=keep_locked,
                           library=self.library)
                 for name in names]
        try:
            with AdminClient(lrs_required=False) as adm:
                adm.medium_add(media, fstype, tags=tags)
            self.logger.info("Added %d media successfully", len(names))
        except EnvironmentError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))

    def del_medium(self, adm, family, resources, library):
        """Delete medium method"""

    def delete_medium_and_device(self):
        """Delete a medium and device at once"""
        resources = self.params.get('res')
        set_library(self)
        valid_count = 0
        rc = 0

        try:
            with AdminClient(lrs_required=False) as adm:
                for path in resources:
                    device_is_del = False
                    try:
                        rc = adm.device_delete(self.family, [path],
                                               self.library)
                        device_is_del = True
                        self.del_medium(adm, self.family, [path], self.library)
                        valid_count += 1
                    except EnvironmentError as err:
                        self.logger.error(env_error_format(err))
                        rc = (err.errno if not rc else rc)
                        if device_is_del:
                            adm.device_add(self.family, [path], False,
                                           self.library)
                        continue
        except EnvironmentError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))

        return (rc, len(resources), valid_count)

    def _media_update(self, media, fields):
        """Calling client.media.update"""
        try:
            self.client.media.update(media, fields)
            with AdminClient(lrs_required=False) as adm:
                adm.notify_media_update(media)

        except EnvironmentError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))

    def exec_update(self):
        """Update tags of an existing media"""
        set_library(self)
        tags = self.params.get('tags')
        if tags is None:
            self.logger.info("No update to be performed")
            return

        # Empty string clears tag and ''.split(',') == ['']
        if tags == ['']:
            tags = []

        uids = NodeSet.fromlist(self.params.get('res'))

        media = [MediaInfo(family=self.family, name=uid, tags=tags,
                           library=self.library)
                 for uid in uids]
        self._media_update(media, TAGS)

    def exec_format(self):
        """Format media however requested."""
        media_list = NodeSet.fromlist(self.params.get('res'))
        nb_streams = self.params.get('nb_streams')
        fs_type = self.params.get('fs')
        unlock = self.params.get('unlock')
        set_library(self)
        if self.family == ResourceFamily.RSC_TAPE:
            force = self.params.get('force')
        else:
            force = False

        try:
            with AdminClient(lrs_required=True) as adm:
                if unlock:
                    self.logger.debug("Post-unlock enabled")

                self.logger.info("Formatting media '%s'", media_list)
                if force:
                    self.logger.warning(
                        "This format may imply some orphan extents or lost "
                        "objects. Hoping you know what you are doing!")

                adm.fs_format(media_list, self.family, self.library,
                              nb_streams, fs_type, unlock=unlock, force=force)

            self.logger.info("Media '%s' have been formatted", media_list)

        except EnvironmentError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))

    def exec_list(self):
        """List media and display results."""
        attrs = list(MediaInfo().get_display_dict().keys())
        check_output_attributes(attrs, self.params.get('output'), self.logger)

        kwargs = {}
        if self.params.get('tags'):
            kwargs["tags"] = self.params.get('tags')

        if self.params.get('library'):
            kwargs["library"] = self.params.get('library')

        if self.params.get('status'):
            kwargs["adm_status"] = self.params.get('status')

        kwargs = handle_sort_option(self.params, MediaInfo(), self.logger,
                                    **kwargs)

        objs = []
        if self.params.get('res'):
            uids = NodeSet.fromlist(self.params.get('res'))
            for uid in uids:
                curr = self.client.media.get(family=self.family, id=uid,
                                             **kwargs)
                if not curr:
                    continue
                assert len(curr) == 1
                objs.append(curr[0])
        else:
            objs = list(self.client.media.get(family=self.family, **kwargs))

        if len(objs) > 0:
            dump_object_list(objs, attr=self.params.get('output'),
                             fmt=self.params.get('format'))

    def _set_adm_status(self, adm_status):
        """Update media.adm_status"""
        uids = NodeSet.fromlist(self.params.get('res'))
        media = [MediaInfo(family=self.family, name=uid, adm_status=adm_status,
                           library=self.library)
                 for uid in uids]
        self._media_update(media, ADM_STATUS)

    def exec_lock(self):
        """Lock media"""
        set_library(self)
        self._set_adm_status(PHO_RSC_ADM_ST_LOCKED)

    def exec_unlock(self):
        """Unlock media"""
        set_library(self)
        self._set_adm_status(PHO_RSC_ADM_ST_UNLOCKED)

    def exec_set_access(self):
        """Update media operations flags"""
        set_library(self)
        fields = 0
        flags = self.params.get('flags')
        if 'put' in flags:
            put_access = flags['put']
            fields += PUT_ACCESS
        else:
            put_access = True   # unused value

        if 'get' in flags:
            get_access = flags['get']
            fields += GET_ACCESS
        else:
            get_access = True   # unused value

        if 'delete' in flags:
            delete_access = flags['delete']
            fields += DELETE_ACCESS
        else:
            delete_access = True   # unused value

        uids = NodeSet.fromlist(self.params.get('res'))
        media = [MediaInfo(family=self.family, name=uid,
                           put_access=put_access, get_access=get_access,
                           delete_access=delete_access, library=self.library)
                 for uid in uids]
        self._media_update(media, fields)

    def exec_locate(self):
        """Locate a medium"""
        set_library(self)
        try:
            with AdminClient(lrs_required=False) as adm:
                print(adm.medium_locate(self.family, self.params.get('res'),
                                        self.library))

        except EnvironmentError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))

    def exec_import(self):
        """Import a medium"""
        media_names = NodeSet.fromlist(self.params.get('media'))
        check_hash = self.params.get('check_hash')
        adm_locked = not self.params.get('unlock')
        techno = self.params.get('type', '').upper()
        fstype = self.params.get('fs').upper()
        set_library(self)

        media = (MediaInfo * len(media_names))()
        for index, medium_name in enumerate(media_names):
            media[index] = MediaInfo(family=self.family, name=medium_name,
                                     model=techno, is_adm_locked=adm_locked,
                                     library=self.library)
        try:
            with AdminClient(lrs_required=True) as adm:
                adm.medium_import(fstype, media, check_hash)
        except EnvironmentError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))

    def exec_repack(self):
        """Repack a medium"""
        set_library(self)
        try:
            with AdminClient(lrs_required=True) as adm:
                tags = self.params.get('tags', [])
                adm.repack(self.family, self.params.get('res'), self.library,
                           tags)
        except EnvironmentError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))

    def exec_delete(self):
        """Delete a medium"""
        resources = self.params.get('res')
        set_library(self)

        try:
            with AdminClient(lrs_required=False) as adm:
                num_deleted, num2delete = adm.medium_delete(self.family,
                                                            resources,
                                                            self.library)
        except EnvironmentError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))

        if num_deleted == num2delete:
            self.logger.info("Deleted %d media(s) successfully", num_deleted)
        elif num_deleted == 0:
            self.logger.error("No media deleted: %d/%d", num_deleted,
                              num2delete)
        else:
            self.logger.warning("Failed to delete %d/%d media(s)",
                                num2delete - num_deleted, num2delete)

    def exec_rename(self):
        """Rename a medium"""
        resources = self.params.get('res')
        new_lib = self.params.get('new_library')
        set_library(self)

        if new_lib is None:
            self.logger.info("No rename to be performed")
            return

        try:
            with AdminClient(lrs_required=False) as adm:
                count = adm.medium_rename(self.family, resources, self.library,
                                          new_lib)
        except EnvironmentError as err:
            self.logger.error("%s", env_error_format(err))
            sys.exit(abs(err.errno))

        if count > 0:
            self.logger.info("Rename %d media(s) successfully", count)

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

class DirOptHandler(MediaOptHandler):
    """Directory-related options and actions."""
    label = 'dir'
    descr = 'handle directories'
    family = ResourceFamily(ResourceFamily.RSC_DIR)
    verbs = [
        MediaAddOptHandler,
        MediaUpdateOptHandler,
        DirFormatOptHandler,
        MediaListOptHandler,
        LockOptHandler,
        UnlockOptHandler,
        DirSetAccessOptHandler,
        MediumLocateOptHandler,
        ResourceDeleteOptHandler,
        MediaRenameOptHandler,
    ]

    def add_medium(self, adm, medium, tags):
        adm.medium_add(medium, 'POSIX', tags=tags)

    def exec_add(self):
        """
        Add a new directory.
        Note that this is a special case where we add both a media (storage) and
        a device (mean to access it).
        """
        rc, nb_dev_to_add, nb_dev_added = self.add_medium_and_device()

        if nb_dev_added == nb_dev_to_add:
            self.logger.info("Added %d dir(s) successfully", nb_dev_added)
        else:
            self.logger.error("Failed to add %d/%d dir(s)",
                              nb_dev_to_add - nb_dev_added, nb_dev_to_add)
            sys.exit(abs(rc))

    def del_medium(self, adm, family, resources, library):
        adm.medium_delete(family, resources, library)

    def exec_delete(self):
        """
        Delete a directory
        Note that this is a special case where we delete both a media (storage)
        and a device (mean to access it).
        """
        rc, nb_dev_to_del, nb_dev_del = self.delete_medium_and_device()

        if nb_dev_del == nb_dev_to_del:
            self.logger.info("Deleted %d dir(s) successfully", nb_dev_del)
        else:
            self.logger.error("Failed to delete %d/%d dir(s)",
                              nb_dev_to_del - nb_dev_del, nb_dev_to_del)
            sys.exit(abs(rc))

class DriveStatus(CLIManagedResourceMixin): #pylint: disable=too-many-instance-attributes
    """Wrapper class to use dump_object_list"""

    def __init__(self, values=None):
        if not values:
            return

        self.name = values.get("name", "")
        self.library = values.get("library", "")
        self.device = values.get("device", "")
        self.serial = values.get("serial", "")
        self.address = values.get("address", "")
        self.mount_path = values.get("mount_path", "")
        self.media = values.get("media", "")
        self.ongoing_io = values.get("ongoing_io", "")
        self.currently_dedicated_to = values.get("currently_dedicated_to", "")

    def get_display_fields(self, max_width=None):
        """Return a dict of available fields and optional display formatters."""
        return {
            'name': None,
            'library': None,
            'device': None,
            'address': None,
            'serial': None,
            'mount_path': None,
            'media': None,
            'ongoing_io': None,
            'currently_dedicated_to': None,
        }

class DriveLookupOptHandler(BaseOptHandler):
    """Drive lookp sub-parser"""

    label = 'lookup'
    descr = 'lookup a drive from library'

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        pass

    @classmethod
    def add_options(cls, parser):
        """Add resource-specific options."""
        super(DriveLookupOptHandler, cls).add_options(parser)
        parser.add_argument('res',
                            help='Drive to lookup (could be '
                                 'the path or the serial number)')
        parser.add_argument('--library',
                            help="Library containing the drive to lookup")

class DriveLoadOptHandler(BaseOptHandler):
    """Drive load sub-parser"""

    label = 'load'
    descr = 'load a tape into a drive'

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        pass

    @classmethod
    def add_options(cls, parser):
        """Add resource-specific options."""
        super(DriveLoadOptHandler, cls).add_options(parser)
        parser.add_argument('res',
                            help='Target drive (could be the path or the '
                                 'serial number)')
        parser.add_argument('tape_label', help='Tape label to load')
        parser.add_argument('--library',
                            help="Library containing the target drive and tape")

class DriveUnloadOptHandler(BaseOptHandler):
    """Drive unload sub-parser"""

    label = 'unload'
    descr = 'unload a tape from a drive'

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        pass

    @classmethod
    def add_options(cls, parser):
        """Add resource-specific options."""
        super(DriveUnloadOptHandler, cls).add_options(parser)
        parser.add_argument('res',
                            help='Target drive (could be the path or the '
                                 'serial number)')
        parser.add_argument('--tape-label',
                            help='Tape label to unload (unload is done only if '
                                 'the drive contains this label, any tape is '
                                 'unloaded if this option is not set)')
        parser.add_argument('--library',
                            help="Library containing the target drive")

class DriveOptHandler(DeviceOptHandler):
    """Tape Drive options and actions."""
    label = 'drive'
    descr = 'handle tape drives (use ID or device path to identify resource)'
    family = ResourceFamily(ResourceFamily.RSC_TAPE)
    verbs = [
        AddOptHandler,
        DriveMigrateOptHandler,
        ResourceDeleteOptHandler,
        DriveListOptHandler,
        DeviceLockOptHandler,
        UnlockOptHandler,
        StatusOptHandler,
        DriveLookupOptHandler,
        DriveLoadOptHandler,
        DriveUnloadOptHandler,
        DriveScsiReleaseOptHandler,
    ]

    library = None

    def exec_scsi_release(self):
        """Release SCSI reservation of given drive(s)"""
        resources = self.params.get('res')
        set_library(self)

        try:
            with AdminClient(lrs_required=False) as adm:
                count = adm.device_scsi_release(resources, self.library)

        except EnvironmentError as err:
            self.logger.error("%s", env_error_format(err))
            sys.exit(abs(err.errno))

        if count > 0:
            self.logger.info("Release SCSI reservation of %d drive(s)"
                             " successfully", count)

    def exec_migrate(self):
        """Migrate devices host"""
        resources = self.params.get('res')
        host = self.params.get('host')
        new_lib = self.params.get('new_library')
        set_library(self)

        if host is None and new_lib is None:
            self.logger.info("No migrate to be performed")
            return

        try:
            with AdminClient(lrs_required=False) as adm:
                count = adm.device_migrate(resources, self.library, host,
                                           new_lib)

        except EnvironmentError as err:
            self.logger.error("%s", env_error_format(err))
            sys.exit(abs(err.errno))

        if count > 0:
            self.logger.info("Migrated %d device(s) successfully", count)

    def exec_list(self):
        """List devices and display results."""
        attrs = list(DevInfo().get_display_dict().keys())
        check_output_attributes(attrs, self.params.get('output'), self.logger)

        kwargs = {}
        if self.params.get('model'):
            kwargs['model'] = self.params.get('model')

        if self.params.get('library'):
            kwargs['library'] = self.params.get('library')

        if self.params.get('status'):
            kwargs["adm_status"] = self.params.get('status')

        kwargs = handle_sort_option(self.params, DevInfo(), self.logger,
                                    **kwargs)

        objs = []
        if self.params.get('res'):
            for serial in self.params.get('res'):
                curr = self.filter(serial, **kwargs)
                if not curr:
                    continue
                assert len(curr) == 1
                objs.append(curr[0])
        else:
            objs = list(self.client.devices.get(family=self.family, **kwargs))

        if len(objs) > 0:
            dump_object_list(objs, attr=self.params.get('output'),
                             fmt=self.params.get('format'))

    def exec_status(self):
        """Display I/O and drive status"""
        try:
            with AdminClient(lrs_required=True) as adm:
                status = json.loads(adm.device_status(PHO_RSC_TAPE))
                # disable pylint's warning as it's suggestion does not work
                for i in range(len(status)): #pylint: disable=consider-using-enumerate
                    status[i] = DriveStatus(status[i])

                dump_object_list(sorted(status, key=lambda x: x.address),
                                 self.params.get('output'))

        except EnvironmentError as err:
            self.logger.error("Cannot read status of drives: %s",
                              env_error_format(err))
            sys.exit(abs(err.errno))

    def exec_delete(self):
        """Remove a device"""
        resources = self.params.get('res')
        set_library(self)

        try:
            with AdminClient(lrs_required=False) as adm:
                count = adm.device_delete(self.family, resources, self.library)
        except EnvironmentError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))

        self.logger.info("Removed %d device(s) successfully", count)

    def exec_lookup(self):
        """Lookup drive information from the library."""
        res = self.params.get('res')
        set_library(self)

        try:
            with AdminClient(lrs_required=False) as adm:
                drive_info = adm.drive_lookup(res, self.library)

        except EnvironmentError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))

        relativ_address = drive_info.ldi_addr.lia_addr - \
                          drive_info.ldi_first_addr
        print("Drive %d: address %s" % (relativ_address,
                                        hex(drive_info.ldi_addr.lia_addr)))
        print("State: %s" % ("full" if drive_info.ldi_full else "empty"))

        if drive_info.ldi_full:
            print("Loaded tape id: %s" % (drive_info.ldi_medium_id.name))

    def exec_load(self):
        """Load a tape into a drive"""
        res = self.params.get('res')
        tape_label = self.params.get('tape_label')
        set_library(self)

        try:
            with AdminClient(lrs_required=False) as adm:
                adm.load(res, tape_label, self.library)

        except EnvironmentError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))

    def exec_unload(self):
        """Unload a tape from a drive"""
        res = self.params.get('res')
        tape_label = self.params.get('tape_label')
        set_library(self)

        try:
            with AdminClient(lrs_required=False) as adm:
                adm.unload(res, tape_label, self.library)

        except EnvironmentError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))

class TapeOptHandler(MediaOptHandler):
    """Magnetic tape options and actions."""
    label = 'tape'
    descr = 'handle magnetic tape (use tape label to identify resource)'
    family = ResourceFamily(ResourceFamily.RSC_TAPE)
    verbs = [
        TapeAddOptHandler,
        MediaUpdateOptHandler,
        TapeFormatOptHandler,
        MediaListOptHandler,
        LockOptHandler,
        UnlockOptHandler,
        TapeSetAccessOptHandler,
        MediumLocateOptHandler,
        TapeImportOptHandler,
        RepackOptHandler,
        ResourceDeleteOptHandler,
        MediaRenameOptHandler,
    ]

class RadosPoolOptHandler(MediaOptHandler):
    """RADOS pool options and actions."""
    label = 'rados_pool'
    descr = 'handle RADOS pools'
    family = ResourceFamily(ResourceFamily.RSC_RADOS_POOL)
    verbs = [
        MediaAddOptHandler,
        MediaListOptHandler,
        MediaUpdateOptHandler,
        LockOptHandler,
        UnlockOptHandler,
        RadosPoolSetAccessOptHandler,
        RadosPoolFormatOptHandler,
        MediumLocateOptHandler,
        ResourceDeleteOptHandler,
        MediaRenameOptHandler,
    ]

    def add_medium(self, adm, medium, tags):
        adm.medium_add(medium, 'RADOS', tags=tags)

    def exec_add(self):
        """
        Add a new RADOS pool.
        Note that this is a special case where we add both a media (storage) and
        a device (mean to access it).
        """
        (rc, nb_dev_to_add, nb_dev_added) = self.add_medium_and_device()
        if nb_dev_added == nb_dev_to_add:
            self.logger.info("Added %d RADOS pool(s) successfully",
                             nb_dev_added)
        else:
            self.logger.error("Failed to add %d/%d RADOS pools",
                              nb_dev_to_add - nb_dev_added, nb_dev_to_add)
            sys.exit(abs(rc))

    def del_medium(self, adm, family, resources, library):
        adm.medium_delete(family, resources, library)

    def exec_delete(self):
        """
        Delete a RADOS pool.
        Note that this is a special case where we delete both a media (storage)
        and a device (mean to access it).
        """
        rc, nb_dev_to_del, nb_dev_del = self.delete_medium_and_device()

        if nb_dev_del == nb_dev_to_del:
            self.logger.info("Deleted %d RADOS pool(s) successfully",
                             nb_dev_del)
        else:
            self.logger.error("Failed to delete %d/%d RADOS pools",
                              nb_dev_to_del - nb_dev_del, nb_dev_to_del)
            sys.exit(abs(rc))

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
            try:
                lib_dev_path_from_cfg = cfg.get_val("lrs", "lib_device")
                libs.append(lib_dev_path_from_cfg)
            except KeyError as err:
                self.logger.error("%s, will abort 'lib scan'", err)
                sys.exit(os.EX_DATAERR)

        with_refresh = self.params.get('refresh')

        try:
            with AdminClient(lrs_required=False) as adm:
                for lib_dev in libs:
                    lib_data = adm.lib_scan(PHO_LIB_SCSI, lib_dev, with_refresh)
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
            try:
                lib_dev_path_from_cfg = cfg.get_val("lrs", "lib_device")
                libs.append(lib_dev_path_from_cfg)
            except KeyError as err:
                self.logger.error("%s, will abort 'lib refresh'", err)
                sys.exit(os.EX_DATAERR)

        try:
            with AdminClient(lrs_required=False) as adm:
                for lib_dev in libs:
                    adm.lib_refresh(PHO_LIB_SCSI, lib_dev)
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
