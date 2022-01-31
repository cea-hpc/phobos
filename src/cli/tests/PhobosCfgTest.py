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

"""Unit tests for phobos.core.cfg"""

import os
import unittest

from phobos.core.cfg import get_val


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

if __name__ == '__main__':
    unittest.main(buffer=True)
