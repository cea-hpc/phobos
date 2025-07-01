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
Clear action for Phobos CLI
"""

from phobos.cli.action import ActionOptHandler
from phobos.cli.common.args import add_log_arguments

class ClearOptHandler(ActionOptHandler):
    """Handler for persistent logs clearing"""
    label = "clear"
    descr = "handler for persistent logs clearing"
    epilog = """Will clear persistent logs recorded by Phobos, according to
    given filters."""

    @classmethod
    def add_options(cls, parser):
        super(ClearOptHandler, cls).add_options(parser)
        add_log_arguments(parser, "clear")
        parser.add_argument('--clear-all', action='store_true',
                            help='must be specified to clear all logs, will '
                                 'have no effect, if any of the other '
                                 'arguments is specified')
