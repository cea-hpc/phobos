#!/usr/bin/python

#
#  All rights reserved (c) 2014-2019 CEA/DAM.
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

from phobos.core.const import PHO_XFER_OBJ_GETATTR
from phobos.core.store import XferDescriptor

class FileDescTest(unittest.TestCase):
    @contextmanager
    def _open_context(self):
        f = NamedTemporaryFile()
        yield f

    @contextmanager
    def _getsize_context(self):
        sizes = [0, 1, 16, 32]
        file_sizes = [(NamedTemporaryFile(), size) for size in sizes]
        for f, size in file_sizes:
            f.write('a' * size)
            f.flush()
        yield [(f.name, size) for f, size in file_sizes]

    def _getsize_good(self, path, size):
        xfr = XferDescriptor()
        xfr.open_file(path)
        self.assertEqual(xfr.xd_size, size)
        os.close(xfr.xd_fd)

    def test_open_good(self):
        with self._open_context() as f:
            xfr = XferDescriptor()
            xfr.open_file(f.name)
            self.assertTrue(xfr.xd_fd >= 0)
            os.close(xfr.xd_fd)

    def test_open_notexist(self):
        xfr = XferDescriptor()
        with self.assertRaises(OSError):
            xfr.open_file("foo")

    def test_open_empty(self):
        xfr = XferDescriptor()
        with self.assertRaises(ValueError):
            xfr.open_file("")
        with self.assertRaises(ValueError):
            xfr.open_file(None)

    def test_open_getmd(self):
        xfr = XferDescriptor()
        xfr.xd_flags = PHO_XFER_OBJ_GETATTR
        xfr.open_file("")
        self.assertEqual(xfr.xd_fd, -1)
        xfr.open_file(None)
        self.assertEqual(xfr.xd_fd, -1)

    def test_getsize(self):
        with self._getsize_context() as path_sizes:
            for path, size in path_sizes:
                self._getsize_good(path, size)

if __name__ == '__main__':
    unittest.main()
