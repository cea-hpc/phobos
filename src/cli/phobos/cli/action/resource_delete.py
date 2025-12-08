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
Resource Delete action for Phobos CLI
"""

from phobos.cli.action import ActionOptHandler

class ResourceDeleteOptHandler(ActionOptHandler):
    """Remove a resource from the system."""
    label = 'delete'
    alias = ['del']
    descr = 'remove resource(s) from the system'
    epilog = "Resources are only removed from the database, and so will not "\
             "be available through Phobos anymore. No other operations are "\
             "executed. For media, the resource cannot be removed if it "\
             "contains extents, unless the '--lost' flag is used."

    @classmethod
    def add_options(cls, parser):
        super(ResourceDeleteOptHandler, cls).add_options(parser)
        parser.add_argument('--library',
                            help="Library containing deleted resources")
        parser.add_argument('-l', '--lost', action='store_true',
                            help="remove all extents associated with the "
                                 "medium from the database and update the "
                                 "objects and copies accordingly")
        parser.add_argument('res', nargs='+', help='Resource(s) to remove')
        parser.set_defaults(verb=cls.label)
