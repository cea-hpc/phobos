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
Dump action for Phobos CLI
"""

from phobos.cli.action import ActionOptHandler
from phobos.cli.common.args import add_log_arguments

class DumpOptHandler(ActionOptHandler):
    """Handler for persistent logs dumping"""
    label = "dump"
    descr = "handler for persistent logs dumping"
    epilog = """Will dump persistent logs recorded by Phobos to stdout or a file
    if provided, according to given filters."""

    @classmethod
    def add_options(cls, parser):
        super(DumpOptHandler, cls).add_options(parser)
        add_log_arguments(parser, "dump")
