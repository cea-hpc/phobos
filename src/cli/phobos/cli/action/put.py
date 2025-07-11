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
Put action for Phobos CLI
"""

from phobos.cli.action import ActionOptHandler
from phobos.cli.common.args import add_put_arguments

class PutOptHandler(ActionOptHandler):
    """Insert objects into backend."""
    label = 'put'
    descr = 'insert object into backend. Either "--file" or "src_file" and '\
            '"oid" must be provided.'

    @classmethod
    def add_options(cls, parser):
        """Add options for the PUT command."""
        super(PutOptHandler, cls).add_options(parser)
        # The type argument allows to transform 'a,b,c' into ['a', 'b', 'c'] at
        # parse time rather than post processing it
        add_put_arguments(parser)
        parser.add_argument('--grouping',
                            help='Set the grouping of the new objects')
        parser.add_argument('-c', '--copy-name',
                            help='Copy name for this object instance')

        parser.add_argument('-m', '--metadata',
                            help='Comma-separated list of key=value')
        parser.add_argument('--overwrite', action='store_true',
                            help='Allow object update')
        parser.add_argument('--file',
                            help='File containing lines like: '\
                                 '<src_file>  <object_id>  <metadata|->')
        parser.add_argument('--no-split', action='store_true',
                            help='Prevent splitting object over multiple '
                            'media.')
        parser.add_argument('src_file', help='File to insert', nargs='?')
        parser.add_argument('object_id', help='Desired object ID', nargs='?')
        parser.set_defaults(verb=cls.label)
