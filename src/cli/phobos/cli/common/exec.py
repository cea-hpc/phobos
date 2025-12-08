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
Phobos exec CLI utilities
"""

from os import EX_USAGE
import sys

from phobos.cli.common import env_error_format
from phobos.cli.common.utils import set_library
from phobos.core.admin import Client as AdminClient
from phobos.core.const import (DSS_DEVICE, DSS_MEDIA, # pylint: disable=no-name-in-module
                               PHO_RSC_DIR)

def exec_add_dir_rados(obj, family):
    """Add a new directory or rados pool."""
    rc, nb_dev_to_add, nb_dev_added = obj.add_medium_and_device()

    if nb_dev_added == nb_dev_to_add:
        obj.logger.info("Added %d %s(s) successfully", nb_dev_added,
                        "dir" if family == PHO_RSC_DIR else "rados")
    else:
        obj.logger.error("Failed to add %d/%d %s(s)",
                         nb_dev_to_add - nb_dev_added, nb_dev_to_add,
                         "dir" if family == PHO_RSC_DIR else "rados")
        sys.exit(abs(rc))

def exec_delete_dir_rados(obj, family):
    """Delete a directory or rados pool"""
    rc, nb_dev_to_del, nb_dev_del = obj.delete_medium_and_device()

    if nb_dev_del == nb_dev_to_del:
        obj.logger.info("Deleted %d %s(s) successfully", nb_dev_del,
                        "dir" if family == PHO_RSC_DIR else "rados")
    else:
        obj.logger.error("Failed to delete %d/%d %s(s)",
                         nb_dev_to_del - nb_dev_del, nb_dev_to_del,
                         "dir" if family == PHO_RSC_DIR else "rados")
        sys.exit(abs(rc))

def exec_delete_medium_device(obj, dss_type):
    """Remove media or devices."""
    resources = obj.params.get('res')
    lost = obj.params.get('lost')
    if lost and dss_type != DSS_MEDIA:
        obj.logger.error("Only a medium can be declared lost")
        sys.exit(EX_USAGE)

    set_library(obj)

    try:
        with AdminClient(lrs_required=False) as adm:
            if dss_type == DSS_MEDIA:
                num_deleted, num2delete = adm.medium_delete(obj.family,
                                                            resources,
                                                            obj.library,
                                                            lost)
            elif dss_type == DSS_DEVICE:
                num_deleted, num2delete = adm.device_delete(obj.family,
                                                            resources,
                                                            obj.library)
    except EnvironmentError as err:
        obj.logger.error(env_error_format(err))
        sys.exit(abs(err.errno))

    if num_deleted == num2delete:
        obj.logger.info("Deleted %d %s(s) successfully", num_deleted,
                        "media" if dss_type == DSS_MEDIA else "device")
    elif num_deleted == 0:
        obj.logger.error("No %s deleted: %d/%d",
                         "media" if dss_type == DSS_MEDIA else "device",
                         num_deleted, num2delete)
    else:
        obj.logger.warning("Failed to delete %d/%d %s(s)",
                           num2delete - num_deleted, num2delete,
                           "media" if dss_type == DSS_MEDIA else "device")
