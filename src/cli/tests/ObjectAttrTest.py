#!/usr/bin/env python3

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

"""Unit tests for object attributes handling"""

import unittest

from phobos.cli.common.utils import attr_convert

class AttrConvertTest(unittest.TestCase):
    """
    Convert valid and broken strings of k=v attributes to stress the lexer.
    """
    def _conv_check(self, kvstr, exp_result):
        """Parse the given string and compare to the expected result."""
        res = attr_convert(kvstr)
        diff = set(res.items()) ^ set(exp_result.items())
        if diff:
            print(res)
            print(exp_result)
        self.assertEqual(len(diff), 0)

    def _conv_xfail(self, kvstr):
        """Parse the given string and make sure it raises a parse error."""
        self.assertRaises(ValueError, attr_convert, kvstr)

    def test_valid(self):
        """test valid strings that must work."""
        self._conv_check('a=1,b=2,c=3', {'a':'1', 'b':'2', 'c':'3'})
        self._conv_check('a="1,",b=2,c=3', {'a':'1,', 'b':'2', 'c':'3'})
        self._conv_check('a="1,b=2",c=3', {'a':'1,b=2', 'c':'3'})
        self._conv_check('a="",b=2,c=3', {'a':'', 'b':'2', 'c':'3'})
        self._conv_check('a="=",b="",c=3', {'a':'=', 'b':'', 'c':'3'})
        self._conv_check('', {})

    def test_invalid(self):
        """test invalid format and make sure they are detected as such."""
        self._conv_xfail('a"=')
        self._conv_xfail('abc')


if __name__ == '__main__':
    unittest.main(buffer=True)
