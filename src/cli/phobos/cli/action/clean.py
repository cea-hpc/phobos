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
Clean action for Phobos CLI
"""

from phobos.cli.action import ActionOptHandler

class CleanOptHandler(ActionOptHandler):
    """Clean locks"""
    label = 'clean'
    descr = 'clean locks options'

    @classmethod
    def add_options(cls, parser):
        """Add resource-specific clean options."""
        super(CleanOptHandler, cls).add_options(parser)
        parser.add_argument('--global', action='store_true',
                            help='target all locks (can only be used '
                                 'with --force). If not set, '
                                 'only target localhost locks')
        parser.add_argument('--force', action='store_true',
                            help='clean locks even if phobosd is on')
        parser.add_argument('-t', '--type',
                            help='lock type to clean, between [device, media, '
                                 'object, media_update]',
                            choices=["device", "media",
                                     "object", "media_update"])
        parser.add_argument('-f', '--family',
                            help='Family of locked ressources to clean, '
                                 'between [dir, tape]; object type '
                                 'is not supported with this option',
                            choices=["dir", "tape"])
        parser.add_argument('-i', '--ids', nargs='+', help='lock id(s)')
