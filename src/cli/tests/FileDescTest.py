#!/usr/bin/env python3

#
#  All rights reserved (c) 2014-2022 CEA/DAM.
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

"""Unit tests for Xfer file descriptors handling."""

import os
import unittest

from contextlib import contextmanager
from tempfile import NamedTemporaryFile

from phobos.core.const import PHO_XFER_OP_GETMD, PHO_XFER_OP_PUT # pylint: disable=no-name-in-module
from phobos.core.store import XferDescriptor, XferTarget

@contextmanager
def _open_context():
    """Initialize open_xxx() test context."""
    file_desc = NamedTemporaryFile()
    yield file_desc

@contextmanager
def _getsize_context():
    """Initialize getsize() test context."""
    sizes = [0, 1, 16, 32]
    file_sizes = [(NamedTemporaryFile('w'), size) for size in sizes]
    for file_desc, size in file_sizes:
        file_desc.write('a' * size)
        file_desc.flush()
    yield [(file_desc.name, size) for file_desc, size in file_sizes]

class FileDescTest(unittest.TestCase):
    """Unit test class."""
    def _getsize_good(self, path, size):
        """Check the xfer size parameter is correctly set."""
        xfr = XferDescriptor()
        xfr.xd_op = PHO_XFER_OP_PUT
        target = XferTarget * 1
        xfr.xd_targets = target()
        xfr.xd_targets[0].open_file(path, xfr)
        self.assertEqual(xfr.xd_targets[0].xt_size, size)
        os.close(xfr.xd_targets[0].xt_fd)

    def test_open_good(self):
        """Check opening an existing file is valid."""
        with _open_context() as file_desc:
            xfr = XferDescriptor()
            target = XferTarget * 1
            xfr.xd_targets = target()
            xfr.xd_targets[0].open_file(file_desc.name, xfr)
            self.assertTrue(xfr.xd_targets[0].xt_fd >= 0)
            os.close(xfr.xd_targets[0].xt_fd)

    def test_open_notexist(self):
        """Check opening a non-existing file is not valid."""
        xfr = XferDescriptor()
        target = XferTarget * 1
        xfr.xd_targets = target()
        with self.assertRaises(OSError):
            xfr.xd_targets[0].open_file("foo", xfr)

    def test_open_empty(self):
        """Check bad openings."""
        xfr = XferDescriptor()
        target = XferTarget * 1
        xfr.xd_targets = target()
        with self.assertRaises(ValueError):
            xfr.xd_targets[0].open_file("", xfr)
        with self.assertRaises(ValueError):
            xfr.xd_targets[0].open_file(None, xfr)

    def test_open_getmd(self):
        """Check bad openings with a GETMD has no impact."""
        xfr = XferDescriptor()
        target = XferTarget * 1
        xfr.xd_targets = target()
        xfr.xd_op = PHO_XFER_OP_GETMD
        xfr.xd_targets[0].open_file("", xfr)
        self.assertEqual(xfr.xd_targets[0].xt_fd, -1)
        xfr.xd_targets[0].open_file(None, xfr)
        self.assertEqual(xfr.xd_targets[0].xt_fd, -1)

    def test_getsize(self):
        """Check xfer size parameter setter."""
        with _getsize_context() as path_sizes:
            for path, size in path_sizes:
                self._getsize_good(path, size)

if __name__ == '__main__':
    unittest.main(buffer=True)
