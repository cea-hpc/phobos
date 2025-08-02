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
Tape target for Phobos CLI
"""

from phobos.cli.action import ActionOptHandler
from phobos.cli.action.format import FormatOptHandler
from phobos.cli.action.lock import LockOptHandler
from phobos.cli.action.resource_delete import ResourceDeleteOptHandler
from phobos.cli.action.unlock import UnlockOptHandler
from phobos.cli.target.media import (MediaAddOptHandler,
                                     MediaImportOptHandler,
                                     MediaListOptHandler,
                                     MediaLocateOptHandler,
                                     MediaOptHandler,
                                     MediaRenameOptHandler,
                                     MediaSetAccessOptHandler,
                                     MediaUpdateOptHandler)
from phobos.cli.common.utils import (setaccess_epilog, uncase_fstype)
from phobos.core.const import fs_type2str # pylint: disable=no-name-in-module
from phobos.core.ffi import (FSType, ResourceFamily)


class TapeRepackOptHandler(ActionOptHandler):
    """Repack a tape."""
    label = 'repack'
    descr = 'Repack a tape, by copying its alive extents to another one'

    @classmethod
    def add_options(cls, parser):
        super(TapeRepackOptHandler, cls).add_options(parser)
        parser.add_argument('-T', '--tags', type=lambda t: t.split(','),
                            help='Only use a tape that contain this set of '
                                 'tags (comma-separated: foo,bar).'
                                 'If not specified, Phobos will use a tape '
                                 'with the same tags as the repack tape. To '
                                 'let Phobos choose any tape, use --tags "".')
        parser.add_argument('res', help='Tape to repack')
        parser.add_argument('--library',
                            help="Library containing the tape to repack")


class TapeAddOptHandler(MediaAddOptHandler):
    """Specific version of the 'add' command for tapes, with extra-options."""
    descr = "insert new tape to the system"

    @classmethod
    def add_options(cls, parser):
        """Add resource-specific options."""
        super(TapeAddOptHandler, cls).add_options(parser)
        parser.add_argument('-t', '--type', required=True,
                            help='tape technology')
        parser.add_argument('--fs', default="LTFS",
                            choices=list(map(fs_type2str, FSType)),
                            type=uncase_fstype(list(map(fs_type2str, FSType))),
                            help='Filesystem type (default: LTFS)')


class TapeFormatOptHandler(FormatOptHandler):
    """Format a tape."""
    descr = "format a tape"

    @classmethod
    def add_options(cls, parser):
        """Add resource-specific options."""
        super(TapeFormatOptHandler, cls).add_options(parser)
        parser.add_argument('--fs', default='ltfs',
                            choices=list(map(fs_type2str, FSType)),
                            type=uncase_fstype(list(map(fs_type2str, FSType))),
                            help='Filesystem type')
        parser.add_argument('--force', action='store_true',
                            help='Format the medium whatever its status')


class TapeImportOptHandler(MediaImportOptHandler):
    """Specific version of the 'import' command for tapes"""
    descr = "import existing tape"

    @classmethod
    def add_options(cls, parser):
        super(TapeImportOptHandler, cls).add_options(parser)
        parser.add_argument('-t', '--type', required=True,
                            help='tape technology')
        parser.add_argument('--fs', default="LTFS",
                            choices=list(map(fs_type2str, FSType)),
                            type=uncase_fstype(list(map(fs_type2str, FSType))),
                            help='Filesystem type (default: LTFS)')


class TapeSetAccessOptHandler(MediaSetAccessOptHandler):
    """Set media operation flags to tape media."""
    epilog = setaccess_epilog("tape")


class TapeOptHandler(MediaOptHandler):
    """Magnetic tape options and actions."""
    label = 'tape'
    descr = 'handle magnetic tape (use tape label to identify resource)'
    family = ResourceFamily(ResourceFamily.RSC_TAPE)
    verbs = [
        LockOptHandler,
        MediaListOptHandler,
        MediaLocateOptHandler,
        MediaRenameOptHandler,
        MediaUpdateOptHandler,
        ResourceDeleteOptHandler,
        TapeAddOptHandler,
        TapeFormatOptHandler,
        TapeImportOptHandler,
        TapeRepackOptHandler,
        TapeSetAccessOptHandler,
        UnlockOptHandler,
    ]
