#!/usr/bin/env python2

#
#  All rights reserved (c) 2014-2020 CEA/DAM.
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

import re
import sys
import shlex
import errno
import argparse
import logging
from logging.handlers import SysLogHandler

import os
import os.path

from abc import ABCMeta, abstractmethod

from ClusterShell.NodeSet import NodeSet

from phobos.core.admin import Client as AdminClient
from phobos.core.cfg import load_file as cfg_load_file
from phobos.core.const import (PHO_LIB_SCSI, rsc_family2str,
                               PHO_RSC_ADM_ST_LOCKED, PHO_RSC_ADM_ST_UNLOCKED)
from phobos.core.dss import Client as DSSClient
from phobos.core.ffi import DevInfo, MediaInfo, ResourceFamily
from phobos.core.ldm import LibAdapter
from phobos.core.log import LogControl, DISABLED, WARNING, INFO, VERBOSE, DEBUG
from phobos.core.store import Client as XferClient, attrs_as_dict, PutParams
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
    return "%s: %s" % (exc.strerror, os.strerror(abs(exc.errno)))

def attr_convert(usr_attr):
    """Convert k/v pairs as expressed by the user into a dictionnary."""
    tkn_iter = shlex.shlex(usr_attr, posix=True)
    tkn_iter.whitespace = '=,'
    tkn_iter.whitespace_split = True

    kv_pairs = list(tkn_iter) # [k0, v0, k1, v1...]

    if len(kv_pairs) % 2 != 0:
        print kv_pairs
        raise ValueError("Invalid attribute string")

    return dict(zip(kv_pairs[0::2], kv_pairs[1::2]))


class BaseOptHandler(object):
    """
    Skeleton for action handlers. It can register a corresponding argument
    subparser to a top-level one, with targeted object, description and
    supported actions.
    """
    label = '(undef)'
    descr = '(undef)'
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
        pass

    @abstractmethod
    def __exit__(self, exc_type, exc_value, traceback):
        """
        Optional method handlers can implement to release resources after
        execution.
        """
        pass

    @classmethod
    def add_options(cls, parser):
        """Add options for this specific command-line subsection."""
        pass

    @classmethod
    def subparser_register(cls, base_parser):
        """Register the subparser to a top-level one."""
        subparser = base_parser.add_parser(cls.label, help=cls.descr)

        # Register options relating to the current media
        cls.add_options(subparser)

        # Register supported verbs and associated options
        if cls.verbs:
            v_parser = subparser.add_subparsers(dest='verb')
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
    def _compl_notify(data, xfr, err_code):
        """Custom completion handler to display metadata."""
        if err_code != 0:
            return

        res = []
        itm = attrs_as_dict(xfr.contents.xd_attrs)
        if not itm:
            return

        for k, v in sorted(itm.items()):
            res.append('%s=%s' % (k, v))

        print ','.join(res)

    def exec_getmd(self):
        """Retrieve an object attributes from backend."""
        oid = self.params.get('object_id')
        self.logger.debug("Retrieving attrs for 'objid:%s'", oid)
        self.client.getmd_register(oid, None)
        try:
            self.client.run(compl_cb=self._compl_notify)
        except IOError as err:
            self.logger.error("Cannot GETMD for 'objid:%s': %s",
                              oid, env_error_format(err))
            sys.exit(os.EX_DATAERR)


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

    def exec_get(self):
        """Retrieve an object from backend."""
        oid = self.params.get('object_id')
        dst = self.params.get('dest_file')
        self.logger.debug("Retrieving object 'objid:%s' to '%s'", oid, dst)
        self.client.get_register(oid, dst)
        try:
            self.client.run()
        except IOError as err:
            self.logger.error("Cannot GET 'objid:%s' to '%s': %s",
                              oid, dst, env_error_format(err))
            sys.exit(os.EX_DATAERR)


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
                            choices=map(rsc_family2str,
                                        ResourceFamily.__members__.values()),
                            help='Targeted storage family')
        parser.add_argument('-l', '--layout', choices=["simple", "raid1"],
                            help='Desired storage layout')


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

        put_params = PutParams(self.params.get('family'),
                               self.params.get('layout'),
                               self.params.get('tags', []))

        self.logger.debug("Inserting object '%s' to 'objid:%s'", src, oid)

        self.client.put_register(oid, src, attrs=attrs, put_params=put_params)
        try:
            self.client.run()
        except IOError as err:
            self.logger.error("Cannot PUT '%s' to 'objid:%s': %s",
                              src, oid, env_error_format(err))
            sys.exit(os.EX_DATAERR)


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

        put_params = PutParams(self.params.get('family'),
                               self.params.get('layout'),
                               self.params.get('tags', []))

        for i, line in enumerate(fin):
            # Skip empty lines and comments
            line = line.strip()
            if not line or line.startswith('#'):
                continue

            match = re.match("(\S+)\s+(\S+)\s+(.*)", line)
            if match is None:
                self.logger.error("Format error on line %d: %s", i, line)
                sys.exit(os.EX_DATAERR)

            src = match.group(1)
            oid = match.group(2)
            attrs = match.group(3)

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
            self.logger.error("Cannot MPUT objects, see logs for details: %s",
                              env_error_format(err))
            sys.exit(os.EX_DATAERR)


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
        parser.add_argument('res', nargs='+', help='Resource(s) to lock')
        parser.add_argument('--force', action='store_true',
                            help='Do not check the current lock state')

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

class ScanOptHandler(BaseOptHandler):
    """Scan a physical resource and display retrieved information."""
    label = 'scan'
    descr = 'scan a physical resource and display retrieved information'

    @classmethod
    def add_options(cls, parser):
        """Add resource-specific options."""
        super(ScanOptHandler, cls).add_options(parser)
        parser.add_argument('res', nargs='+',
                            help='Resource(s) or device(s) to scan')

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

        attr = [x for x in DevInfo().get_display_dict().keys()]
        attr.sort()
        parser.add_argument('-o', '--output', type=lambda t: t.split(','),
                            default='name',
                            help="attributes to output, comma-separated, "
                                 "choose from {" + " ".join(attr) + "} "
                                 "(default: %(default)s)")

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

        attr = [x for x in MediaInfo().get_display_dict().keys()]
        attr.sort()
        parser.add_argument('-o', '--output', type=lambda t: t.split(','),
                            default='name',
                            help="attributes to output, comma-separated, "
                                 "choose from {" + " ".join(attr) + "} "
                                 "(default: %(default)s)")

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
                                 'tags')
        parser.add_argument('res', nargs='+', help='Resource(s) to update')

class FormatOptHandler(DSSInteractHandler):
    """Format a resource."""
    label = 'format'
    descr = 'format a media'

    @classmethod
    def add_options(cls, parser):
        """Add resource-specific options."""
        super(FormatOptHandler, cls).add_options(parser)
        parser.add_argument('--fs', default='ltfs', help='Filesystem type')
        parser.add_argument('-n', '--nb-streams', metavar='STREAMS', type=int,
                            help='Max number of parallel formatting operations')
        parser.add_argument('--unlock', action='store_true',
                            help='Unlock media once it is ready to be written')
        parser.add_argument('res', nargs='+', help='Resource(s) to format')

class BaseResourceOptHandler(DSSInteractHandler):
    """Generic interface for resources manipulation."""
    label = None
    descr = None
    family = None
    verbs = []

    def filter(self, ident):
        """Return a list of objects that match the provided identifier."""
        raise NotImplementedError("Abstract method subclasses must implement")

class DeviceOptHandler(BaseResourceOptHandler):
    """Shared interface for devices."""
    verbs = [
        AddOptHandler,
        ListOptHandler,
        LockOptHandler,
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
        keep_locked = not self.params.get('unlock')

        try:
            with AdminClient(lrs_required=False) as adm:
                for path in resources:
                    # TODO: this will be dropped when adding to DSS will be
                    # done by adm.device_add() below
                    _, serial = self.client.devices.add(self.family, path,
                                                             locked=keep_locked)
                    adm.device_add(self.family, serial)
        except EnvironmentError as err:
            self.logger.error("Cannot add device: %s", env_error_format(err))
            sys.exit(os.EX_DATAERR)

        self.logger.info("Added %d device(s) successfully", len(resources))

    def exec_lock(self):
        """Device lock"""
        names = self.params.get('res')

        try:
            with AdminClient(lrs_required=False) as adm:
                adm.device_lock(self.family, names, self.params.get('force'))
        except EnvironmentError as err:
            self.logger.error("Failed to lock device(s): %s",
                              env_error_format(err))
            sys.exit(os.EX_DATAERR)

        self.logger.info("%d device(s) locked", len(names))

    def exec_unlock(self):
        """Device unlock"""
        names = self.params.get('res')

        try:
            with AdminClient(lrs_required=False) as adm:
                adm.device_unlock(self.family, names, self.params.get('force'))
        except EnvironmentError as err:
            self.logger.error("Failed to unlock device(s): %s",
                              env_error_format(err))
            sys.exit(os.EX_DATAERR)

        self.logger.info("%d device(s) unlocked", len(names))

class MediaOptHandler(BaseResourceOptHandler):
    """Shared interface for media."""
    verbs = [
        MediaAddOptHandler,
        MediaUpdateOptHandler,
        FormatOptHandler,
        MediaListOptHandler,
        LockOptHandler,
        UnlockOptHandler
    ]

    def exec_add(self):
        """Add new media."""
        names = NodeSet.fromlist(self.params.get('res'))
        fstype = self.params.get('fs').upper()
        techno = self.params.get('type', '').upper()
        tags = self.params.get('tags', [])
        keep_locked = not self.params.get('unlock')

        for med in names:
            try:
                self.client.media.add(self.family, fstype, techno, med,
                                      tags=tags, locked=keep_locked)
            except EnvironmentError as err:
                self.logger.error("Cannot add medium %s: %s", med,
                                  env_error_format(err))
                sys.exit(os.EX_DATAERR)

        self.logger.info("Added %d media successfully", len(names))

    def exec_update(self):
        """Update an existing media"""
        uids = NodeSet.fromlist(self.params.get('res'))
        tags = self.params.get('tags')
        if tags is None:
            self.logger.info("No update to be performed")
            return

        # Empty string clears tag and ''.split(',') == ['']
        if tags == ['']:
            tags = []

        failed = []
        for uid in uids:
            # Retrieve full media and check that it exists
            media = self.client.media.get(family=self.family, id=uid)
            if not media:
                self.logger.error("No '%s' media found", uid)
                failed.append(uid)
                continue

            # Unlikely: means an incoherent db
            if len(media) > 1:
                raise RuntimeError("multiple media have the same id: %s" % uid)
            media = media[0]

            # Attempt to lock the media to avoid concurrent modifications
            try:
                self.client.media.lock([media])
            except EnvironmentError as err:
                self.logger.error("Failed to lock media '%s': %s",
                                  uid, env_error_format(err))
                failed.append(uid)
                continue

            media = self.client.media.get(family=self.family, id=uid)
            assert len(media) == 1
            media = media[0]

            # Update tags
            try:
                media.tags = tags
                self.client.media.update([media])
            except EnvironmentError as err:
                self.logger.error("Failed to update media '%s': %s",
                                  uid, env_error_format(err))
                failed.append(uid)
            finally:
                self.client.media.unlock([media])

        if failed:
            self.logger.error("Failed to update: %s", ", ".join(failed))
            sys.exit(os.EX_DATAERR)

    def exec_format(self):
        """Format media however requested."""
        media_list = NodeSet.fromlist(self.params.get('res'))
        fs_type = self.params.get('fs')
        unlock = self.params.get('unlock')

        try:
            with AdminClient(lrs_required=True) as adm:
                if unlock:
                    self.logger.debug("Post-unlock enabled")
                for name in media_list:
                    self.logger.info("Formatting media '%s'", name)
                    adm.fs_format(name, fs_type, unlock=unlock)
        except EnvironmentError as err:
            # XXX add an option to exit on first error
            self.logger.error("fs_format: %s", env_error_format(err))

    def exec_list(self):
        """List media and display results."""
        attrs = [x for x in MediaInfo().get_display_dict().keys()]
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
            objs = self.client.media.get(family=self.family, **kwargs)


        if len(objs) > 0:
            dump_object_list(objs, 'media', attr=self.params.get('output'),
                             fmt=self.params.get('format'))

    def exec_lock(self):
        """Lock media"""
        results = []
        uids = NodeSet.fromlist(self.params.get('res'))
        for uid in uids:
            media = self.client.media.get(id=uid)
            assert len(media) == 1

            # Attempt to lock the media to avoid concurrent modifications
            try:
                self.client.media.lock([media[0]])
            except EnvironmentError as err:
                self.logger.error("Failed to lock media '%s': %s",
                                  uid, env_error_format(err))
                continue

            media = self.client.media.get(id=uid)
            assert len(media) == 1

            media[0].rsc.adm_status = PHO_RSC_ADM_ST_LOCKED
            results.append(media[0])

        if len(results) != len(uids):
            self.logger.error("At least one media is in use, use --force")
            sys.exit(os.EX_DATAERR)

        try:
            self.client.media.update(results)
        except EnvironmentError as err:
            self.logger.error("Failed to lock one or more media(s): %s",
                              env_error_format(err))
            sys.exit(os.EX_DATAERR)
        finally:
            self.client.media.unlock(results)

        self.logger.info("%d media(s) locked" % len(results))

    def exec_unlock(self):
        """Unlock media"""
        results = []
        uids = NodeSet.fromlist(self.params.get('res'))
        for uid in uids:
            media = self.client.media.get(id=uid)
            assert len(media) == 1

            # Attempt to lock the media to avoid concurrent modifications
            try:
                self.client.media.lock([media[0]])
            except EnvironmentError as err:
                self.logger.error("Failed to lock media '%s': %s",
                                  uid, env_error_format(err))
                continue

            media = self.client.media.get(id=uid)
            assert len(media) == 1

            if media[0].rsc.adm_status == PHO_RSC_ADM_ST_UNLOCKED:
                self.logger.warn("Media %s is already unlocked", uid)

            media[0].rsc.adm_status = PHO_RSC_ADM_ST_UNLOCKED
            results.append(media[0])

        if len(results) != len(uids):
            self.logger.error("At least one media is in use, use --force")
            sys.exit(os.EX_DATAERR)

        try:
            self.client.media.update(results)
        except EnvironmentError as err:
            self.logger.error("Failed to unlock one or more media(s): %s",
                              env_error_format(err))
            sys.exit(os.EX_DATAERR)
        finally:
            self.client.media.unlock(results)

        self.logger.info("%d media(s) unlocked" % len(results))


class DirOptHandler(MediaOptHandler):
    """Directory-related options and actions."""
    label = 'dir'
    descr = 'handle directories'
    family = ResourceFamily(ResourceFamily.RSC_DIR)

    def exec_add(self):
        """
        Add a new directory.
        Note that this is a special case where we add both a media (storage) and
        a device (mean to access it).
        """
        resources = self.params.get('res')
        keep_locked = not self.params.get('unlock')
        tags = self.params.get('tags', [])

        try:
            with AdminClient(lrs_required=False) as adm:
                for path in resources:
                    # Remove any trailing slash
                    path = path.rstrip('/')

                    self.client.media.add(self.family, 'POSIX', None, path,
                                          locked=keep_locked, tags=tags)
                    # Add device unlocked and rely on media locking
                    _, serial = self.client.devices.add(self.family, path,
                                                        locked=False)
                    adm.device_add(self.family, serial)

                    self.logger.debug("Added directory '%s'", path)
        except EnvironmentError as err:
            self.logger.error("Cannot add directory: %s", env_error_format(err))
            sys.exit(os.EX_DATAERR)

        self.logger.info("Added %d dir(s) successfully", len(resources))


class DriveOptHandler(DeviceOptHandler):
    """Tape Drive options and actions."""
    label = 'drive'
    descr = 'handle tape drives (use ID or device path to identify resource)'
    family = ResourceFamily(ResourceFamily.RSC_TAPE)
    verbs = [
        AddOptHandler,
        DriveListOptHandler,
        LockOptHandler,
        UnlockOptHandler
    ]

    def exec_list(self):
        """List devices and display results."""
        attrs = [x for x in DevInfo().get_display_dict().keys()]
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
            objs = self.client.devices.get(family=self.family, **kwargs)

        if len(objs) > 0:
            dump_object_list(objs, 'device', attr=self.params.get('output'),
                             fmt=self.params.get('format'))

class TapeOptHandler(MediaOptHandler):
    """Magnetic tape options and actions."""
    label = 'tape'
    descr = 'handle magnetic tape (use tape label to identify resource)'
    family = ResourceFamily(ResourceFamily.RSC_TAPE)
    verbs = [
        TapeAddOptHandler,
        MediaUpdateOptHandler,
        FormatOptHandler,
        MediaListOptHandler,
        LockOptHandler,
        UnlockOptHandler
    ]

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
        for lib_dev in self.params.get("res"):
            lib_data = LibAdapter(PHO_LIB_SCSI, lib_dev).scan()
            # FIXME: can't use dump_object_list yet as it does not play well
            # with unstructured dict-like data (relies on getattr)
            self._print_lib_data(lib_data)

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
            print " ".join(line)

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
        DriveOptHandler,
        LibOptHandler,

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

        # Register misc actions handlers
        for handler in self.supported_handlers:
            handler.subparser_register(sub)

    def load_config(self):
        """Load configuration file."""
        cpath = self.parameters.get('config')
        # Try to open configuration file
        try:
            cfg_load_file(cpath)
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
        self.configure_app_logging()

        target = self.parameters.get('goal')
        action = self.parameters.get('verb')

        assert target is not None
        assert action is not None

        for handler in self.supported_handlers:
            if handler.label == target:
                with handler(self.parameters) as target_inst:
                    # Invoke target::exec_{action}
                    getattr(target_inst, 'exec_%s' % action)()
                return

        # The command line parser must catch such mistakes
        raise NotImplementedError("Unexpected parameters: '%s' '%s'" % (target,
                                                                        action))


def phobos_main(args=sys.argv[1::]):
    """
    Entry point for the `phobos' command. Indirectly provides
    argument parsing and execution of the appropriate actions.
    """
    PhobosActionContext(args).run()
