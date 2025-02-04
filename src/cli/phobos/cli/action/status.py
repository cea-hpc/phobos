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
Status action for Phobos CLI
"""

from phobos.cli.action import ActionOptHandler
from phobos.core.ffi import DriveStatus

class StatusOptHandler(ActionOptHandler):
    """Display I/O and drive status"""
    label = 'status'
    descr = 'display I/O and drive status'

    @classmethod
    def add_options(cls, parser):
        """Add resource-specific options."""
        super(StatusOptHandler, cls).add_options(parser)
        attr = list(DriveStatus().get_display_fields().keys())
        attr.sort()
        parser.add_argument('-o', '--output', type=lambda t: t.split(','),
                            default='all',
                            help=("attributes to output, comma-separated, "
                                  "choose from {" + " ".join(attr) + "} "
                                  "(default: %(default)s)"))
