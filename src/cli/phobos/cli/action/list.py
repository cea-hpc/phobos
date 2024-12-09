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
List action for Phobos CLI
"""

from phobos.cli.action import ActionOptHandler

class ListOptHandler(ActionOptHandler):
    """Option handler for list action"""
    label = 'list'
    descr = 'list resources'

    @classmethod
    def add_options(cls, parser):
        super(ListOptHandler, cls).add_options(parser)
        parser.add_argument('res', nargs='*', help='resource to list')
        parser.add_argument('-f', '--format', default='human',
                            help="output format human/xml/json/csv/yaml " \
                                 "(default: human)")
