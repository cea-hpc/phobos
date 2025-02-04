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
Phobos CLI arguments utilities
"""

from phobos.core.const import rsc_family2str # pylint: disable=no-name-in-module
from phobos.core.ffi import ResourceFamily

def add_put_arguments(parser):
    """Default arguments for put-like actions"""
    parser.add_argument('-T', '--tags', type=lambda t: t.split(','),
                        help='only use media that contain all these tags '
                             '(comma-separated: foo,bar)')
    parser.add_argument('-f', '--family',
                        choices=list(map(rsc_family2str, ResourceFamily)),
                        help='targeted storage family')
    parser.add_argument('--library',
                        help='desired library (if not set, any available '
                             'library will be used)')
    parser.add_argument('-l', '--layout', '--lyt',
                        choices=['raid1', 'raid4'],
                        help='desired storage layout')
    parser.add_argument('-p', '--profile',
                        help='desired profile for family, tags and layout. '
                             'Specifically set family and layout supersede '
                             'the profile, tags are joined')
    parser.add_argument('-P', '--layout-params', '--lyt-params',
                        help='comma-separated list of key=value for layout '
                             'specific parameters')
