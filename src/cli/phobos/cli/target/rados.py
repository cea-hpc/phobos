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
RadosPool target for Phobos CLI
"""

import argparse
import sys

from phobos.cli.action.format import FormatOptHandler
from phobos.cli.action.lock import LockOptHandler
from phobos.cli.action.resource_delete import ResourceDeleteOptHandler
from phobos.cli.action.unlock import UnlockOptHandler
from phobos.cli.common.utils import (setaccess_epilog, uncase_fstype)
from phobos.cli.target.media import (MediaAddOptHandler,
                                     MediaListOptHandler,
                                     MediaLocateOptHandler,
                                     MediaOptHandler,
                                     MediaRenameOptHandler,
                                     MediaSetAccessOptHandler,
                                     MediaUpdateOptHandler)
from phobos.core.const import fs_type2str # pylint: disable=no-name-in-module
from phobos.core.ffi import (FSType, ResourceFamily)


class RadosPoolFormatOptHandler(FormatOptHandler):
    """Format a RADOS pool."""
    descr = "format a RADOS pool"

    @classmethod
    def add_options(cls, parser):
        """Add resource-specific options."""
        super(RadosPoolFormatOptHandler, cls).add_options(parser)
        # invisible fs argument in help because it is not useful
        parser.add_argument('--fs', default='RADOS',
                            choices=list(map(fs_type2str, FSType)),
                            type=uncase_fstype(list(map(fs_type2str, FSType))),
                            help=argparse.SUPPRESS)


class RadosPoolSetAccessOptHandler(MediaSetAccessOptHandler):
    """Set media operation flags to rados_pool media."""
    epilog = setaccess_epilog("rados_pool")


class RadosPoolOptHandler(MediaOptHandler):
    """RADOS pool options and actions."""
    label = 'rados_pool'
    descr = 'handle RADOS pools'
    family = ResourceFamily(ResourceFamily.RSC_RADOS_POOL)
    verbs = [
        LockOptHandler,
        MediaAddOptHandler,
        MediaListOptHandler,
        MediaLocateOptHandler,
        MediaUpdateOptHandler,
        MediaRenameOptHandler,
        RadosPoolSetAccessOptHandler,
        RadosPoolFormatOptHandler,
        ResourceDeleteOptHandler,
        UnlockOptHandler,
    ]

    def add_medium(self, adm, medium, tags):
        adm.medium_add(medium, 'RADOS', tags=tags)

    def exec_add(self):
        """
        Add a new RADOS pool.
        Note that this is a special case where we add both a media (storage) and
        a device (mean to access it).
        """
        (rc, nb_dev_to_add, nb_dev_added) = self.add_medium_and_device()
        if nb_dev_added == nb_dev_to_add:
            self.logger.info("Added %d RADOS pool(s) successfully",
                             nb_dev_added)
        else:
            self.logger.error("Failed to add %d/%d RADOS pools",
                              nb_dev_to_add - nb_dev_added, nb_dev_to_add)
            sys.exit(abs(rc))

    def del_medium(self, adm, family, resources, library, lost):
        #pylint: disable=too-many-arguments
        adm.medium_delete(family, resources, library, lost)

    def exec_delete(self):
        """
        Delete a RADOS pool.
        Note that this is a special case where we delete both a media (storage)
        and a device (mean to access it).
        """
        rc, nb_dev_to_del, nb_dev_del = self.delete_medium_and_device()

        if nb_dev_del == nb_dev_to_del:
            self.logger.info("Deleted %d RADOS pool(s) successfully",
                             nb_dev_del)
        else:
            self.logger.error("Failed to delete %d/%d RADOS pools",
                              nb_dev_to_del - nb_dev_del, nb_dev_to_del)
            sys.exit(abs(rc))
