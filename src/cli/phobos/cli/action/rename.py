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
Rename action for Phobos CLI
"""

from phobos.cli.action import ActionOptHandler

class RenameOptHandler(ActionOptHandler):
    """Rename object handler"""
    label = 'rename'
    descr = 'Change the oid of an object generation, '\
            'among living or deprecated objects tables.'

    @classmethod
    def add_options(cls, parser):
        """Add command options for object rename."""
        super(RenameOptHandler, cls).add_options(parser)
        parser.set_defaults(verb=cls.label)
        parser.add_argument('oid', help='Object ID to be renamed')
        parser.add_argument('new_oid', help='New name for the object')
        parser.add_argument('--uuid', help='UUID of the object to rename')
