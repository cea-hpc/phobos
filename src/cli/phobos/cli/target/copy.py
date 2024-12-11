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
Copy target for Phobos CLI
"""

from phobos.cli.action.create import CreateOptHandler
from phobos.cli.action.delete import DeleteOptHandler
from phobos.cli.action.list import ListOptHandler
from phobos.cli.common import BaseOptHandler
from phobos.cli.common.args import add_put_arguments

class CopyCreateOptHandler(CreateOptHandler):
    """Option handler for create action of copy target"""
    descr = 'create copy of object'

    @classmethod
    def add_options(cls, parser):
        super(CopyCreateOptHandler, cls).add_options(parser)
        parser.add_argument('oid', help='targeted object')
        parser.add_argument('copy', help='copy name')
        add_put_arguments(parser)


class CopyDeleteOptHandler(DeleteOptHandler):
    """Option handler for delete action of copy target"""
    descr = 'delete copy of object'

    @classmethod
    def add_options(cls, parser):
        super(CopyDeleteOptHandler, cls).add_options(parser)
        parser.add_argument('oid', help='targeted object')
        parser.add_argument('copy', help='copy name')


class CopyOptHandler(BaseOptHandler):
    """Option handler for copy target"""
    label = 'copy'
    descr = 'manage copies of objects'
    verbs = [
        CopyCreateOptHandler,
        CopyDeleteOptHandler,
        ListOptHandler,
    ]

    def exec_create(self):
        """Copy creation"""
        raise NotImplementedError(
            "This command will be implemented in a future version"
        )

    def exec_delete(self):
        """Copy deletion"""
        raise NotImplementedError(
            "This command will be implemented in a future version"
        )

    def exec_list(self):
        """Copy listing"""
        raise NotImplementedError(
            "This command will be implemented in a future version"
        )
