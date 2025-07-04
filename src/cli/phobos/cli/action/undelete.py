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
Undelete action for Phobos CLI
"""

from phobos.cli.action import ActionOptHandler

class UndeleteOptHandler(ActionOptHandler):
    """Undelete objects handler."""
    label = 'undelete'
    descr = 'Move back deprecated objects into phobos namespace'

    @classmethod
    def add_options(cls, parser):
        """Add command options for undelete object."""
        super(UndeleteOptHandler, cls).add_options(parser)
        parser.set_defaults(verb=cls.label)
        parser.add_argument('oids', nargs='+', help='Object OIDs to undelete')
        parser.add_argument('--uuid', help='UUID of the object')
