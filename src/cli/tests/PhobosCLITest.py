#!/usr/bin/python

# Phobos project - CEA/DAM
# Henri Doreau <henri.doreau@cea.fr>

"""Unit tests for phobos.cli"""

import sys
import unittest
import errno

from contextlib import contextmanager
from StringIO import StringIO
from random import randint

from phobos.cli import PhobosActionContext
from phobos.dss import Client
from phobos.dss import GenericError as DSSError

from phobos.capi.dss import dev_info, PHO_DEV_DIR


@contextmanager
def output_intercept():
    """Intercept stdout / stderr outputs."""
    old_out, old_err = sys.stdout, sys.stderr
    try:
        sys.stdout, sys.stderr = StringIO(), StringIO()
        yield sys.stdout, sys.stderr
    finally:
        sys.stdout, sys.stderr = old_out, old_err

class CLIParametersTest(unittest.TestCase):
    """
    This test exerts phobos command line parser with valid and invalid
    combinations.
    """
    def check_cmdline_valid(self, args):
        """Make sure a command line is seen as valid."""
        PhobosActionContext(args)

    def check_cmdline_exit(self, args, code=0):
        """Make sure a command line exits with a given error code."""
        with output_intercept() as (stdout, stderr):
            try:
                # 2.7+ required to use assertRaises as a context manager
                self.check_cmdline_valid(args)
            except SystemExit as exc:
                self.assertEqual(exc.code, code)
            else:
                self.fail("Should have raised SystemExit")

    def test_cli_help(self):
        """test simple commands users are likely to issue."""
        self.check_cmdline_exit([], code=2)
        self.check_cmdline_exit(['-h'])
        self.check_cmdline_exit(['--help'])
        self.check_cmdline_exit(['dir', '-h'])
        self.check_cmdline_exit(['dir', 'add', '-h'])
        self.check_cmdline_exit(['tape', '-h'])
        self.check_cmdline_exit(['drive', '-h'])

    def test_cli_basic(self):
        """test simple valid and invalid command lines."""
        self.check_cmdline_valid(['dir', 'list'])
        self.check_cmdline_valid(['dir', 'add', '--unlock', 'toto'])
        self.check_cmdline_valid(['dir', 'add', 'A', 'B', 'C'])

        # Test invalid object and invalid verb
        self.check_cmdline_exit(['voynichauthor', 'show'], code=2)
        self.check_cmdline_exit(['dir', 'teleport'], code=2)

    def test_cli_tape(self):
        """ test tape add commands"""
        #Test differents types of tape name format
        PhobosActionContext(['-c', '../../tests/phobos.conf', 'tape', 'add',
                             '-t', 'LTO6', '--fs', 'LTFS', 'STANDARD[0000-1000]']).run()
        PhobosActionContext(['-c', '../../tests/phobos.conf', 'tape', 'add',
                             '-t', 'LTO6', '--fs', 'LTFS', 'TE[000-666]st']).run()
        PhobosActionContext(['-c', '../../tests/phobos.conf', 'tape', 'add',
                             '-t', 'LTO6', '--fs', 'LTFS', 'ABC,DEF,XZE,AQW']).run()
        PhobosActionContext(['-c', '../../tests/phobos.conf', 'tape', 'lock',
                             'STANDARD[0000-0200]']).run()
        PhobosActionContext(['-c', '../../tests/phobos.conf', 'tape', 'unlock',
                             'STANDARD[0000-0100]']).run()
        PhobosActionContext(['-c', '../../tests/phobos.conf', 'tape', 'unlock',
                             '--force', 'STANDARD[0000-0200]']).run()

    def test_cli_log(self):
        """
        Due to the way C and python layers are sandwitched when handling error
        messages we have see very obscure crashes. Make sure that error codes
        from lower layers get properly propagated up to the python callers.
        """
        cli = Client()
        cli.connect(dbname='phobos', user='phobos', password='phobos')

        dev = dev_info()
        dev.family = PHO_DEV_DIR
        dev.model = ''
        dev.path = '/tmp/test_%d' % randint(0, 1000000)
        dev.host = 'localhost'
        dev.serial = '__TEST_MAGIC_%d' % randint(0, 1000000)

        cli.devices.insert([dev])
        rc = cli.devices.insert([dev])

        self.assertEqual(rc, -errno.ECOMM)

        cli.disconnect()

if __name__ == '__main__':
    unittest.main()
