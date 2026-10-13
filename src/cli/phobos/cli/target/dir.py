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

import json
import sys

# pylint: disable=duplicate-code
from phobos.cli.action.format import FormatOptHandler
from phobos.cli.action.lock import LockOptHandler
from phobos.cli.action.resource_delete import ResourceDeleteOptHandler
from phobos.cli.action.status import StatusOptHandler
from phobos.cli.action.unlock import UnlockOptHandler
from phobos.cli.common import env_error_format
from phobos.cli.common.exec import (exec_add_dir_rados, exec_delete_dir_rados,
                                    exec_lock_dir_rados, exec_unlock_dir_rados)
from phobos.cli.common.utils import (setaccess_epilog, uncase_fstype)
# pylint: enable=duplicate-code
from phobos.cli.target.media import (MediaAddOptHandler, MediaListOptHandler,
                                     MediaLocateOptHandler, MediaOptHandler,
                                     MediaRebuildOptHandler,
                                     MediaRenameOptHandler,
                                     MediaImportOptHandler,
                                     MediaSetAccessOptHandler,
                                     MediaUpdateOptHandler)
from phobos.core.admin import Client as AdminClient
from phobos.core.const import fs_type2str, PHO_RSC_DIR # pylint: disable=no-name-in-module
from phobos.core.ffi import (DeviceStatus, FSType, ResourceFamily)
from phobos.output import dump_object_list

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

class DirResourceDeleteOptHandler(ResourceDeleteOptHandler):
    """Specific version of the 'delete' command for directories"""
    descr = 'remove dir(s) from the system'

    @classmethod
    def add_options(cls, parser):
        super(DirResourceDeleteOptHandler, cls).add_options(parser)
        parser.add_argument('-f', '--force', action='store_true',
                            help="Try to delete the device even if the path "
                                 "normalization failed (ie: dir path not "
                                 "available on localhost")

class DirOptHandler(MediaOptHandler):
    """Directory-related options and actions."""
    label = 'dir'
    descr = 'handle directories'
    family = ResourceFamily(ResourceFamily.RSC_DIR)
    verbs = [
        DirFormatOptHandler,
        DirSetAccessOptHandler,
        DirImportOptHandler,
        LockOptHandler, # pylint: disable=duplicate-code
        MediaAddOptHandler,
        MediaListOptHandler,
        MediaLocateOptHandler,
        MediaRebuildOptHandler,
        MediaRenameOptHandler,
        MediaUpdateOptHandler, # pylint: enable=duplicate-code
        DirResourceDeleteOptHandler,
        StatusOptHandler,
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

    def del_medium(self, adm, family, resources, library, lost, force):
        #pylint: disable=too-many-arguments
        adm.medium_delete(family, resources, library, lost, force)

    def exec_status(self):
        """Display I/O and dir status"""
        try:
            with AdminClient(lrs_required=True) as adm:
                status = json.loads(adm.device_status(PHO_RSC_DIR))
                # disable pylint's warning as it's suggestion does not work
                for i in range(len(status)): #pylint: disable=consider-using-enumerate
                    status[i] = DeviceStatus(status[i])

                dump_object_list(sorted(status, key=lambda x: x.address),
                                 self.params.get('output'))

        except EnvironmentError as err:
            self.logger.error("Cannot read status of dir: %s",
                              env_error_format(err))
            sys.exit(abs(err.errno))

    def exec_delete(self):
        """
        Delete a directory
        Note that this is a special case where we delete both a media (storage)
        and a device (mean to access it).
        """
        exec_delete_dir_rados(self, PHO_RSC_DIR)

    def exec_lock(self):
        """
        Lock a directory.
        Note that this is a special case where we lock both a media (storage)
        and a device (mean to access it).
        """
        exec_lock_dir_rados(self, PHO_RSC_DIR)

    def exec_unlock(self):
        """
        Unlock a directory.
        Note that this is a special case where we unlock both a media (storage)
        and a device (mean to access it).
        """
        exec_unlock_dir_rados(self, PHO_RSC_DIR)
