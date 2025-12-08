#!/usr/bin/env python3

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

"""Unit tests for phobos CLI"""

import errno
import os
import sys
import tempfile
import unittest

from contextlib import contextmanager
from io import StringIO
from socket import gethostname

from phobos import HANDLERS
from phobos.cli.common import PhobosActionContext
from phobos.core.dss import MediaManager
import phobos.core.cfg as cfg

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
    Base class to execute CLI and check return codes.
    This test exerts phobos command line parser with valid and invalid
    combinations.
    """
    @staticmethod
    def check_cmdline_valid(args):
        """Make sure a command line is seen as valid."""
        PhobosActionContext(HANDLERS, args)

    def check_cmdline_exit(self, args, code=0):
        """Make sure a command line exits with a given error code."""
        print('"' + ' '.join(args) + '"')
        with output_intercept():
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
        self.check_cmdline_exit(['object', '-h'])
        self.check_cmdline_exit(['extent', '-h'])
        self.check_cmdline_exit(['logs', '-h'])
        self.check_cmdline_exit(['phobosd', '-h'])
        self.check_cmdline_exit(['tlc', '-h'])

    def test_cli_basic(self): # pylint: disable=too-many-statements
        """test simple valid and invalid command lines."""
        self.check_cmdline_valid(['dir', 'list'])
        self.check_cmdline_valid(['dir', 'add', '--unlock', 'toto'])
        self.check_cmdline_valid(['dir', 'add', 'A', 'B', 'C'])
        self.check_cmdline_valid(['dir', 'list', 'A,B,C', '-o', 'all'])
        self.check_cmdline_valid(['dir', 'list', 'A,B,C', '-o', '*'])
        self.check_cmdline_valid(['dir', 'list', 'A,B,C', '-o', 'name,family'])
        self.check_cmdline_valid(['drive', 'delete', 'A'])
        self.check_cmdline_valid(['drive', 'delete', 'A', 'B', 'C'])
        self.check_cmdline_valid(['drive', 'del', 'A'])
        self.check_cmdline_valid(['drive', 'del', 'A', 'B', 'C'])
        self.check_cmdline_valid(['tape', 'add', '-t', 'LTO5', 'I,J,K'])
        self.check_cmdline_valid(['tape', 'list', 'I,J,K', '-o', 'all'])
        self.check_cmdline_valid(['tape', 'list', 'I,J,K', '-o', '*'])
        self.check_cmdline_valid(['tape', 'list', 'I,J,K', '-o',
                                  'name,family'])
        self.check_cmdline_valid(['tape', 'repack', 'A'])
        self.check_cmdline_valid(['tape', 'repack', '-T', 't1,t2', 'A'])
        self.check_cmdline_valid(['object', 'list'])
        self.check_cmdline_valid(['object', 'list', '"obj.*"'])
        self.check_cmdline_valid(['object', 'list', '"obj.?2"'])
        self.check_cmdline_valid(['object', 'list', '"obj[0-9]*"'])
        self.check_cmdline_valid(['object', 'list', '"obj[[:digit:]]*"'])
        self.check_cmdline_valid(['object', 'list', '-o', 'all'])
        self.check_cmdline_valid(['object', 'list', '-o', '*'])
        self.check_cmdline_valid(['object', 'list', '-o', 'oid'])
        self.check_cmdline_valid(['object', 'list', '-o', 'all', '"obj.*"'])
        self.check_cmdline_valid(['object', 'list', '-m', 'user=foo,test=abd'])
        self.check_cmdline_valid(['object', 'list', '--deprecated'])
        self.check_cmdline_valid(['object', 'list', '--deprecated-only'])
        self.check_cmdline_valid(['object', 'list', '--version', '1'])
        self.check_cmdline_valid(['object', 'list', '--uuid', 'uuid1', 'oid'])
        self.check_cmdline_valid(['object', 'list', '--pattern', 'oid',
                                  'blob'])
        self.check_cmdline_valid(['extent', 'list'])
        self.check_cmdline_valid(['extent', 'list', '"obj.*"'])
        self.check_cmdline_valid(['extent', 'list', '-o', 'all'])
        self.check_cmdline_valid(['extent', 'list', '-o', '*'])
        self.check_cmdline_valid(['extent', 'list', '-o', 'oid'])
        self.check_cmdline_valid(['extent', 'list', '-o', 'all', '"obj.*"'])
        self.check_cmdline_valid(['extent', 'list', '-n', 't01'])
        self.check_cmdline_valid(['extent', 'list', '-n', 't01', '"obj.*"'])
        self.check_cmdline_valid(['extent', 'list', '--degroup'])
        self.check_cmdline_valid(['extent', 'list', '--degroup', '"obj.*"'])
        self.check_cmdline_valid(['extent', 'list', '--degroup', '-o', 'all'])
        self.check_cmdline_valid(['extent', 'list', '--degroup', '-n', 't01'])
        self.check_cmdline_valid(['extent', 'list', '--pattern', 'blob'])
        self.check_cmdline_valid(['extent', 'list', '-n', 't01', '--pattern',
                                  'blob'])
        self.check_cmdline_valid(['extent', 'list', '--pattern',
                                  'blob', 'lorem'])
        self.check_cmdline_valid(['delete', 'oid'])
        self.check_cmdline_valid(['delete', 'oid1', 'oid2', 'oid3'])
        self.check_cmdline_valid(['del', 'oid'])
        self.check_cmdline_valid(['del', 'oid1', 'oid2', 'oid3'])
        self.check_cmdline_valid(['del', 'oid', '--hard'])
        self.check_cmdline_valid(['undelete', 'oid1'])
        self.check_cmdline_valid(['undelete', 'oid1', '--uuid', 'uuid1'])
        self.check_cmdline_valid(['undelete', 'oid1', 'oid2'])
        self.check_cmdline_valid(['undel', 'oid1'])
        self.check_cmdline_valid(['undel', 'oid1', '--uuid', 'uuid1'])
        self.check_cmdline_valid(['undel', 'oid1', 'oid2'])
        self.check_cmdline_valid(['phobosd', 'ping'])
        self.check_cmdline_valid(['tlc', 'ping'])
        self.check_cmdline_valid(['locate', 'oid1'])
        self.check_cmdline_valid(['locate', '--focus-host', 'vm0', 'oid1'])
        self.check_cmdline_valid(['locate', '--uuid', 'uuid1', 'oid1'])
        self.check_cmdline_valid(['locate', '--version', '1', 'oid1'])
        self.check_cmdline_valid(['locate', '--uuid', 'uuid1',
                                  '--version', '1', 'oid1'])
        self.check_cmdline_valid(['dir', 'locate', 'oid1'])
        self.check_cmdline_valid(['tape', 'locate', 'oid1'])
        self.check_cmdline_valid(['get', '--best-host', 'oid1', 'dest'])
        self.check_cmdline_valid(['get', '--version', '1', 'objid', 'file'])
        self.check_cmdline_valid(['get', '--uuid', 'uuid', 'objid', 'file'])
        self.check_cmdline_valid(['get', '--copy-name', 'copy', 'objid',
                                  'file'])
        self.check_cmdline_valid(['get', '--version', '1', '--uuid', 'uuid',
                                  'objid', 'file'])
        self.check_cmdline_valid(['put', '--lyt-params', 'a=b', 'src', 'oid'])
        self.check_cmdline_valid(['put', '--lyt-params', 'a=b,c=d', 'src',
                                  'oid'])
        self.check_cmdline_valid(['put', '--grouping', 'my_grouping', 'src',
                                  'oid'])
        self.check_cmdline_valid(['put', '--copy-name', 'copy', 'src', 'oid'])
        self.check_cmdline_valid(['drive', 'lookup', 'drive_serial_or_path'])
        self.check_cmdline_valid(['drive', 'load', 'drive_serial_or_path',
                                  'tape_label'])
        self.check_cmdline_valid(['drive', 'unload', 'drive_serial_or_path'])
        self.check_cmdline_valid(['drive', 'unload', '--tape-label',
                                  'tape_label', 'drive_serial_or_path'])
        self.check_cmdline_valid(['lib', 'scan'])
        self.check_cmdline_valid(['lib', 'scan', '--refresh'])
        self.check_cmdline_valid(['lib', 'refresh'])
        self.check_cmdline_valid(['tape', 'import', '-t', 'lto5', 'name'])
        self.check_cmdline_valid(['tape', 'import', '-t', 'lto5', 'A', 'B',
                                  'C'])
        self.check_cmdline_valid(['tape', 'import', '-t', 'lto5',
                                  '--unlock', 'A'])
        self.check_cmdline_valid(['drive', 'migrate', '--host', 'vm0',
                                  'drive_serial_or_path'])
        self.check_cmdline_valid(['drive', 'migrate', '--new-library', 'blob',
                                  'drive_serial_or_path'])
        self.check_cmdline_valid(['drive', 'migrate', '--host', 'vm0',
                                  'new-library', 'blob',
                                  'drive_serial_or_path'])
        self.check_cmdline_valid(['tape', 'rename', '--new-library', 'blob',
                                  'name'])
        self.check_cmdline_valid(['tape', 'rename', '--library', 'legacy',
                                  '--new-library', 'blob', 'name'])
        self.check_cmdline_valid(['dir', 'rename', '--new-library', 'blob',
                                  'name'])
        self.check_cmdline_valid(['dir', 'rename', '--library', 'legacy',
                                  '--new-library', 'blob', 'name'])
        self.check_cmdline_valid(['copy', 'create', 'oid', 'name'])
        self.check_cmdline_valid(['copy', 'create', '--family', 'tape', 'oid',
                                  'name'])
        self.check_cmdline_valid(['copy', 'create', '--profile', 'p1', 'oid',
                                  'name'])
        self.check_cmdline_valid(['copy', 'create', '--lyt-params', 'a=b',
                                  'oid', 'name'])
        self.check_cmdline_valid(['copy', 'delete', 'oid', 'name'])
        self.check_cmdline_valid(['copy', 'list', 'oid'])

        # Test invalid object and invalid verb
        self.check_cmdline_exit(['get', '--version', 'nan', 'objid', 'file'],
                                code=2)
        self.check_cmdline_exit(['voynichauthor', 'list'], code=2)
        self.check_cmdline_exit(['dir', 'teleport'], code=2)
        self.check_cmdline_exit(['delete'], code=2)
        self.check_cmdline_exit(['undelete'], code=2)
        self.check_cmdline_exit(['undelete', 'oid1', '--uuid'], code=2)
        self.check_cmdline_exit(['locate'], code=2)
        self.check_cmdline_exit(['locate', 'oid1', 'oid2'], code=2)
        self.check_cmdline_exit(['locate', '--focus-host', 'oid1'], code=2)
        self.check_cmdline_exit(['locate', '--uuid', 'oid1'], code=2)
        self.check_cmdline_exit(['locate', '--version', 'blob', 'oid1'], code=2)
        self.check_cmdline_exit(['dir', 'locate'], code=2)
        self.check_cmdline_exit(['dir', 'locate', 'oid1', 'oid2'], code=2)
        self.check_cmdline_exit(['tape', 'locate'], code=2)
        self.check_cmdline_exit(['tape', 'locate', 'oid1', 'oid2'], code=2)
        self.check_cmdline_exit(['tape', 'repack', 'A', 'B'], code=2)
        self.check_cmdline_exit(['drive', 'delete'], code=2)
        self.check_cmdline_exit(['phobosd'], code=2)
        self.check_cmdline_exit(['tlc'], code=2)
        self.check_cmdline_exit(['phobosd', 'bad_action'], code=2)
        self.check_cmdline_exit(['tlc', 'bad_action'], code=2)
        self.check_cmdline_exit(['drive', 'lookup'], code=2)
        self.check_cmdline_exit(['drive', 'load'], code=2)
        self.check_cmdline_exit(['drive', 'load', 'only_drive_no_tape'], code=2)
        self.check_cmdline_exit(['drive', 'unload', 'drive_serial_or_path',
                                 'tape-label'], code=2)
        self.check_cmdline_exit(['tape', 'import'], code=2)
        self.check_cmdline_exit(['tape', 'import', 'A'], code=2)
        self.check_cmdline_exit(['drive', 'migrate', '--host', 'vm0'], code=2)
        self.check_cmdline_exit(['drive', 'migrate', '--new-library', 'blob'],
                                code=2)
        self.check_cmdline_exit(['tape', 'rename', '--new-library', 'blob'],
                                code=2)
        self.check_cmdline_exit(['dir', 'rename', '--new-library', 'blob'],
                                code=2)

    def test_cli_logs_command(self): # pylint: disable=too-many-statements
        """Check logs specific commands"""
        self.check_cmdline_valid(['logs', 'clear'])
        self.check_cmdline_valid(['logs', 'clear', '--drive', '42'])
        self.check_cmdline_valid(['logs', 'clear', '-D', '42'])
        self.check_cmdline_valid(['logs', 'clear', '--tape', '42'])
        self.check_cmdline_valid(['logs', 'clear', '-T', '42'])
        self.check_cmdline_valid(['logs', 'clear', '--errno', '42'])
        self.check_cmdline_valid(['logs', 'clear', '-e', '42'])
        self.check_cmdline_valid(['logs', 'clear', '--cause', 'library_scan'])
        self.check_cmdline_valid(['logs', 'clear', '-c', 'library_scan'])
        self.check_cmdline_valid(['logs', 'clear', '--start', '1234-12-12'])
        self.check_cmdline_valid(['logs', 'clear', '--start',
                                  '1234-12-12 23:59:59'])
        self.check_cmdline_valid(['logs', 'clear', '--end', '1234-12-12'])
        self.check_cmdline_valid(['logs', 'clear', '--end',
                                  '1234-12-12 23:59:59'])
        self.check_cmdline_valid(['logs', 'clear', '--start', '1234-01-01',
                                  '--end', '1234-01-01'])
        self.check_cmdline_valid(['logs', 'clear', '--clear-all'])

        self.check_cmdline_valid(['logs', 'dump'])
        self.check_cmdline_valid(['logs', 'dump', '--drive', '42'])
        self.check_cmdline_valid(['logs', 'dump', '-D', '42'])
        self.check_cmdline_valid(['logs', 'dump', '--tape', '42'])
        self.check_cmdline_valid(['logs', 'dump', '-T', '42'])
        self.check_cmdline_valid(['logs', 'dump', '--errno', '42'])
        self.check_cmdline_valid(['logs', 'dump', '-e', '42'])
        self.check_cmdline_valid(['logs', 'dump', '--cause', 'library_scan'])
        self.check_cmdline_valid(['logs', 'dump', '-c', 'library_scan'])
        self.check_cmdline_valid(['logs', 'dump', '--start', '1234-12-12'])
        self.check_cmdline_valid(['logs', 'dump', '--start',
                                  '1234-12-12 23:59:59'])
        self.check_cmdline_valid(['logs', 'dump', '--end', '1234-12-12'])
        self.check_cmdline_valid(['logs', 'dump', '--end',
                                  '1234-12-12 23:59:59'])
        self.check_cmdline_valid(['logs', 'dump', '--start', '1234-01-01',
                                  '--end', '1234-01-01'])

        self.check_cmdline_exit(['logs'], code=2)

        self.check_cmdline_exit(['logs', 'clear', '42'], code=2)
        self.check_cmdline_exit(['logs', 'clear', '--errno', 'blob'], code=2)
        self.check_cmdline_exit(['logs', 'clear', '--cause', 'blob'], code=2)
        self.check_cmdline_exit(['logs', 'clear', '--start', 'blob'], code=2)
        self.check_cmdline_exit(['logs', 'clear', '--start', '1234-12-'],
                                code=2)
        self.check_cmdline_exit(['logs', 'clear', '--start', '1234'],
                                code=2)
        self.check_cmdline_exit(['logs', 'clear', '--start',
                                 '1234-12-12 52:58:47:85'], code=2)
        self.check_cmdline_exit(['logs', 'clear', '--end', 'blob'], code=2)
        self.check_cmdline_exit(['logs', 'clear', '--end', '1234-12-'],
                                code=2)
        self.check_cmdline_exit(['logs', 'clear', '--end', '1234'],
                                code=2)
        self.check_cmdline_exit(['logs', 'clear', '--end',
                                 '1234-12-12 52:58:47:85'], code=2)
        self.check_cmdline_exit(['logs', 'clear', '--start', '1234-01-01',
                                 '--end', '1234-01-'], code=2)

        self.check_cmdline_exit(['logs', 'dump', '42'], code=2)
        self.check_cmdline_exit(['logs', 'dump', '--errno', 'blob'], code=2)
        self.check_cmdline_exit(['logs', 'dump', '--cause', 'blob'], code=2)
        self.check_cmdline_exit(['logs', 'dump', '--start', 'blob'], code=2)
        self.check_cmdline_exit(['logs', 'dump', '--start', '1234-12-'],
                                code=2)
        self.check_cmdline_exit(['logs', 'dump', '--start', '1234'],
                                code=2)
        self.check_cmdline_exit(['logs', 'dump', '--start',
                                 '1234-12-12 52:58:47:85'], code=2)
        self.check_cmdline_exit(['logs', 'dump', '--end', 'blob'], code=2)
        self.check_cmdline_exit(['logs', 'dump', '--end', '1234-12-'],
                                code=2)
        self.check_cmdline_exit(['logs', 'dump', '--end', '1234'],
                                code=2)
        self.check_cmdline_exit(['logs', 'dump', '--end',
                                 '1234-12-12 52:58:47:85'], code=2)
        self.check_cmdline_exit(['logs', 'dump', '--start', '1234-01-01',
                                 '--end', '1234-01-'], code=2)


class BasicExecutionTest(unittest.TestCase):
    """Base execution of the CLI."""
    # Reuse configuration file from global tests
    TEST_CFG_FILE = "../../../tests/phobos.conf"
    def get_test_db_name(self):
        """Get the test database name"""
        try:
            cfg.load_file(self.TEST_CFG_FILE)
        except IOError as exc:
            self.fail(exc)

        for value in cfg.get_val("dss", "connect_string").split(' '):
            key_value = value.split('=')
            if key_value[0] == "dbname":
                return key_value[1]

        return "phobos"

    def pho_execute(self, params, auto_cfg=True, code=0):
        """Instanciate and execute a PhobosActionContext."""
        if auto_cfg:
            params = ['-c', self.TEST_CFG_FILE] + params

        try:
            with PhobosActionContext(HANDLERS, params) as pac:
                pac.run()
        except SystemExit as exc:
            self.assertEqual(exc.code, code)
        else:
            if code:
                self.fail("SystemExit with code %d expected" % code)

    def pho_execute_capture(self, *args, **kwargs):
        """Same as pho_execute but captures and returns (stdout, stderr)"""
        with output_intercept() as (stdout, stderr):
            self.pho_execute(*args, **kwargs)
        return stdout.getvalue(), stderr.getvalue()


class MediaAddTest(BasicExecutionTest):
    """
    This sub-test suite adds tapes and makes sure that both regular and abnormal
    cases are handled properly.

    Note that what we are in the CLI tests and therefore try to specifically
    exercise the upper layers (command line parsing etc.)
    """
    def test_tape_add(self):
        """test adding tapes. Simple case."""
        # Test different types of tape name format
        self.pho_execute(['tape', 'add', '-t', 'LTO6', '--fs', 'LTFS',
                          'STANDARD0[000-100]'])
        self.pho_execute(['tape', 'add', '-t', 'LTO6', '--fs', 'LTFS',
                          'TE[000-666]st'])
        self.pho_execute(['tape', 'add',
                          '-t', 'LTO6', '--fs', 'LTFS', 'ABC,DEF,XZE,AQW'])
        self.pho_execute(['tape', 'lock', 'STANDARD0[000-100]'])
        self.pho_execute(['tape', 'unlock', 'STANDARD0[000-050]'])
        self.pho_execute(['tape', 'unlock', '--force', 'STANDARD0[000-100]'])

    def test_add_tags(self):
        """Test that tags are properly added"""
        self.pho_execute(['tape', 'add', '-t', 'LTO6', '--fs', 'LTFS',
                          'TAGGED0', '--tags', 'tag-foo'])
        self.pho_execute(['tape', 'add', '-t', 'LTO6', '--fs', 'LTFS',
                          'TAGGED1', '--tags', 'tag-foo,tag-bar'])

        # Check that tags have successfully been added
        output, _ = self.pho_execute_capture(['tape', 'list', '--output',
                                              'tags', 'TAGGED0'])
        self.assertEqual(output.strip(), "['tag-foo']")
        output, _ = self.pho_execute_capture(['tape', 'list', '--output',
                                              'tags', 'TAGGED1'])
        self.assertEqual(output.strip(), "['tag-foo', 'tag-bar']")

    def test_media_update(self):
        """test updating media."""
        self.pho_execute(['tape', 'add', '-t', 'LTO6', '--fs', 'LTFS',
                          'update0', '--tags', 'tag-foo'])
        self.pho_execute(['tape', 'add', '-t', 'LTO6', '--fs', 'LTFS',
                          'update1', '--tags', 'tag-foo,tag-bar'])

        # Check inserted media
        output, _ = self.pho_execute_capture(['tape', 'list', '--output',
                                              'tags', 'update0'])
        self.assertEqual(output.strip(), "['tag-foo']")
        output, _ = self.pho_execute_capture(['tape', 'list', '--output',
                                              'tags', 'update1'])
        self.assertEqual(output.strip(), "['tag-foo', 'tag-bar']")

        # Update media
        self.pho_execute(['tape', 'update', '-T', 'new-tag1,new-tag2',
                          'update[0-1]'])

        # Check updated media
        for med_id in "update0", "update1":
            output, _ = self.pho_execute_capture(['tape', 'list', '--output',
                                                  'tags', med_id])
            self.assertEqual(output.strip(), "['new-tag1', 'new-tag2']")

        # No '-T' argument does nothing
        self.pho_execute(['tape', 'update', 'update0'])
        output, _ = self.pho_execute_capture(['tape', 'list', '--output',
                                              'tags', 'update0'])
        self.assertEqual(output.strip(), "['new-tag1', 'new-tag2']")

        # Test a failed update
        def failed_update(*args, **kwargs): # pylint: disable=unused-argument
            """Emulates a failed update by raising EnvironmentError"""
            raise EnvironmentError(errno.ENOENT, "Expected failed")

        old_update = MediaManager.update
        MediaManager.update = failed_update
        try:
            self.pho_execute(['tape', 'update', '-T', '', 'update0'],
                             code=errno.ENOENT)
        finally:
            MediaManager.update = old_update

        # Ensure that the tape is unlocked after failure
        # Exactly one media should be returned
        output, _ = self.pho_execute_capture(['tape', 'list', '--output',
                                              'lock_hostname', 'update0'])
        test = output.strip() == 'None' or output.strip() == ''
        self.assertTrue(test)

        test_db_name = self.get_test_db_name()

        request = "insert into lock (type, id, hostname, owner) values \
                    (\'media\'::lock_type, \'update0\', \'dummy\', 1337);"
        # Check that locked tapes can be updated
        os.system('psql ' + test_db_name + ' phobos -c "' + request + '"')
        try:
            self.pho_execute(['tape', 'update', '-T', '', 'update0'])
        finally:
            os.system('psql ' + test_db_name + ' phobos -c \
                      "delete from lock where type = \'media\'::lock_type \
                                              and id = \'update0\';"')

    def test_tape_add_lowercase(self):
        """Express tape technology in lowercase in the command line (PHO-67)."""
        self.pho_execute(['tape', 'add', 'B0000[5-9]L5', '-t', 'lto5'])
        self.pho_execute(['tape', 'add', 'C0000[5-9]L5', '-t', 'lto5',
                          '--fs', 'ltfs'])

    def test_tape_invalid_const(self):
        """Unknown FS type should raise an error."""
        self.pho_execute(['tape', 'add', 'D000[10-15]', '-t', 'LTO5',
                          '--fs', 'FooBarFS'], code=2) # argparse check
        self.pho_execute(['tape', 'add', 'E000[10-15]', '-t', 'BLAH'],
                         code=errno.EINVAL)

    def test_tape_model_case_insensitive_match(self): # pylint: disable=invalid-name
        """Test tape type is case insensitive."""
        self.pho_execute(['tape', 'add', 'F000[10-15]', '-t', 'lto8'])
        self.pho_execute(['tape', 'add', 'G000[10-15]', '-t', 'lTo8'])
        self.pho_execute(['tape', 'add', 'H000[10-15]', '-t', 'LTO8'])
        self.pho_execute(['tape', 'add', 'I000[10-15]', '-t', 'raTaTouille'])


class MediumListTest(BasicExecutionTest):
    """
    This sub-test suite adds tapes and makes sure that phobos list them
    according to their tags.

    Note that we are in the CLI tests and therefore try to specifically
    exercise the upper layers (command line parsing etc.)
    """
    def add_media(self):
        """Context function which adds media."""
        self.pho_execute(['tape', 'add', 'm0', '-t', 'lto8'])
        self.pho_execute(['tape', 'add', 'm1', '-t', 'lto8', '-T', 'foo'])
        self.pho_execute(['tape', 'add', 'm2', '-t', 'lto8', '-T', 'foo,bar'])
        self.pho_execute(['tape', 'add', 'm3', '-t', 'lto8', '-T', 'goo,foo'])
        self.pho_execute(['tape', 'add', 'm4', '-t', 'lto8', '-T', 'bar,goo'])
        self.pho_execute(['tape', 'add', 'm5', '-t', 'lto8', \
                          '-T', 'foo,bar,goo'])

    def test_med_list_tags(self):
        """Test added media with tags are correctly listed."""
        self.add_media()
        output, _ = self.pho_execute_capture(['tape', 'list', '-T', 'foo'])
        self.assertNotIn("m0", output)
        self.assertIn("m1", output)
        self.assertIn("m2", output)
        self.assertIn("m3", output)
        self.assertNotIn("m4", output)
        self.assertIn("m5", output)

        output, _ = self.pho_execute_capture(['tape', 'list', '-T', 'goo'])
        self.assertNotIn("m0", output)
        self.assertNotIn("m1", output)
        self.assertNotIn("m2", output)
        self.assertIn("m3", output)
        self.assertIn("m4", output)
        self.assertIn("m5", output)

        output, _ = self.pho_execute_capture(['tape', 'list', '-T', 'foo,bar'])
        self.assertNotIn("m0", output)
        self.assertNotIn("m1", output)
        self.assertIn("m2", output)
        self.assertNotIn("m3", output)
        self.assertNotIn("m4", output)
        self.assertIn("m5", output)


class DeviceAddTest(BasicExecutionTest):
    """
    This sub-test suite adds devices (drives and directories) and makes sure
    that both regular and abnormal cases are handled properly.
    """
    def test_dir_add(self):
        """Test adding directories. Simple case."""
        flist = []
        for _ in range(5):
            file = tempfile.TemporaryDirectory()
            self.pho_execute(['-v', 'dir', 'add', file.name])
            flist.append(file)

        for file in flist:
            path = "%s:%s" % (gethostname_short(), file.name)
            self.pho_execute(['-v', 'dir', 'list', '-o', 'all', path])

    def test_dir_tags(self):
        """Test adding a directory with tags."""
        tmp_f = tempfile.TemporaryDirectory()
        tmp_path = tmp_f.name
        self.pho_execute(['dir', 'add', tmp_path, '--tags', 'tag-foo,tag-bar'])
        output, _ = self.pho_execute_capture(['dir', 'list', '-o', 'all',
                                              tmp_path])
        self.assertIn("['tag-foo', 'tag-bar']", output)

    def test_dir_update(self):
        """Test updating a directory."""
        tmp_f = tempfile.TemporaryDirectory()
        tmp_path = tmp_f.name
        self.pho_execute(['dir', 'add', tmp_path, '--tags', 'tag-baz'])

        # Check inserted media
        output, _ = self.pho_execute_capture(['dir', 'list', '--output', 'tags',
                                              tmp_path])
        self.assertEqual(output.strip(), "['tag-baz']")

        # Update media
        self.pho_execute(['dir', 'update', '-T', '', tmp_path])

        # Check updated media
        output, _ = self.pho_execute_capture(['dir', 'list', '--output', 'tags',
                                              tmp_path])
        self.assertEqual(output.strip(), "[]")

    def test_dir_add_missing(self):
        """Adding a non-existent directory should raise an error."""
        self.pho_execute(['-v', 'dir', 'add', '/tmp/nonexistentfileAA'],
                         code=errno.ENOENT)
        self.pho_execute(['-v', 'drive', 'add', '/dev/IMBtape0 /dev/IBMtape1'],
                         code=errno.ENOENT)

    def test_dir_add_double(self):
        """Adding a directory twice should raise an error."""
        file = tempfile.TemporaryDirectory()
        self.pho_execute(['-v', 'dir', 'add', file.name])
        self.pho_execute(['-v', 'dir', 'add', file.name], code=errno.EEXIST)

    def test_dir_add_correct_and_missing(self): # pylint: disable=invalid-name
        """
        Adding existing and a non-existent directories should add the correct
        directories and raise an error because of the missing one.
        """
        file1 = tempfile.TemporaryDirectory()
        file2 = tempfile.TemporaryDirectory()

        self.pho_execute(['-v', 'dir', 'add', file1.name, '/tmp/notfileAA',
                          file2.name],
                         code=errno.ENOENT)
        output, _ = self.pho_execute_capture(['dir', 'list'])
        self.assertIn(file1.name, output)
        self.assertNotIn('/tmp/notfileAA', output)
        self.assertIn(file2.name, output)

class SyslogTest(BasicExecutionTest):
    """Syslog related tests"""

    def test_cli_syslog(self):
        """Partially test syslog logging feature"""
        # Messages in /var/log/messages or journalctl can't be retrieved as
        # an unprivileged user
        self.pho_execute(['--syslog', 'debug', 'dir', 'list'])
        self.pho_execute(['--syslog', 'info', '-vvv', 'dir', 'list'])

if __name__ == '__main__':
    unittest.main(buffer=True)
