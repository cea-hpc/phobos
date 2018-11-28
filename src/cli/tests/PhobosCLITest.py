#!/usr/bin/python

#
#  All rights reserved (c) 2014-2017 CEA/DAM.
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

"""Unit tests for phobos.cli"""

import os
import sys
import errno
import unittest
import tempfile

from contextlib import contextmanager
from StringIO import StringIO
from random import randint
from socket import gethostname

from phobos.cli import PhobosActionContext
from phobos.core.dss import Client
from phobos.core.ffi import DevInfo
from phobos.core.const import PHO_DEV_DIR, PHO_DEV_TAPE

def gethostname_short():
    """Return short hostname"""
    return gethostname().split('.')[0]

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
    """Base class to execute CLI and check return codes."""
    def check_cmdline_valid(self, args):
        """Make sure a command line is seen as valid."""
        PhobosActionContext(args)

    def check_cmdline_exit(self, args, code=0):
        """Make sure a command line exits with a given error code."""
        print ' '.join(args)
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
        self.check_cmdline_exit(['lib', '-h'])

    def test_cli_basic(self):
        """test simple valid and invalid command lines."""
        self.check_cmdline_valid(['dir', 'list'])
        self.check_cmdline_valid(['dir', 'add', '--unlock', 'toto'])
        self.check_cmdline_valid(['dir', 'add', 'A', 'B', 'C'])
        self.check_cmdline_valid(['dir', 'show', 'A,B,C'])
        self.check_cmdline_valid(['tape', 'add', '-t', 'LTO5', 'I,J,K'])
        self.check_cmdline_valid(['tape', 'show', 'I,J,K'])

        # Test invalid object and invalid verb
        self.check_cmdline_exit(['voynichauthor', 'show'], code=2)
        self.check_cmdline_exit(['dir', 'teleport'], code=2)


class BasicExecutionTest(unittest.TestCase):
    """Ease execution of the CLI."""
    # Reuse configuration file from global tests
    TEST_CFG_FILE = "../../tests/phobos.conf"
    def pho_execute(self, params, auto_cfg=True, code=0):
        """Instanciate and execute a PhobosActionContext."""
        if auto_cfg:
            params = ['-c', self.TEST_CFG_FILE] + params

        try:
            PhobosActionContext(params).run()
        except SystemExit as exc:
            self.assertEqual(exc.code, code)
        else:
            if code:
                self.fail("SystemExit with code %d expected" % code)


class MediaAddTest(BasicExecutionTest):
    """
    This sub-test suite adds tapesand makes sure that both regular and abnormal
    cases are handled properly.

    Note that what we are in the CLI tests and therefore try to specifically
    exercise the upper layers (command line parsing etc.)
    """
    def test_tape_add(self):
        """test adding tapes. Simple case."""
        #Test differents types of tape name format
        self.pho_execute(['tape', 'add', '-t', 'LTO6', '--fs', 'LTFS',
                          'STANDARD0[000-100]'])
        self.pho_execute(['tape', 'add', '-t', 'LTO6', '--fs', 'LTFS',
                          'TE[000-666]st'])
        self.pho_execute(['tape', 'add',
                          '-t', 'LTO6', '--fs', 'LTFS', 'ABC,DEF,XZE,AQW'])
        self.pho_execute(['tape', 'lock', 'STANDARD0[000-100]'])
        self.pho_execute(['tape', 'unlock', 'STANDARD0[000-050]'])
        self.pho_execute(['tape', 'unlock', '--force', 'STANDARD0[000-100]'])
        self.pho_execute(['tape', 'add', '-t', 'LTO6', '--fs', 'LTFS',
                          'TAGGED0', '--tags', 'tag-foo'])
        self.pho_execute(['tape', 'add', '-t', 'LTO6', '--fs', 'LTFS',
                          'TAGGED1', '--tags', 'tag-foo,tag-bar'])
        for (tape, foo_count, bar_count) in [
                ('TAGGED0', 1, 0),
                ('TAGGED1', 1, 1),
        ]:
            with output_intercept() as (stdout, _):
                self.pho_execute(['tape', 'show', tape])
            output = stdout.getvalue()
            self.assertEqual(output.count('tag-foo'), foo_count)
            self.assertEqual(output.count('tag-bar'), bar_count)

    def test_tape_add_lowercase(self):
        """Express tape technology in lowercase in the command line (PHO-67)."""
        self.pho_execute(['tape', 'add', 'B0000[5-9]L5', '-t', 'lto5'])
        self.pho_execute(['tape', 'add', 'C0000[5-9]L5', '-t', 'lto5',
                          '--fs', 'ltfs'])

    def test_tape_invalid_const(self):
        """Unknown FS type should raise an error."""
        self.pho_execute(['tape', 'add', 'D000[10-15]', '-t', 'LTO5',
                          '--fs', 'FooBarFS'], code=os.EX_DATAERR)
        self.pho_execute(['tape', 'add', 'E000[10-15]', '-t', 'BLAH'],
                         code=os.EX_DATAERR)

    def test_tape_model_case_insentive_match(self):
        self.pho_execute(['tape', 'add', 'F000[10-15]', '-t', 'lto8'])
        self.pho_execute(['tape', 'add', 'G000[10-15]', '-t', 'lTo8'])
        self.pho_execute(['tape', 'add', 'H000[10-15]', '-t', 'LTO8'])
        self.pho_execute(['tape', 'add', 'I000[10-15]', '-t', 'raTaTouille'])


class DeviceAddTest(BasicExecutionTest):
    """
    This sub-test suite adds devices (drives and directories) and makes sure
    that both regular and abnormal cases are handled properly.
    """
    def test_dir_add(self):
        """test adding directories. Simple case."""
        flist = []
        for i in range(5):
            file = tempfile.NamedTemporaryFile()
            self.pho_execute(['-v', 'dir', 'add', file.name])
            flist.append(file)

        for file in flist:
            path = "%s:%s" % (gethostname_short(), file.name)
            self.pho_execute(['-v', 'dir', 'show', path])

    def test_dir_add_missing(self):
        """Add a non-existent directory should raise an error."""
        self.pho_execute(['-v', 'dir', 'add', '/tmp/nonexistentfileAA'],
                         code=os.EX_DATAERR)
        self.pho_execute(['-v', 'drive', 'add', '/dev/IMBtape0 /dev/IBMtape1'],
                         code=os.EX_DATAERR)

    def test_dir_add_double(self):
        """Add a directory twice should raise an error."""
        file = tempfile.NamedTemporaryFile()
        self.pho_execute(['-v', 'dir', 'add', file.name])
        self.pho_execute(['-v', 'dir', 'add', file.name], code=os.EX_DATAERR)


if __name__ == '__main__':
    unittest.main()
