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
Format action for Phobos CLI
"""

from phobos.cli.action import ActionOptHandler

class FormatOptHandler(ActionOptHandler):
    """Format a resource."""
    label = 'format'
    descr = 'format a media'

    @classmethod
    def add_options(cls, parser):
        """Add resource-specific options."""
        super(FormatOptHandler, cls).add_options(parser)
        parser.add_argument('-n', '--nb-streams', metavar='STREAMS', type=int,
                            default=0,
                            help='Max number of parallel formatting operations,'
                                 ' 0 means no limitation (default is 0)')
        parser.add_argument('--unlock', action='store_true',
                            help='Unlock media once it is ready to be written')
        parser.add_argument('--library',
                            help="Library containing added resources")
        parser.add_argument('res', nargs='+', help='Resource(s) to format')
