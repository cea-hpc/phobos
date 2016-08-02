#!/usr/bin/python

# Copyright CEA/DAM 2015-2016
# This file is part of the Phobos project

"""
Phobos command-line interface utilities.

Phobos action handlers (AH). AHs are descriptors of the objects that phobos can
manipulate. They expose both command line subparsers, to retrieve and validate
specific command line parameters and the API entry points for phobos to trigger
actions.
"""

import re
import os
import sys
import shlex
import errno
import argparse
import logging
import os.path

import phobos.capi.log as clog
import phobos.capi.dss as cdss

from phobos.cfg import load_config_file, get_config_value
from phobos.dss import Client as DSSClient, GenericError
from phobos.store import Client as XferClient
from phobos.lrs import fs_format
from phobos.output import dump_object_list
from ClusterShell.NodeSet import NodeSet


def strerror(rc):
    """Basic wrapper to convert return code into corresponding errno string."""
    return os.strerror(abs(rc))


def phobos_log_handler(rec):
    """
    Receive log records emitted from lower layers and inject them into the
    currently configured logger.
    """
    full_msg = rec[7]

    # Append ': <errmsg>' to the original message if err_code was set
    if rec[5] != 0:
        full_msg += ": %s"
        args = (strerror(rec[5]), )
    else:
        args = tuple()

    attrs = {
        'name': 'internals',
        'levelno': rec[0],
        'levelname': logging.getLevelName(rec[0]),
        'process': rec[1],
        'filename': rec[2],
        'funcName': rec[3],
        'lineno': rec[4],
        'exc_info': None,
        'msg': full_msg,
        'args': args,
        'created': rec[6],
    }

    record = logging.makeLogRecord(attrs)
    logger = logging.getLogger(__name__)
    logger.handle(record)


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

    def __init__(self, params, **kwargs):
        """
        Initialize action handler with command line parameters. These are to be
        re-checked later by the specialized chk_* methods.
        """
        super(BaseOptHandler, self).__init__(**kwargs)
        self.params = params
        self.logger = logging.getLogger(__name__)

    def initialize(self):
        """
        Optional method handlers can implement to prepare execution context.
        """
        pass

    def teardown(self):
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

    def initialize(self):
        """Initialize a DSS Client."""
        # XXX Connection info is currently expressed as an opaque string.
        #     Thus use the special _connect keyword to not rebuild it.
        self.client = DSSClient()
        conn_info = get_config_value('dss', 'connect_string')
        if conn_info is None:
            conn_info = ''
        self.client.connect(_connect=conn_info)

    def teardown(self):
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

    def initialize(self):
        """Initialize a client for data movements."""
        self.client = XferClient()

    def teardown(self):
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

    def _compl_notify(self, *args, **kwargs):
        """Custom completion handler to display metadata."""
        oid, _, attrs, rc = args
        if rc != 0:
            return

        if not attrs:
            print '<empty attribute set>'

        res = []
        for k, v in sorted(attrs.items()):
            res.append('%s=%s' % (k, v))

        print ','.join(res)

    def exec_getmd(self):
        """Retrieve an object attributes from backend."""
        oid = self.params.get('object_id')
        self.logger.debug("Retrieving attrs for 'objid:%s'", oid)
        self.client.get_register(oid, None, md_only=True)
        rc = self.client.run(compl_cb=self._compl_notify)
        if rc:
            self.logger.error("Cannot GETMD for 'objid:%s'", oid)
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
        rc = self.client.run()
        if rc:
            self.logger.error("Cannot GET 'objid:%s' to '%s'", oid, dst)
            sys.exit(os.EX_DATAERR)


class StorePutHandler(XferOptHandler):
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
            self.logger.debug("Loaded attributes set '%r'", attrs)

        self.logger.debug("Inserting object '%s' to 'objid:%s'", src, oid)

        self.client.put_register(oid, src, attrs=attrs)
        rc = self.client.run()
        if rc:
            self.logger.error("Cannot issue PUT request")
            sys.exit(os.EX_DATAERR)


class StoreMPutHandler(XferOptHandler):
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
                self.logger.debug("Loaded attributes set '%r'", attrs)

            self.logger.debug("Inserting object '%s' to 'objid:%s'", src, oid)
            self.client.put_register(oid, src, attrs=attrs)

        rc = self.client.run()
        if rc:
            self.logger.error("Cannot issue MPUT request")
            sys.exit(os.EX_DATAERR)

        if fin is not sys.stdin:
            fin.close()


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

class ShowOptHandler(DSSInteractHandler):
    """Show resource details."""
    label = 'show'
    descr = 'show resource details'

    @classmethod
    def add_options(cls, parser):
        """Add resource-specific options."""
        super(ShowOptHandler, cls).add_options(parser)
        parser.add_argument('res', nargs='+', help='Resource(s) to show')
        parser.add_argument('--numeric', action='store_true', default=False,
                            help='Output numeric values')
        parser.add_argument('--format', default='human',
                            help="Output format human/xml/json/csv/yaml " \
                                 "(default: human)")

class DriveListOptHandler(ListOptHandler):
    """
    Specific version of the 'list' command for tape drives, with a couple
    extra-options.
    """
    @classmethod
    def add_options(cls, parser):
        """Add resource-specific options."""
        super(DriveListOptHandler, cls).add_options(parser)
        parser.add_argument('-t', '--type', help='filter on type')


class TapeAddOptHandler(AddOptHandler):
    """Specific version of the 'add' command for tapes, with extra-options."""
    @classmethod
    def add_options(cls, parser):
        """Add resource-specific options."""
        super(TapeAddOptHandler, cls).add_options(parser)
        parser.add_argument('-t', '--type', required=True,
                            help='tape technology')
        parser.add_argument('--fs', default="LTFS",
                            help='Filesystem type (default: LTFS)')

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
    cenum = None
    verbs = []

    @property
    def family(self):
        """Return family (as a string) for the current instance."""
        return cdss.dev_family2str(self.cenum)

    def filter(self, ident):
        """Return a list of objects that match the provided identifier."""
        raise NotImplementedError("Abstract method subclasses must implement")

class DeviceOptHandler(BaseResourceOptHandler):
    """Shared interface for devices."""
    verbs = [
        AddOptHandler,
        FormatOptHandler,
        ListOptHandler,
        ShowOptHandler,
        LockOptHandler,
        UnlockOptHandler
    ]

    def filter(self, ident):
        """
        Return a list of devices that match the identifier for either serial or
        path. You may call it a bug but this is a feature intended to let admins
        transparently address devices using one or the other scheme.
        """
        dev = self.client.devices.get(family=self.family, serial=ident)
        if not dev:
            # 2nd attempt, by path...
            dev = self.client.devices.get(family=self.family, path=ident)
        return dev

    def exec_add(self):
        """Add a new device"""
        resources = self.params.get('res')
        keep_locked = not self.params.get('unlock')

        for path in resources:
            rc = self.client.devices.add(self.cenum, path,
                                         locked=keep_locked)
            if rc:
                self.logger.error("Cannot add device: %s", strerror(rc))
                sys.exit(os.EX_DATAERR)

        self.logger.info("Added %d device(s) successfully", len(resources))

    def exec_list(self):
        """List devices and display results."""
        for obj in self.client.devices.get(family=self.family):
            print obj.serial

    def exec_show(self):
        """Show device details."""
        devs = []
        for serial in self.params.get('res'):
            curr = self.filter(serial)
            if not curr:
                self.logger.error("'%s' not found", serial)
                sys.exit(os.EX_DATAERR)
            assert len(curr) == 1
            devs.append(curr[0])
        if len(devs) > 0:
            dump_object_list(devs, self.params.get('format'),
                             self.params.get('numeric'))

    def exec_lock(self):
        """Device lock"""
        devices = []
        serials = self.params.get('res')

        for serial in serials:
            device = self.filter(serial)
            if not device:
                self.logger.error("Device %s not found", serial)
                sys.exit(os.EX_DATAERR)
            assert len(device) == 1
            if device[0].lock.lock != "":
                if self.params.get('force'):
                    self.logger.warn("Device %s is in use. Administrative "
                                     "locking will not be effective "
                                     "immediately", serial)
                else:
                    self.logger.error("Device %s is in use by %s.",
                                      serial, device[0].lock.lock)
                    continue
            device[0].adm_status = cdss.PHO_DEV_ADM_ST_LOCKED
            devices.append(device[0])
        if len(devices) == len(serials):
            rc = self.client.devices.update(devices)
        else:
            rc = errno.EPERM
            self.logger.error("At least one device is in use, use --force")

        if not rc:
            print "%d device(s) locked" % len(devices)
        else:
            self.logger.error("Failed to lock one or more device(s), error: %s",
                              strerror(rc))

    def exec_unlock(self):
        """Device unlock"""
        devices = []
        serials = self.params.get('res')

        for serial in serials:
            device = self.filter(serial)
            if not device:
                self.logger.error("No such device: %s", serial)
                sys.exit(os.EX_DATAERR)
            if device[0].lock.lock != "" and not self.params.get('force'):
                self.logger.error("Device %s is in use by %s.",
                                  serial, device[0].lock.lock)
                continue
            if device[0].adm_status == cdss.PHO_DEV_ADM_ST_UNLOCKED:
                self.logger.warn("Device %s is already unlocked", serial)
            device[0].adm_status = cdss.PHO_DEV_ADM_ST_UNLOCKED
            devices.append(device[0])
        if len(devices) == len(serials):
            rc = self.client.devices.update(devices)
        else:
            rc = errno.EPERM
            self.logger.error("At least one device is in use, use --force")

        if not rc:
            print "%d device(s) unlocked" % len(devices)
        else:
            self.logger.error("Failed to unlock one or more device(s): %s",
                              strerror(rc))

class MediaOptHandler(BaseResourceOptHandler):
    """Shared interface for media."""
    verbs = [
        AddOptHandler,
        FormatOptHandler,
        ShowOptHandler,
        ListOptHandler,
        LockOptHandler,
        UnlockOptHandler
    ]

    def exec_add(self):
        """Add new media."""
        labels = NodeSet.fromlist(self.params.get('res'))
        fstype = self.params.get('fs').upper()
        techno = self.params.get('type', '').upper()
        keep_locked = not self.params.get('unlock')

        for med in labels:
            rc = self.client.media.add(self.cenum, fstype, techno, med,
                                       locked=keep_locked)
            if rc:
                self.logger.error("Cannot add medium %s: %s", med, strerror(rc))
                sys.exit(os.EX_DATAERR)

        self.logger.info("Added %d media successfully", len(labels))

    def exec_format(self):
        """Format media however requested."""
        media_list = NodeSet.fromlist(self.params.get('res'))
        fs_type = self.params.get('fs')
        unlock = self.params.get('unlock')
        if unlock:
            self.logger.debug("Post-unlock enabled")
        for label in media_list:
            self.logger.debug("Formatting media '%s'", label)
            try:
                fs_format(self.client, label, fs_type, unlock=unlock)
            except GenericError, exc:
                # XXX add an option to exit on first error
                self.logger.error("fs_format: %s", exc)

    def exec_show(self):
        """Show media details."""
        results = []
        uids = NodeSet.fromlist(self.params.get('res'))
        for uid in uids:
            media = self.client.media.get(family=self.family, id=uid)
            if not media:
                self.logger.warning("Media id %s not found", uid)
                continue
            assert len(media) == 1
            results.append(media[0])
        dump_object_list(results, self.params.get('format'),
                         self.params.get('numeric'))

    def exec_list(self):
        """List all medias."""
        for media in self.client.media.get(family=self.family):
            print media.ident

    def exec_lock(self):
        """Lock media"""
        results = []
        uids = NodeSet.fromlist(self.params.get('res'))
        for uid in uids:
            media = self.client.media.get(id=uid)
            if media[0].lock.lock != "":
                if self.params.get('force'):
                    self.logger.warn("Media %s is in use. Administrative "
                                     "locking will not be effective "
                                     "immediately", uid)
                else:
                    self.logger.error("Media %s is in use by %s.",
                                      uid, media[0].lock.lock)
                    continue

            media[0].adm_status = cdss.PHO_MDA_ADM_ST_LOCKED
            results.append(media[0])

        if len(results) == len(uids):
            rc = self.client.media.update(results)
        else:
            rc = errno.EPERM
            self.logger.error("At least one media is in use, use --force")

        if not rc:
            print "%d media(s) locked" % len(results)
        else:
            self.logger.error("Failed to lock one or more media(s): %s",
                              strerror(rc))

    def exec_unlock(self):
        """Unlock media"""
        results = []
        uids = NodeSet.fromlist(self.params.get('res'))
        for uid in uids:
            media = self.client.media.get(id=uid)
            if media[0].lock.lock != "" and not self.params.get('force'):
                self.logger.error("Media %s is in use by %s",
                                  uid, media[0].lock.lock)
                continue

            if media[0].adm_status == cdss.PHO_MDA_ADM_ST_UNLOCKED:
                self.logger.warn("Media %s is already unlocked", uid)

            media[0].adm_status = cdss.PHO_MDA_ADM_ST_UNLOCKED
            results.append(media[0])

        if len(results) == len(uids):
            rc = self.client.media.update(results)
        else:
            rc = errno.EPERM
            self.logger.error("At least one media is not locked, use --force")

        if not rc:
            print "%d media unlocked" % len(results)
        else:
            self.logger.error("Failed to unlock one or more media: %s",
                              strerror(rc))

class DirOptHandler(MediaOptHandler, DeviceOptHandler):
    """Directory-related options and actions."""
    label = 'dir'
    descr = 'handle directories'
    cenum = cdss.PHO_DEV_DIR

    def exec_add(self):
        """
        Add a new directory.
        Note that this is a special case where we add both a media (storage) and
        a device (mean to access it).
        """
        resources = self.params.get('res')
        keep_locked = not self.params.get('unlock')

        for path in resources:
            # Remove any trailing slash
            path = path.rstrip('/')
            rc_m = self.client.media.add(self.cenum, 'POSIX', None, path,
                                         locked=keep_locked)
            # Add device unlocked and rely on media locking
            rc_d = self.client.devices.add(self.cenum, path, locked=False)
            if rc_m or rc_d:
                self.logger.error("Cannot add directory: %s",
                                  strerror(rc_m or rc_d))
                sys.exit(os.EX_DATAERR)

            self.logger.debug("Added directory '%s'", path)

        self.logger.info("Added %d dir(s) successfully", len(resources))


class DriveOptHandler(DeviceOptHandler):
    """Tape Drive options and actions."""
    label = 'drive'
    descr = 'handle tape drives (use ID or device path to identify resource)'
    cenum = cdss.PHO_DEV_TAPE


class TapeOptHandler(MediaOptHandler):
    """Magnetic tape options and actions."""
    label = 'tape'
    descr = 'handle magnetic tape (use tape label to identify resource)'
    cenum = cdss.PHO_DEV_TAPE
    verbs = [
        TapeAddOptHandler,
        FormatOptHandler,
        ShowOptHandler,
        ListOptHandler,
        LockOptHandler,
        UnlockOptHandler
    ]


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

        self.load_config() # After this, code can use get_config_value()

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

        self.parser.add_argument('-c', '--config',
                                 help='Alternative configuration file')

        sub = self.parser.add_subparsers(dest='goal')

        # Register misc actions handlers
        for obj in self.supported_handlers:
            obj.subparser_register(sub)

    def load_config(self):
        """Load configuration file."""
        cpath = self.parameters.get('config')
        # Try to open configuration file
        try:
            load_config_file(cpath)
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

        if lvl >= 2:
            # -vv
            pylvl = clog.PY_LOG_DEBUG
            fmt = self.CLI_LOG_FORMAT_DEV
        elif lvl == 1:
            # -v
            pylvl = clog.PY_LOG_VERB
        elif lvl == 0:
            # default
            pylvl = clog.PY_LOG_INFO
        elif lvl == -1:
            # -q
            pylvl = clog.PY_LOG_WARNING
        elif lvl <= -2:
            # -qq
            pylvl = clog.PY_LOG_DISABLED

        clog.set_callback(phobos_log_handler)
        clog.set_level(pylvl)

        logging.basicConfig(level=pylvl, format=fmt)

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

        target_inst = None
        for obj in self.supported_handlers:
            if obj.label == target:
                target_inst = obj(self.parameters)
                break

        # The command line parser must catch such mistakes
        assert target_inst is not None

        # Invoke target::exec_{action}()
        target_inst.initialize()
        getattr(target_inst, 'exec_%s' % action)()
        target_inst.teardown()

def phobos_main(args=sys.argv[1::]):
    """
    Entry point for the `phobos' command. Indirectly provides
    argument parsing and execution of the appropriate actions.
    """
    PhobosActionContext(args).run()
