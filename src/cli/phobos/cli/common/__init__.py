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
Phobos CLI common utilities
"""

from abc import ABCMeta, abstractmethod
import argparse
import errno
import logging
from logging.handlers import SysLogHandler
import os

from phobos.core import cfg
from phobos.core.dss import Client as DSSClient
from phobos.core.log import LogControl, DISABLED, WARNING, INFO, VERBOSE, DEBUG

def env_error_format(exc):
    """Return a human readable representation of an environment exception."""
    if exc.errno and exc.strerror:
        return "%s (%s)" % (exc.strerror, os.strerror(abs(exc.errno)))
    elif exc.errno:
        return "%s (%s)" % (os.strerror(abs(exc.errno)), abs(exc.errno))
    elif exc.strerror:
        return exc.strerror

    return ""

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
        'process': rec.plr_tid,
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

class BaseOptHandler:
    """
    Skeleton for action handlers. It can register a corresponding argument
    subparser to a top-level one, with targeted object, description and
    supported actions.
    """
    label = '(undef)'
    descr = '(undef)'
    epilog = None
    alias = []
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
        Optional method handlers can implement to prepare execution context.
        """

    @classmethod
    def add_options(cls, parser):
        """Add options for this specific command-line subsection."""

    @classmethod
    def subparser_register(cls, base_parser):
        """Register the subparser to a top-level one."""
        subparser = base_parser.add_parser(
            cls.label,
            help=cls.descr,
            epilog=cls.epilog,
            aliases=cls.alias
        )

        # Register options relating to the current parser
        cls.add_options(subparser)

        # Register supported verbs and association options
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


class BaseResourceOptHandler(DSSInteractHandler):
    """Generic interface for resources manipulation."""
    label = None
    descr = None
    family = None
    library = None
    verbs = []

SYSLOG_LOG_LEVELS = ["critical", "error", "warning", "info", "debug"]

class PhobosActionContext:
    """
    Find, initialize and operate an appropriate action execution context for the
    specified command line.
    """
    CLI_LOG_FORMAT_REG = "%(asctime)s <%(levelname)s> %(message)s"
    CLI_LOG_FORMAT_DEV = "%(asctime)s <%(levelname)s> [%(process)d/" \
                         "%(funcName)s:%(filename)s:%(lineno)d] %(message)s"

    def __init__(self, handlers, args, **kwargs):
        super(PhobosActionContext, self).__init__(**kwargs)
        self.parser = None
        self.parameters = None
        self.supported_handlers = handlers

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
        self.parser = argparse.ArgumentParser(
            'phobos',
            description="phobos command line interface",
        )

        verb_grp = self.parser.add_mutually_exclusive_group()
        verb_grp.add_argument(
            '-v', '--verbose',
            help='increase verbosity',
            action='count',
            default=0,
        )
        verb_grp.add_argument(
            '-q', '--quiet',
            help='decrease verbosity',
            action='count',
            default=0,
        )

        self.parser.add_argument(
            '-s', '--syslog',
            choices=SYSLOG_LOG_LEVELS,
            help='also log via syslog with a given verbosity'
        )
        self.parser.add_argument(
            '-c', '--config',
            help='alternative configuration file'
        )

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

        if lvl >= 2: # -vv
            pylvl = DEBUG
            fmt = self.CLI_LOG_FORMAT_DEV
        elif lvl == 1: # -v
            pylvl = VERBOSE
        elif lvl == 0: # default
            pylvl = INFO
        elif lvl == -1: # -q
            pylvl = WARNING
        elif lvl <= -2: # -qq
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
                    getattr(target_inst, f"exec_{action.replace('-', '_')}")()
                return

        raise NotImplementedError(
            f"Unexpected parameters: '{target}' '{action}'"
        )
