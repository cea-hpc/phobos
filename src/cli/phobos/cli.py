#!/usr/bin/env python3
# pylint: disable=too-many-lines

#
#  All rights reserved (c) 2014-2022 CEA/DAM.
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
import errno
import json
import logging
from logging.handlers import SysLogHandler
from shlex import shlex
import sys

import os
import os.path

from abc import ABCMeta, abstractmethod

from ClusterShell.NodeSet import NodeSet

from phobos.core.admin import Client as AdminClient
import phobos.core.cfg as cfg
from phobos.core.const import (PHO_LIB_SCSI, rsc_family2str, # pylint: disable=no-name-in-module
                               PHO_RSC_ADM_ST_LOCKED, PHO_RSC_ADM_ST_UNLOCKED,
                               ADM_STATUS, TAGS, PUT_ACCESS, GET_ACCESS,
                               DELETE_ACCESS, PHO_RSC_TAPE)
from phobos.core.dss import Client as DSSClient
from phobos.core.ffi import (DeprecatedObjectInfo, DevInfo, LayoutInfo,
                             MediaInfo, ObjectInfo, ResourceFamily,
                             CLIManagedResourceMixin)
from phobos.core.log import LogControl, DISABLED, WARNING, INFO, VERBOSE, DEBUG
from phobos.core.store import XferClient, UtilClient, attrs_as_dict, PutParams
from phobos.output import dump_object_list

def phobos_log_handler(log_record):
    """
    Receive log records emitted from lower layers and inject them into the
    currently configured logger.
    """
    rec = log_record.contents
    msg = rec.plr_msg

    # Append ': <errmsg>' to the original message if err_code was set
    if rec.plr_err != 0:
        msg += ": %s"
        args = (os.strerror(abs(rec.plr_err)), )
    else:
        args = tuple()

    level = LogControl.level_pho2py(rec.plr_level)

    attrs = {
        'name': 'internals',
        'levelno': level,
        'levelname': LogControl.level_name(level),
        'process': rec.plr_pid,
        'filename': rec.plr_file,
        'funcName': rec.plr_func,
        'lineno': rec.plr_line,
        'exc_info': None,
        'msg': msg,
        'args': args,
        'created': rec.plr_time.tv_sec,
    }

    record = logging.makeLogRecord(attrs)
    logger = logging.getLogger(__name__)
    logger.handle(record)

def env_error_format(exc):
    """Return a human readable representation of an environment exception."""
    if exc.errno and exc.strerror:
        return "%s (%s)" % (exc.strerror, os.strerror(abs(exc.errno)))
    elif exc.errno:
        return "%s (%s)" % (os.strerror(abs(exc.errno)), abs(exc.errno))
    elif exc.strerror:
        return exc.strerror

    return ""

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
        raise ValueError("Invalid number of values, only 3 expected "
                         "(src_file, oid, metadata).")

    return file_entry


class BaseOptHandler(object):
    """
    Skeleton for action handlers. It can register a corresponding argument
    subparser to a top-level one, with targeted object, description and
    supported actions.
    """
    label = '(undef)'
    descr = '(undef)'
    alias = []
    epilog = None
    verbs = []

    __metaclass__ = ABCMeta

    def __init__(self, params, **kwargs):
        """
        Initialize action handler with command line parameters. These are to be
        re-checked later by the specialized chk_* methods.
        """
        super(BaseOptHandler, self).__init__(**kwargs)
        self.params = params
        self.logger = logging.getLogger(__name__)

    @abstractmethod
    def __enter__(self):
        """
        Optional method handlers can implement to prepare execution context.
        """

    @abstractmethod
    def __exit__(self, exc_type, exc_value, traceback):
        """
        Optional method handlers can implement to release resources after
        execution.
        """

    @classmethod
    def add_options(cls, parser):
        """Add options for this specific command-line subsection."""

    @classmethod
    def subparser_register(cls, base_parser):
        """Register the subparser to a top-level one."""
        subparser = base_parser.add_parser(cls.label, help=cls.descr,
                                           epilog=cls.epilog,
                                           aliases=cls.alias)

        # Register options relating to the current media
        cls.add_options(subparser)

        # Register supported verbs and associated options
        if cls.verbs:
            v_parser = subparser.add_subparsers(dest='verb')
            v_parser.required = True
            for verb in cls.verbs:
                verb.subparser_register(v_parser)

        return subparser


class DSSInteractHandler(BaseOptHandler):
    """Option handler for actions that interact with the DSS."""
    def __init__(self, params, **kwargs):
        """Initialize a new instance."""
        super(DSSInteractHandler, self).__init__(params, **kwargs)
        self.client = None

    def __enter__(self):
        """Initialize a DSS Client."""
        self.client = DSSClient()
        self.client.connect()
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        """Release resources associated to a DSS handle."""
        self.client.disconnect()


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
        itm = attrs_as_dict(xfr.contents.xd_attrs)
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


class StoreGenericPutHandler(XferOptHandler):
    """Base class for common options between put and mput"""

    @classmethod
    def add_options(cls, parser):
        super(StoreGenericPutHandler, cls).add_options(parser)
        # The type argument allows to transform 'a,b,c' into ['a', 'b', 'c'] at
        # parse time rather than post processing it
        parser.add_argument('-T', '--tags', type=lambda t: t.split(','),
                            help='Only use media that contain all these tags '
                                 '(comma-separated: foo,bar)')
        parser.add_argument('-f', '--family',
                            choices=list(map(rsc_family2str, ResourceFamily)),
                            help='Targeted storage family')
        parser.add_argument('-l', '--layout', choices=["raid1"],
                            help='Desired storage layout')
        parser.add_argument('-a', '--alias',
                            help='Desired alias for family, tags and layout. '
                            'Specifically set family and layout supersede the '
                            'alias, tags are joined.')
        parser.add_argument('-p', '--lyt-params',
                            help='Comma-separated list of key=value for layout '
                            'specific parameters.')


class StorePutHandler(StoreGenericPutHandler):
    """Insert objects into backend."""
    label = 'put'
    descr = 'insert object into backend'

    @classmethod
    def add_options(cls, parser):
        """Add options for the PUT command."""
        super(StorePutHandler, cls).add_options(parser)
        parser.add_argument('-m', '--metadata',
                            help='Comma-separated list of key=value')
        parser.add_argument('--overwrite', action='store_true',
                            help='Allow object update')
        parser.add_argument('src_file', help='File to insert')
        parser.add_argument('object_id', help='Desired object ID')

    def exec_put(self):
        """Insert an object into backend."""
        src = self.params.get('src_file')
        oid = self.params.get('object_id')

        attrs = self.params.get('metadata')
        if attrs is not None:
            attrs = attr_convert(attrs)
            self.logger.debug("Loaded attributes set %r", attrs)

        lyt_attrs = self.params.get('lyt_params')
        if lyt_attrs is not None:
            lyt_attrs = attr_convert(lyt_attrs)
            self.logger.debug("Loaded layout params set %r", lyt_attrs)

        put_params = PutParams(alias=self.params.get('alias'),
                               family=self.params.get('family'),
                               layout=self.params.get('layout'),
                               lyt_params=lyt_attrs,
                               overwrite=self.params.get('overwrite'),
                               tags=self.params.get('tags', []))

        self.logger.debug("Inserting object '%s' to 'objid:%s'", src, oid)

        self.client.put_register(oid, src, attrs=attrs, put_params=put_params)
        try:
            self.client.run()
        except IOError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))


class StoreMPutHandler(StoreGenericPutHandler):
    """Insert objects into backend."""
    label = 'mput'
    descr = 'insert multiple objects into backend'

    @classmethod
    def add_options(cls, parser):
        """Add options for the MPUT command."""
        super(StoreMPutHandler, cls).add_options(parser)
        parser.add_argument('xfer_list',
                            help='File containing lines like: '\
                                 '<src_file>  <object_id>  <metadata|->')

    def exec_mput(self):
        """Insert objects into backend."""
        path = self.params.get('xfer_list')
        if path == '-':
            fin = sys.stdin
        else:
            fin = open(path)

        lyt_attrs = self.params.get('lyt_params')
        if lyt_attrs is not None:
            lyt_attrs = attr_convert(lyt_attrs)
            self.logger.debug("Loaded layout params set %r", lyt_attrs)

        put_params = PutParams(alias=self.params.get('alias'),
                               family=self.params.get('family'),
                               layout=self.params.get('layout'),
                               lyt_params=lyt_attrs,
                               overwrite=self.params.get('overwrite'),
                               tags=self.params.get('tags', []))

        for i, line in enumerate(fin):
            # Skip empty lines and comments
            line = line.strip()
            if not line or line.startswith('#'):
                continue

            try:
                match = mput_file_line_parser(line)
            except ValueError:
                self.logger.error("Format error on line %d: %s", i + 1, line)
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
            self.client.put_register(oid, src, attrs=attrs,
                                     put_params=put_params)

        if fin is not sys.stdin:
            fin.close()

        try:
            self.client.run()
        except IOError as err:
            self.logger.error("Failed to MPUT: %s",
                              env_error_format(err))
            sys.exit(abs(err.errno))

class AddOptHandler(DSSInteractHandler):
    """Insert a new resource into the system."""
    label = 'add'
    descr = 'insert new resource(s) to the system'

    @classmethod
    def add_options(cls, parser):
        super(AddOptHandler, cls).add_options(parser)
        parser.add_argument('--unlock', action='store_true',
                            help='Unlock resource on success')
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

    @classmethod
    def add_options(cls, parser):
        super(ResourceDeleteOptHandler, cls).add_options(parser)
        parser.add_argument('res', nargs='+', help='Resource(s) to remove')
        parser.set_defaults(verb=cls.label)

class CheckOptHandler(DSSInteractHandler):
    """Issue a check command on the designated object(s)."""
    label = 'check'
    descr = 'check state / consistency of the selected resource(s).'

    @classmethod
    def add_options(cls, parser):
        """Add resource-specific options."""
        super(CheckOptHandler, cls).add_options(parser)
        parser.add_argument('res', nargs='+', help='Resource(s) to check')


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
        pass

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

class ScanOptHandler(BaseOptHandler):
    """Scan a physical resource and display retrieved information."""
    label = 'scan'
    descr = 'scan a physical resource and display retrieved information'

    def __enter__(self):
        pass

    def __exit__(self, exc_type, exc_value, traceback):
        pass

    @classmethod
    def add_options(cls, parser):
        """Add resource-specific options."""
        super(ScanOptHandler, cls).add_options(parser)
        parser.add_argument('res', nargs='*',
                            help="Resource(s) or device(s) to scan. If not "
                                 "given, will fetch 'lib_device' path in "
                                 "configuration file instead")

class DriveListOptHandler(ListOptHandler):
    """
    Specific version of the 'list' command for tape drives, with a couple
    extra-options.
    """

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

class MediaListOptHandler(ListOptHandler):
    """
    Specific version of the 'list' command for media, with a couple
    extra-options.
    """

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
        parser.set_defaults(verb=cls.label)

    def exec_delete(self):
        """Delete objects."""
        client = UtilClient()
        try:
            client.object_delete(self.params.get('oids'))
        except EnvironmentError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))

class UuidOptHandler(BaseOptHandler):
    """Handler to select objects with a list of uuids"""

    label = 'uuid'
    descr = 'select uuids'

    def __enter__(self):
        pass

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
        pass

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
            print(client.object_locate(self.params.get('oid'),
                                       self.params.get('uuid'),
                                       self.params.get('version'),
                                       self.params.get('focus-host')))
        except EnvironmentError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))

class ExtentListOptHandler(ListOptHandler):
    """
    Specific version of the 'list' command for extent, with a couple
    extra-options.
    """

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

class TapeAddOptHandler(MediaAddOptHandler):
    """Specific version of the 'add' command for tapes, with extra-options."""
    @classmethod
    def add_options(cls, parser):
        """Add resource-specific options."""
        super(TapeAddOptHandler, cls).add_options(parser)
        parser.add_argument('-t', '--type', required=True,
                            help='tape technology')
        parser.add_argument('--fs', default="LTFS",
                            help='Filesystem type (default: LTFS)')

class MediumLocateOptHandler(DSSInteractHandler):
    """Locate a medium into the system."""
    label = 'locate'
    descr = 'locate a medium'

    @classmethod
    def add_options(cls, parser):
        super(MediumLocateOptHandler, cls).add_options(parser)
        parser.add_argument('res', help='medium to locate')

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

class DriveMigrateOptHandler(DSSInteractHandler):
    """Migrate an existing drive"""
    label = 'migrate'
    descr = 'migrate existing drive to another host'

    @classmethod
    def add_options(cls, parser):
        """Add resource-specific options."""
        super(DriveMigrateOptHandler, cls).add_options(parser)
        parser.add_argument('host', help='New host for these drives')
        parser.add_argument('res', nargs='+', help='Resource(s) to update')

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
        parser.add_argument('res', nargs='+', help='Resource(s) to format')

class TapeFormatOptHandler(FormatOptHandler):
    """Format a tape."""

    @classmethod
    def add_options(cls, parser):
        """Add resource-specific options."""
        super(TapeFormatOptHandler, cls).add_options(parser)
        parser.add_argument('--fs', default='ltfs', help='Filesystem type')

class DirFormatOptHandler(FormatOptHandler):
    """Format a directory."""

    @classmethod
    def add_options(cls, parser):
        """Add resource-specific options."""
        super(DirFormatOptHandler, cls).add_options(parser)
        parser.add_argument('--fs', default='posix', help='Filesystem type')

class RadosPoolFormatOptHandler(FormatOptHandler):
    """Format a RADOS pool."""

    @classmethod
    def add_options(cls, parser):
        """Add resource-specific options."""
        super(RadosPoolFormatOptHandler, cls).add_options(parser)
        # invisible fs argument in help because it is not useful
        parser.add_argument('--fs', default='RADOS', help=argparse.SUPPRESS)

class BaseResourceOptHandler(DSSInteractHandler):
    """Generic interface for resources manipulation."""
    label = None
    descr = None
    family = None
    verbs = []

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
        attrs.extend(['*', 'all'])
        out_attrs = self.params.get('output')
        bad_attrs = set(out_attrs).difference(set(attrs))
        if bad_attrs:
            self.logger.error("Bad output attributes: %s", " ".join(bad_attrs))
            sys.exit(os.EX_USAGE)

        metadata = []
        if self.params.get('metadata'):
            metadata = self.params.get('metadata')
            for elt in metadata:
                if '=' not in elt:
                    self.logger.error("Metadata parameter '%s' must be a "
                                      "'key=value'", elt)
                    sys.exit(os.EX_USAGE)

        client = UtilClient()

        try:
            objs = client.object_list(self.params.get('res'),
                                      self.params.get('pattern'),
                                      metadata,
                                      self.params.get('deprecated'))

            if objs:
                max_width = (None if self.params.get('no_trunc')
                             else self.params.get('max_width'))

                dump_object_list(objs, attr=out_attrs, max_width=max_width,
                                 fmt=self.params.get('format'))

            client.list_free(objs, len(objs))
        except EnvironmentError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))

class DeviceOptHandler(BaseResourceOptHandler):
    """Shared interface for devices."""
    verbs = [
        AddOptHandler,
        ResourceDeleteOptHandler,
        ListOptHandler,
        DeviceLockOptHandler,
        UnlockOptHandler
    ]

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

        try:
            with AdminClient(lrs_required=False) as adm:
                adm.device_add(self.family, resources,
                               not self.params.get('unlock'))

        except EnvironmentError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))

        self.logger.info("Added %d device(s) successfully", len(resources))

    def exec_lock(self):
        """Device lock"""
        names = self.params.get('res')

        try:
            with AdminClient(lrs_required=False) as adm:
                adm.device_lock(self.family, names, self.params.get('wait'))

        except EnvironmentError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))

        self.logger.info("%d device(s) locked", len(names))

    def exec_unlock(self):
        """Device unlock"""
        names = self.params.get('res')

        try:
            with AdminClient(lrs_required=False) as adm:
                adm.device_unlock(self.family, names, self.params.get('force'))

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
        MediumLocateOptHandler
    ]

    def add_medium(self, medium, tags):
        """Add media method"""
        pass

    def add_medium_and_device(self):
        """Add a new medium and a new device at once"""
        resources = self.params.get('res')
        keep_locked = not self.params.get('unlock')
        tags = self.params.get('tags', [])
        valid_count = 0
        rc = 0

        try:
            with AdminClient(lrs_required=False) as adm:
                for path in resources:
                    # Remove any trailing slash
                    path = path.rstrip('/')
                    medium_is_added = False

                    try:
                        medium = MediaInfo(family=self.family, name=path,
                                           model=None,
                                           is_adm_locked=keep_locked)
                        self.add_medium(medium, tags=tags)
                        medium_is_added = True
                        adm.device_add(self.family, [path], False)
                        valid_count += 1
                    except EnvironmentError as err:
                        self.logger.error(env_error_format(err))
                        rc = (err.errno if not rc else rc)
                        if medium_is_added:
                            self.client.media.remove(self.family, path)
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

        for med in names:
            try:
                medium = MediaInfo(family=self.family, name=med, model=techno,
                                   is_adm_locked=keep_locked)
                self.client.media.add(medium, fstype, tags=tags)
            except EnvironmentError as err:
                self.logger.error(env_error_format(err))
                sys.exit(abs(err.errno))

        self.logger.info("Added %d media successfully", len(names))

    def _media_update(self, media, fields):
        """Calling client.media.update"""
        try:
            self.client.media.update(media, fields)
        except EnvironmentError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))

    def exec_update(self):
        """Update tags of an existing media"""
        tags = self.params.get('tags')
        if tags is None:
            self.logger.info("No update to be performed")
            return

        # Empty string clears tag and ''.split(',') == ['']
        if tags == ['']:
            tags = []

        uids = NodeSet.fromlist(self.params.get('res'))
        media = [MediaInfo(family=self.family, name=uid, tags=tags)
                 for uid in uids]
        self._media_update(media, TAGS)

    def exec_format(self):
        """Format media however requested."""
        media_list = NodeSet.fromlist(self.params.get('res'))
        nb_streams = self.params.get('nb_streams')
        fs_type = self.params.get('fs')
        unlock = self.params.get('unlock')

        try:
            with AdminClient(lrs_required=True) as adm:
                if unlock:
                    self.logger.debug("Post-unlock enabled")

                self.logger.info("Formatting media '%s'", media_list)
                adm.fs_format(media_list, nb_streams, fs_type, unlock=unlock)

        except EnvironmentError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))

    def exec_list(self):
        """List media and display results."""
        attrs = list(MediaInfo().get_display_dict().keys())
        attrs.extend(['*', 'all'])
        out_attrs = self.params.get('output')
        bad_attrs = set(out_attrs).difference(set(attrs))
        if bad_attrs:
            self.logger.error("Bad output attributes: %s", " ".join(bad_attrs))
            sys.exit(os.EX_USAGE)

        kwargs = {}
        if self.params.get('tags'):
            kwargs["tags"] = self.params.get('tags')

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
        media = [MediaInfo(family=self.family, name=uid,
                           adm_status=adm_status)
                 for uid in uids]
        self._media_update(media, ADM_STATUS)

    def exec_lock(self):
        """Lock media"""
        self._set_adm_status(PHO_RSC_ADM_ST_LOCKED)

    def exec_unlock(self):
        """Unlock media"""
        self._set_adm_status(PHO_RSC_ADM_ST_UNLOCKED)

    def exec_set_access(self):
        """Update media operations flags"""
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
                           delete_access=delete_access)
                 for uid in uids]
        self._media_update(media, fields)

    def exec_locate(self):
        """Locate a medium"""
        try:
            with AdminClient(lrs_required=False) as adm:
                print(adm.medium_locate(self.family, self.params.get('res')))

        except EnvironmentError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))

class PingOptHandler(BaseOptHandler):
    """Ping phobos daemon"""
    label = 'ping'
    descr = 'ping the phobos daemon'

    @classmethod
    def add_options(cls, parser):
        """Add ping option to the parser"""
        super(PingOptHandler, cls).add_options(parser)
        parser.set_defaults(verb=cls.label)

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        pass

    def exec_ping(self):
        """Ping the daemon to check if it is online."""
        try:
            with AdminClient(lrs_required=True) as adm:
                adm.ping()

        except EnvironmentError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))

        self.logger.info("Ping sent to daemon successfully")

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
    ]

    def add_medium(self, medium, tags):
        self.client.media.add(medium, 'POSIX', tags=tags)

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

class DriveStatus(CLIManagedResourceMixin):
    """Wrapper class to use dump_object_list"""

    def __init__(self, values=None):
        if not values:
            return

        self.name = values.get("name", "")
        self.device = values.get("device", "")
        self.serial = values.get("serial", "")
        self.address = values.get("address", "")
        self.mount_path = values.get("mount_path", "")
        self.media = values.get("media", "")
        self.ongoing_io = values.get("ongoing_io", "")

    def get_display_fields(self, max_width=None):
        """Return a dict of available fields and optional display formatters."""
        return {
            'name': None,
            'device': None,
            'address': None,
            'serial': None,
            'mount_path': None,
            'media': None,
            'ongoing_io': None,
        }


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
    ]

    def exec_migrate(self):
        """Migrate devices host"""
        resources = self.params.get('res')
        host = self.params.get('host')

        try:
            with AdminClient(lrs_required=False) as adm:
                count = adm.device_migrate(resources, host)

        except EnvironmentError as err:
            self.logger.error("%s", env_error_format(err))
            sys.exit(abs(err.errno))

        if count > 0:
            self.logger.info("Migrated %d device(s) successfully", count)

    def exec_list(self):
        """List devices and display results."""
        attrs = list(DevInfo().get_display_dict().keys())
        attrs.extend(['*', 'all'])
        out_attrs = self.params.get('output')
        bad_attrs = set(out_attrs).difference(set(attrs))
        if bad_attrs:
            self.logger.error("Bad output attributes: %s", " ".join(bad_attrs))
            sys.exit(os.EX_USAGE)

        kwargs = {}
        if self.params.get('model'):
            kwargs['model'] = self.params.get('model')

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

        try:
            with AdminClient(lrs_required=False) as adm:
                count = adm.device_delete(self.family, resources)
        except EnvironmentError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))

        self.logger.info("Removed %d device(s) successfully", count)

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
    ]

    def add_medium(self, medium, tags):
        self.client.media.add(medium, 'RADOS', tags=tags)

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
        attrs.extend(['*', 'all'])
        out_attrs = self.params.get('output')
        bad_attrs = set(out_attrs).difference(set(attrs))
        if bad_attrs:
            self.logger.error("Bad output attributes: %s", " ".join(bad_attrs))
            sys.exit(os.EX_USAGE)

        try:
            with AdminClient(lrs_required=False) as adm:
                obj_list, p_objs, n_objs = adm.layout_list(
                    self.params.get('res'),
                    self.params.get('pattern'),
                    self.params.get('name'),
                    self.params.get('degroup'))

                if len(obj_list) > 0:
                    dump_object_list(obj_list, attr=out_attrs,
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
        ScanOptHandler,
    ]

    def exec_scan(self):
        """Scan this lib and display the result"""
        libs = self.params.get("res")
        if not libs:
            try:
                lib_dev_path_from_cfg = cfg.get_val("lrs", "lib_device")
                libs.append(lib_dev_path_from_cfg)
            except KeyError as err:
                self.logger.error(str(err) + ", will abort 'lib scan'")
                sys.exit(os.EX_DATAERR)

        try:
            with AdminClient(lrs_required=False) as adm:
                for lib_dev in libs:
                    lib_data = adm.lib_scan(PHO_LIB_SCSI, lib_dev)
                    # FIXME: can't use dump_object_list yet as it does not play
                    # well with unstructured dict-like data (relies on getattr)
                    self._print_lib_data(lib_data)
        except EnvironmentError as err:
            self.logger.error(str(err) + ", will abort 'lib scan'")
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
        pass

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
                                 'between [disk, dir, tape]; object type '
                                 'is not supported with this option',
                            choices=["dir", "tape", "disk"])
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

SYSLOG_LOG_LEVELS = ["critical", "error", "warning", "info", "debug"]

class PhobosActionContext(object):
    """
    Find, initialize and operate an appropriate action execution context for the
    specified command line.
    """
    CLI_LOG_FORMAT_REG = "%(asctime)s <%(levelname)s> %(message)s"
    CLI_LOG_FORMAT_DEV = "%(asctime)s <%(levelname)s> [%(process)d/" \
                         "%(funcName)s:%(filename)s:%(lineno)d] %(message)s"

    supported_handlers = [
        # Resource interfaces
        DirOptHandler,
        TapeOptHandler,
        RadosPoolOptHandler,
        DriveOptHandler,
        ObjectOptHandler,
        ExtentOptHandler,
        LibOptHandler,
        DeleteOptHandler,
        UndeleteOptHandler,
        PingOptHandler,
        LocateOptHandler,
        LocksOptHandler,

        # Store command interfaces
        StoreGetHandler,
        StorePutHandler,
        StoreMPutHandler,
        StoreGetMDHandler,
    ]

    def __init__(self, args, **kwargs):
        """Initialize a PAC instance."""
        super(PhobosActionContext, self).__init__(**kwargs)
        self.parser = None
        self.parameters = None

        self.install_arg_parser()

        self.args = self.parser.parse_args(args)
        self.parameters = vars(self.args)

        self.load_config()

        self.log_ctx = LogControl()

    def __enter__(self):
        self.configure_app_logging()
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        self.log_ctx.set_callback(None)

    def install_arg_parser(self):
        """Initialize hierarchical command line parser."""
        # Top-level parser for common options
        self.parser = argparse.ArgumentParser('phobos',
                                              description= \
                                              'phobos command line interface')

        verb_grp = self.parser.add_mutually_exclusive_group()
        verb_grp.add_argument('-v', '--verbose', help='Increase verbosity',
                              action='count', default=0)
        verb_grp.add_argument('-q', '--quiet', help='Decrease verbosity',
                              action='count', default=0)

        self.parser.add_argument('-s', '--syslog', choices=SYSLOG_LOG_LEVELS,
                                 help='also log via syslog with a given '
                                      'verbosity')
        self.parser.add_argument('-c', '--config',
                                 help='Alternative configuration file')

        sub = self.parser.add_subparsers(dest='goal')
        sub.required = True

        # Register misc actions handlers
        for handler in self.supported_handlers:
            handler.subparser_register(sub)

    def load_config(self):
        """Load configuration file."""
        cpath = self.parameters.get('config')
        # Try to open configuration file
        try:
            cfg.load_file(cpath)
        except IOError as exc:
            if exc.errno == errno.ENOENT or exc.errno == errno.EALREADY:
                return
            raise

    def configure_app_logging(self):
        """
        Configure a multilayer logger according to command line specifications.
        """
        fmt = self.CLI_LOG_FORMAT_REG # default

        # Both are mutually exclusive
        lvl = self.parameters.get('verbose')
        lvl -= self.parameters.get('quiet')
        syslog_level = self.parameters.get('syslog')

        if lvl >= 2:
            # -vv
            pylvl = DEBUG
            fmt = self.CLI_LOG_FORMAT_DEV
        elif lvl == 1:
            # -v
            pylvl = VERBOSE
        elif lvl == 0:
            # default
            pylvl = INFO
        elif lvl == -1:
            # -q
            pylvl = WARNING
        elif lvl <= -2:
            # -qq
            pylvl = DISABLED

        # Basic root logger configuration: log to console
        root_logger = logging.getLogger()
        base_formatter = logging.Formatter(fmt)
        stream_handler = logging.StreamHandler()
        stream_handler.setLevel(pylvl)
        stream_handler.setFormatter(base_formatter)
        root_logger.addHandler(stream_handler)

        # Add a syslog handler if asked on the CLI (maybe this could be done
        # with the config file too)
        if syslog_level is not None:
            syslog_handler = SysLogHandler(address="/dev/log")
            syslog_handler.setLevel(syslog_level.upper())

            # Set the syslog formatter according to the syslog level
            if syslog_handler.level <= logging.DEBUG:
                syslog_fmt = self.CLI_LOG_FORMAT_DEV
            else:
                syslog_fmt = self.CLI_LOG_FORMAT_REG
            syslog_formatter = logging.Formatter(
                'phobos[%(process)d]: ' + syslog_fmt
            )
            syslog_handler.setFormatter(syslog_formatter)
            root_logger.addHandler(syslog_handler)

            # The actual log level will be the most verbose of the console and
            # syslog ones (lesser value is more verbose)
            pylvl = min(pylvl, syslog_handler.level)

        # Set the root logger level
        root_logger.setLevel(pylvl)

        self.log_ctx.set_callback(phobos_log_handler)
        self.log_ctx.set_level(pylvl)

    def run(self):
        """
        Invoke the desired method on the selected media handler.
        It is assumed that all checks have happened already to make sure that
        the execution order refers to a valid method of the target object.
        """

        target = self.parameters.get('goal')
        action = self.parameters.get('verb')

        assert target is not None
        assert action is not None

        for handler in self.supported_handlers:
            if handler.label == target or target in handler.alias:
                with handler(self.parameters) as target_inst:
                    # Invoke target::exec_{action}
                    getattr(target_inst, 'exec_%s' % action.replace('-', '_'))()
                return

        # The command line parser must catch such mistakes
        raise NotImplementedError("Unexpected parameters: '%s' '%s'" % (target,
                                                                        action))

def phobos_main(args=None):
    """
    Entry point for the `phobos' command. Indirectly provides
    argument parsing and execution of the appropriate actions.
    """
    with PhobosActionContext(args if args is not None else sys.argv[1::]) \
                                                                        as pac:
        pac.run()
