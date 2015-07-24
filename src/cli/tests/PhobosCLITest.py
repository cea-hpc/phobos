#!/usr/bin/python

# Phobos project - CEA/DAM
# Henri Doreau <henri.doreau@cea.fr>

"""Unit tests for phobos.cli"""

import sys
import unittest

from contextlib import contextmanager
from StringIO import StringIO

from phobos.cli import PhobosActionContext


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
        PhobosActionContext(args[1::])

    def check_cmdline_exit(self, args, code=0):
        """Make sure a command line exits with a given error code."""
        with self.assertRaises(SystemExit) as cm:
            with output_intercept() as (stdout, stderr):
                PhobosActionContext(args[1::])
        self.assertEqual(cm.exception.code, code)

    def test_cli_help(self):
        """test simple commands users are likely to issue."""
        self.check_cmdline_exit(['phobos'], code=2)
        self.check_cmdline_exit(['phobos', '-h'])
        self.check_cmdline_exit(['phobos', '--help'])
        self.check_cmdline_exit(['phobos', 'dir', '-h'])
        self.check_cmdline_exit(['phobos', 'dir', 'add', '-h'])
        self.check_cmdline_exit(['phobos', 'tape', '-h'])
        self.check_cmdline_exit(['phobos', 'drive', '-h'])

    def test_cli_basic(self):
        """test simple valid and invalid command lines."""
        self.check_cmdline_valid(['phobos', 'dir', 'list'])
        self.check_cmdline_valid(['phobos', 'dir', 'add', '--unlock', 'toto'])
        self.check_cmdline_valid(['phobos', 'dir', 'add', 'A', 'B', 'C'])

        # Test invalid object and invalid verb
        self.check_cmdline_exit(['phobos', 'voynichauthor', 'show'], code=2)
        self.check_cmdline_exit(['phobos', 'dir', 'teleport'], code=2)

if __name__ == '__main__':
    unittest.main()
