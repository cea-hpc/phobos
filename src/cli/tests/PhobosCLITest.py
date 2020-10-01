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
from phobos.core.dss import Client, MediaManager
from phobos.core.ffi import DevInfo
from phobos.core.const import PHO_RSC_DIR, PHO_RSC_TAPE

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
        print('"' + ' '.join(args) + '"')
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
        self.check_cmdline_exit(['object', '-h'])
        self.check_cmdline_exit(['extent', '-h'])

    def test_cli_basic(self):
        """test simple valid and invalid command lines."""
        self.check_cmdline_valid(['dir', 'list'])
        self.check_cmdline_valid(['dir', 'add', '--unlock', 'toto'])
        self.check_cmdline_valid(['dir', 'add', 'A', 'B', 'C'])
        self.check_cmdline_valid(['dir', 'list', 'A,B,C', '-o', 'all'])
        self.check_cmdline_valid(['dir', 'list', 'A,B,C', '-o', '*'])
        self.check_cmdline_valid(['dir', 'list', 'A,B,C', '-o', 'name,family'])
        self.check_cmdline_valid(['tape', 'add', '-t', 'LTO5', 'I,J,K'])
        self.check_cmdline_valid(['tape', 'list', 'I,J,K', '-o', 'all'])
        self.check_cmdline_valid(['tape', 'list', 'I,J,K', '-o', '*'])
        self.check_cmdline_valid(['tape', 'list', 'I,J,K', '-o',
                                  'name,family'])
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

        # Test invalid object and invalid verb
        self.check_cmdline_exit(['voynichauthor', 'list'], code=2)
        self.check_cmdline_exit(['dir', 'teleport'], code=2)


class BasicExecutionTest(unittest.TestCase):
    """Base execution of the CLI."""
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
        client = Client()
        client.connect()
        tagged0, = client.media.get(id='TAGGED0')
        tagged1, = client.media.get(id='TAGGED1')
        self.assertItemsEqual(tagged0.tags, ['tag-foo'])
        self.assertItemsEqual(tagged1.tags, ['tag-foo', 'tag-bar'])

    def test_media_update(self):
        """test updating media."""
        client = Client()
        client.connect()
        self.pho_execute(['tape', 'add', '-t', 'LTO6', '--fs', 'LTFS',
                          'update0', '--tags', 'tag-foo'])
        self.pho_execute(['tape', 'add', '-t', 'LTO6', '--fs', 'LTFS',
                          'update1', '--tags', 'tag-foo,tag-bar'])

        # Check inserted media
        update0, = client.media.get(id="update0")
        update1, = client.media.get(id="update1")
        self.assertItemsEqual(update0.tags, ['tag-foo'])
        self.assertItemsEqual(update1.tags, ['tag-foo', 'tag-bar'])

        # Update media
        self.pho_execute(['tape', 'update', '-T', 'new-tag1,new-tag2',
                          'update[0-1]'])

        # Check updated media
        for med_id in "update0", "update1":
            media, = client.media.get(id=med_id)
            self.assertItemsEqual(media.tags, ['new-tag1', 'new-tag2'])

        # No '-T' argument does nothing
        self.pho_execute(['tape', 'update', 'update0'])
        update0, = client.media.get(id=med_id)
        self.assertItemsEqual(update0.tags, ['new-tag1', 'new-tag2'])

        # Test a failed update
        def failed_update(*args, **kwargs):
            """Emulates a failed update by raising EnvironmentError"""
            raise EnvironmentError(errno.ENOENT, "Expected failed")

        old_update = MediaManager.update
        MediaManager.update = failed_update
        try:
            self.pho_execute(['tape', 'update', '-T', '', 'update0'],
                             code=os.EX_DATAERR)
        finally:
            MediaManager.update = old_update

        # Ensure that the tape is unlocked after failure
        client = Client()
        client.connect()
        # Exactly one media should be returned
        media, = client.media.get(id='update0')
        self.assertFalse(media.is_locked())

        # Check that locked tapes cannot be updated
        client.media.lock([media])
        try:
            self.pho_execute(['tape', 'update', '-T', '', 'update0'],
                             code=os.EX_DATAERR)
        finally:
            client.media.unlock([media])

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


class MediumListTest(BasicExecutionTest):
    """
    This sub-test suite adds tapes and makes sure that phobos list them
    according to their tags.

    Note that we are in the CLI tests and therefore try to specifically
    exercise the upper layers (command line parsing etc.)
    """
    def add_media(self):
        self.pho_execute(['tape', 'add', 'm0', '-t', 'lto8'])
        self.pho_execute(['tape', 'add', 'm1', '-t', 'lto8', '-T', 'foo'])
        self.pho_execute(['tape', 'add', 'm2', '-t', 'lto8', '-T', 'foo,bar'])
        self.pho_execute(['tape', 'add', 'm3', '-t', 'lto8', '-T', 'goo,foo'])
        self.pho_execute(['tape', 'add', 'm4', '-t', 'lto8', '-T', 'bar,goo'])
        self.pho_execute(['tape', 'add', 'm5', '-t', 'lto8', \
                          '-T', 'foo,bar,goo'])

    def test_med_list_tags(self):
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
        """test adding directories. Simple case."""
        flist = []
        for i in range(5):
            file = tempfile.NamedTemporaryFile()
            self.pho_execute(['-v', 'dir', 'add', file.name])
            flist.append(file)

        for file in flist:
            path = "%s:%s" % (gethostname_short(), file.name)
            self.pho_execute(['-v', 'dir', 'list', '-o', 'all', path])

    def test_dir_tags(self):
        """Test adding a directory with tags."""
        tmp_f = tempfile.NamedTemporaryFile()
        tmp_path = tmp_f.name
        self.pho_execute(['dir', 'add', tmp_path, '--tags', 'tag-foo,tag-bar'])
        output, _ = self.pho_execute_capture(['dir', 'list', '-o', 'all',
                                             tmp_path])
        self.assertIn("['tag-foo', 'tag-bar']", output)

    def test_dir_update(self):
        """Test updating a directory."""
        tmp_f = tempfile.NamedTemporaryFile()
        tmp_path = tmp_f.name
        client = Client()
        client.connect()
        self.pho_execute(['dir', 'add', tmp_path, '--tags', 'tag-baz'])

        # Check inserted media
        media, = client.media.get(id=tmp_path)
        self.assertItemsEqual(media.tags, ['tag-baz'])

        # Update media
        self.pho_execute(['dir', 'update', '-T', '', tmp_path])

        # Check updated media
        media, = client.media.get(id=tmp_path)
        self.assertItemsEqual(media.tags, [])

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

    def test_dir_add_correct_and_missing(self):
        """
        Add existing and a non-existent directories should add the correct
        directories and raise an error because of the missing one.
        """
        file1 = tempfile.NamedTemporaryFile()
        file2 = tempfile.NamedTemporaryFile()

        self.pho_execute(['-v', 'dir', 'add', file1.name, '/tmp/notfileAA',
                          file2.name],
                         code=os.EX_DATAERR)
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
