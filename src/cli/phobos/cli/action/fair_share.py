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
Fair Share action for Phobos CLI
"""

from phobos.cli.action import ActionOptHandler

class FairShareOptHandler(ActionOptHandler):
    """Handler for fair_share configuration parameters"""
    label = "fair_share"
    descr = "handler for fair_share configuration parameters"
    epilog = """If neither --min nor --max are set, this command will query the
    configuration information from the local daemon and output them to
    stdout."""

    @classmethod
    def add_options(cls, parser):
        super(FairShareOptHandler, cls).add_options(parser)
        parser.add_argument('-t', '--type', required=True,
                            help="tape technology")
        parser.add_argument('--min',
                            help="of the form r,w,f where r, w and f are "
                            "integers representing the minimum number of "
                            "devices that ought to be allocated to the read, "
                            "write and format schedulers respectively")
        parser.add_argument('--max',
                            help="of the form r,w,f where r, w and f are "
                            "integers representing the maximum number of "
                            "devices that ought to be allocated to the read, "
                            "write and format schedulers respectively")
