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
Dir target for Phobos CLI
"""

from phobos.cli.action.format import FormatOptHandler
from phobos.cli.action.lock import LockOptHandler
from phobos.cli.action.resource_delete import ResourceDeleteOptHandler
from phobos.cli.action.unlock import UnlockOptHandler
from phobos.cli.common.exec import exec_add_dir_rados, exec_delete_dir_rados
from phobos.cli.common.utils import (setaccess_epilog, uncase_fstype)
from phobos.cli.target.media import (MediaAddOptHandler, MediaListOptHandler,
                                     MediaLocateOptHandler, MediaOptHandler,
                                     MediaRenameOptHandler,
                                     MediaImportOptHandler,
                                     MediaSetAccessOptHandler,
                                     MediaUpdateOptHandler)
from phobos.core.const import fs_type2str, PHO_RSC_DIR # pylint: disable=no-name-in-module
from phobos.core.ffi import (FSType, ResourceFamily)

class DirFormatOptHandler(FormatOptHandler):
    """Format a directory."""
    descr = "format a directory"

    @classmethod
    def add_options(cls, parser):
        """Add resource-specific options."""
        super(DirFormatOptHandler, cls).add_options(parser)
        parser.add_argument('--fs', default='POSIX',
                            choices=list(map(fs_type2str, FSType)),
                            type=uncase_fstype(list(map(fs_type2str, FSType))),
                            help='Filesystem type')


class DirSetAccessOptHandler(MediaSetAccessOptHandler):
    """Set media operation flags to directory media."""
    epilog = setaccess_epilog("dir")


class DirImportOptHandler(MediaImportOptHandler):
    """Specific version of the 'import' command for directories"""
    descr = "import existing dir"

    @classmethod
    def add_options(cls, parser):
        super(DirImportOptHandler, cls).add_options(parser)
        parser.add_argument('--fs', default="POSIX",
                            choices=list(map(fs_type2str, FSType)),
                            type=uncase_fstype(list(map(fs_type2str, FSType))),
                            help='Filesystem type (default: POSIX)')


class DirOptHandler(MediaOptHandler):
    """Directory-related options and actions."""
    label = 'dir'
    descr = 'handle directories'
    family = ResourceFamily(ResourceFamily.RSC_DIR)
    verbs = [
        DirFormatOptHandler,
        DirSetAccessOptHandler,
        DirImportOptHandler,
        LockOptHandler,
        MediaAddOptHandler,
        MediaListOptHandler,
        MediaLocateOptHandler,
        MediaRenameOptHandler,
        MediaUpdateOptHandler,
        ResourceDeleteOptHandler,
        UnlockOptHandler,
    ]

    def add_medium(self, adm, medium, tags):
        adm.medium_add(medium, 'POSIX', tags=tags)

    def exec_add(self):
        """
        Add a new directory.
        Note that this is a special case where we add both a media (storage) and
        a device (mean to access it).
        """
        exec_add_dir_rados(self, PHO_RSC_DIR)

    def del_medium(self, adm, family, resources, library, lost):
        #pylint: disable=too-many-arguments
        adm.medium_delete(family, resources, library, lost)

    def exec_delete(self):
        """
        Delete a directory
        Note that this is a special case where we delete both a media (storage)
        and a device (mean to access it).
        """
        exec_delete_dir_rados(self, PHO_RSC_DIR)
