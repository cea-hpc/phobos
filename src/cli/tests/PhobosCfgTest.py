#!/usr/bin/env python3

#
#  All rights reserved (c) 2014-2026 CEA/DAM.
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

"""Unit tests for phobos.core.cfg"""

import errno
import os
import tempfile
import unittest

from phobos.core.cfg import get_library_list, get_val


class CfgTest(unittest.TestCase):
    """Test cfg module"""

    def test_get_val(self):
        """Test the get_val() wrapper"""
        # Nominal case
        os.environ["PHOBOS_FOO_bar"] = "foobar"
        self.assertEqual(get_val("foo", "bar"), "foobar")

        # Unknwon key
        with self.assertRaises(KeyError):
            get_val("not", "an_existing_key")

        # Default values
        self.assertEqual(get_val("not", "an_existing_key", None), None)
        self.assertEqual(get_val("not", "an_existing_key", "foo"), "foo")


class LibraryListTest(unittest.TestCase):
    """Test library listing from the configuration file"""

    TEST_CFG = """[store]
default_tape_library = legacy

[tlc "legacy"]
hostname = localhost
lib_device = /dev/changer

[tlc "library_bis"]
hostname = localhost
lib_device = /dev/changer_bis

[profile "decoy"]
library = legacy
"""

    def setUp(self):
        """Write a test configuration file."""
        with tempfile.NamedTemporaryFile(mode='w', suffix='.conf',
                                         delete=False) as cfg_file:
            cfg_file.write(self.TEST_CFG)
            self.cfg_path = cfg_file.name

    def tearDown(self):
        """Remove the test configuration file."""
        os.unlink(self.cfg_path)

    def test_get_library_list(self):
        """List the names of the tlc sections, in alphabetical order."""
        self.assertEqual(get_library_list(self.cfg_path),
                         ['legacy', 'library_bis'])

    def test_get_library_list_default(self):
        """Without argument, use PHOBOS_CFG_FILE to locate the file."""
        old_path = os.environ.get('PHOBOS_CFG_FILE')
        os.environ['PHOBOS_CFG_FILE'] = self.cfg_path
        try:
            self.assertEqual(get_library_list(), ['legacy', 'library_bis'])
        finally:
            if old_path is None:
                del os.environ['PHOBOS_CFG_FILE']
            else:
                os.environ['PHOBOS_CFG_FILE'] = old_path

    def test_get_library_list_no_file(self):
        """A missing configuration file raises an error."""
        with self.assertRaises(IOError) as ctx:
            get_library_list('/no/such/phobos.conf')
        self.assertEqual(ctx.exception.errno, errno.ENOENT)

if __name__ == '__main__':
    unittest.main(buffer=True)
