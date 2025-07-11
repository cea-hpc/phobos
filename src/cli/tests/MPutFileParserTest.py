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

"""
Unit tests for mput file parser

This file is for testing the parser used for the "put --file" command line.
"""

import unittest

from phobos.cli.common.utils import mput_file_line_parser

class CliTestMultiPutParser(unittest.TestCase):
    """Try to parse a mput line entry to detect broken strings."""
    def _conv_check(self, line, exp_result):
        """Parse the given string and compare to the expected result."""
        res = mput_file_line_parser(line)
        self.assertListEqual(res, exp_result)

    def _conv_fail(self, line):
        """Parse the given string and make it raises a parse error"""
        self.assertRaises(ValueError, mput_file_line_parser, line)

    def test_valid(self):
        """Test valid strings for the put --file lines parser"""
        self._conv_check('a b c', ['a', 'b', 'c'])
        self._conv_check('a/b c -', ['a/b', 'c', '-'])
        self._conv_check(r'a\ b c\ d e', ['a b', 'c d', 'e'])
        self._conv_check('"a b c" c "d e"', ['a b c', 'c', 'd e'])
        self._conv_check("a/b/'c d' 'd a' ab", ['a/b/c d', 'd a', 'ab'])

    def test_invalid(self):
        """Test invalid format and make sure they are detected as such"""
        self._conv_fail('a/b/"c d" da a b')
        self._conv_fail('a/cd  ba a d')
        self._conv_fail('a b')

if __name__ == '__main__':
    unittest.main(buffer=True)
