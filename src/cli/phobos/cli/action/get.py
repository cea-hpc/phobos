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

"""
Get action for Phobos CLI
"""

from phobos.cli.action import ActionOptHandler

class GetOptHandler(ActionOptHandler):
    """Retrieve object from backend."""
    label = 'get'
    descr = 'retrieve object from backend'

    @classmethod
    def add_options(cls, parser):
        """Add options for the GET command."""
        super(GetOptHandler, cls).add_options(parser)
        parser.add_argument('object_id', help='Object to retrieve')
        parser.add_argument('dest_file', help='Destination file')
        parser.add_argument('--version', help='Version of the object',
                            type=int, default=0)
        parser.add_argument('--uuid', help='UUID of the object')
        parser.add_argument('--best-host', action='store_true',
                            help="Only get object if the current host is the "
                                 "most optimal one or if the object can be "
                                 "accessed from any node, else return the best "
                                 "hostname to get this object")
        parser.add_argument('-c', '--copy-name',
                            help='Copy of the object to get')
        parser.set_defaults(verb=cls.label)
