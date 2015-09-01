#!/usr/bin/python

# Copyright CEA/DAM 2015
# Author: Henri Doreau <henri.doreau@cea.fr>
#
# This file is part of the Phobos project

"""
Phobos command-line interface utilities.

Phobos action handlers (AH). AHs are descriptors of the objects that phobos can
manipulate. They expose both command line subparsers, to retrieve and validate
specific command line parameters and the API entry points for phobos to trigger
actions.
"""

import sys
import errno
import argparse
import ConfigParser as configparser

from phobos.dss import Client


class BaseOptHandler(object):
    """
    Skeleton for action handlers. It can register a corresponding argument
    subparser to a top-level one, with targeted object, description and
    supported actions.
    """
    label = '(undef)'
    descr = '(undef)'
    verbs = []

    def __init__(self, params, config, **kwargs):
        """
        Initialize action handler with command line parameters. These are to be
        re-checked later by the specialized chk_* methods.
        """
        super(BaseOptHandler, self).__init__(**kwargs)
        self.params = params
        self.config = config
        self.client = None

    def dss_connect(self):
        """Initialize a DSS Client."""
        dss_params = self.params.get('dss', {})
        if dss_params:
            # pass options as is, except pwdfile which we override
            pwd_file = dss_params.get('pwdfile')
            if pwd_file is not None:
                del dss_params['pwdfile']
                with pwd_file.open(pwd_file) as fin:
                    dss_params['password'] = fin.read()

        self.client = Client()
        # XXX Connection info is currently expressed as an opaque string.
        #     Thus use the special _connect keyword to not rebuild it.
        self.client.connect(_connect=self.config['dss']['connect_string'])

    def dss_disconnect(self):
        """Release resources associated to a DSS handle."""
        self.client.disconnect()

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


class AddOptHandler(BaseOptHandler):
    """Insert a new resource into the system."""
    label = 'add'
    descr = 'insert new resource(s) to the system'

    @classmethod
    def add_options(cls, parser):
        super(AddOptHandler, cls).add_options(parser)
        parser.add_argument('--unlock', action='store_true',
                            help='Unlock resource on success')
        parser.add_argument('res', nargs='+', help='Resource(s) to add')


class CheckOptHandler(BaseOptHandler):
    """Issue a check command on the designated object(s)."""
    label = 'check'
    descr = 'check state / consistency of the selected resource(s).'

    @classmethod
    def add_options(cls, parser):
        """Add resource-specific options."""
        super(CheckOptHandler, cls).add_options(parser)
        parser.add_argument('res', nargs='+', help='Resource(s) to check')


class ListOptHandler(BaseOptHandler):
    """List items of a specific type."""
    label = 'list'
    descr = 'list all entries of the kind'


class LockOptHandler(BaseOptHandler):
    """Lock resource."""
    label = 'lock'
    descr = 'lock resource(s)'

    @classmethod
    def add_options(cls, parser):
        """Add resource-specific options."""
        super(LockOptHandler, cls).add_options(parser)
        parser.add_argument('res', nargs='+', help='Resource(s) to lock')


class UnlockOptHandler(BaseOptHandler):
    """Unlock resource."""
    label = 'unlock'
    descr = 'unlock resource(s)'

    @classmethod
    def add_options(cls, parser):
        """Add resource-specific options."""
        super(UnlockOptHandler, cls).add_options(parser)
        parser.add_argument('res', nargs='+', help='Resource(s) to unlock')


class ShowOptHandler(BaseOptHandler):
    """Show resource details."""
    label = 'show'
    descr = 'show resource details'

    @classmethod
    def add_options(cls, parser):
        """Add resource-specific options."""
        super(ShowOptHandler, cls).add_options(parser)
        parser.add_argument('res', nargs='+', help='Resource(s) to show')


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
        parser.add_argument('-t', '--type', required=True, help='tape technology')
        parser.add_argument('-f', '--fs-type', help='Filesystem type')


class FormatOptHandler(BaseOptHandler):
    """Format a resource."""
    label = 'format'
    descr = 'format a tape'

    @classmethod
    def add_options(cls, parser):
        """Add resource-specific options."""
        super(FormatOptHandler, cls).add_options(parser)
        parser.add_argument('-f', '--fs-type', required=True, help='Filesystem type')
        parser.add_argument('-n', '--nb-streams', metavar='STREAMS', type=int,
                            help='Max number of parallel formatting operations')
        parser.add_argument('--unlock', action='store_true',
                            help='Unlock tape once it is ready to be written')
        parser.add_argument('res', nargs='+', help='Resource(s) to format')


class DirOptHandler(BaseOptHandler):
    """Directory-related options and actions."""
    label = 'dir'
    descr = 'handle directories'
    verbs = [
        AddOptHandler,
        ListOptHandler,
        ShowOptHandler,
        LockOptHandler,
        UnlockOptHandler
    ]

    def exec_add(self):
        print 'DIR ADD'

    def exec_list(self):
        print 'DIR LIST'

    def exec_show(self):
        print 'DIR SHOW'

    def exec_lock(self):
        print 'DIR LOCK'

    def exec_unlock(self):
        print 'DIR UNLOCK'


class DriveOptHandler(BaseOptHandler):
    """Tape Drive options and actions."""
    label = 'drive'
    descr = 'handle tape drives (use ID or device path to identify resource)'
    verbs = [
        AddOptHandler,
        DriveListOptHandler,
        ShowOptHandler,
        LockOptHandler,
        UnlockOptHandler
    ]

    def exec_add(self):
        print 'DRIVE ADD'

    def exec_list(self):
        print 'DRIVE LIST'

    def exec_show(self):
        print 'DRIVE SHOW'

    def exec_lock(self):
        print 'DRIVE LOCK'

    def exec_unlock(self):
        print 'DRIVE UNLOCK'


class TapeOptHandler(BaseOptHandler):
    """Magnetic tape options and actions."""
    label = 'tape'
    descr = 'handle magnetic tape (use tape label to identify resource)'
    verbs = [
        TapeAddOptHandler,
        FormatOptHandler,
        ShowOptHandler,
        ListOptHandler,
        LockOptHandler,
        UnlockOptHandler
    ]

    def exec_add(self):
        print 'TAPE ADD'

    def exec_format(self):
        print 'TAPE FORMAT'

    def exec_show(self):
        """Show tape details."""
        for serial in self.params.get('res'):
            tape = self.client.device_get(family='tape', serial=serial)
            assert len(tape) == 1
            desc_iter = sorted(tape[0]._asdict().iteritems())
            print ' '.join(["%s:'%s'" % (k, v) for k, v in desc_iter])

    def exec_list(self):
        """List all tapes."""
        for tape in self.client.device_get(family='tape'):
            print tape.serial

    def exec_lock(self):
        print 'TAPE LOCK'

    def exec_unlock(self):
        print 'TAPE UNLOCK'


class PhobosActionContext(object):
    """
    Find, initialize and operate an appropriate action execution context for the
    specified command line.
    """
    default_conf_file = '/etc/phobos.conf'
    supported_objects = [ DirOptHandler, DriveOptHandler, TapeOptHandler ]

    def __init__(self, args, **kwargs):
        """Initialize a PAC instance."""
        super(PhobosActionContext, self).__init__(**kwargs)
        self.parser = None
        self.defaults = None
        self.parameters = None

        self.install_arg_parser()
        self.parameters = vars(self.parser.parse_args(args))
        self.load_config()

    def install_arg_parser(self):
        """Initialize hierarchical command line parser."""
        # Top-level parser for common options
        self.parser = argparse.ArgumentParser('phobos',
                                              description= \
                                              'phobos command line interface')

        self.parser.add_argument('-v', '--verbose', help='Increase verbosity',
                                 action='count', default=0)

        self.parser.add_argument('-c', '--config',
                                 default=self.default_conf_file,
                                 help='Alternative configuration file')

        # Initialize specialized (objects) argument parsers
        sub = self.parser.add_subparsers(dest='media',
                                         help="Select target object type")

        for obj in self.supported_objects:
            obj.subparser_register(sub)

    def load_config(self):
        """Load configuration file."""
        cpath = self.parameters.get('config')
        # Try to open configuration file
        try:
            cfile = open(cpath)
        except IOError, exc:
            if exc.errno == errno.ENOENT:
                return
            raise

        config = configparser.ConfigParser()
        config.readfp(cfile)
        cfile.close()

        self.defaults = {}
        for section in config.sections():
            self.defaults[section] = dict(config.items(section))

    def run(self):
        """
        Invoke the desired method on the selected media handler.
        It is assumed that all checks have happened already to make sure that
        the execution order refers to a valid method of the target object.
        """
        target = self.parameters.get('media')
        action = self.parameters.get('verb')

        assert target is not None
        assert action is not None

        target_inst = None
        for obj in self.supported_objects:
            if obj.label == target:
                target_inst = obj(self.parameters, self.defaults)
                break

        # The command line parser must catch such mistakes
        assert target_inst is not None

        # Invoke target::exec_{action}()
        target_inst.dss_connect()
        getattr(target_inst, 'exec_%s' % action)()
        target_inst.dss_disconnect()

def phobos_main(args=sys.argv[1::]):
    """
    Entry point for the `phobos' command. Indirectly provides
    argument parsing and execution of the appropriate actions.
    """
    PhobosActionContext(args).run()
