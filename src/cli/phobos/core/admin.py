#!/usr/bin/env python3

#
#  All rights reserved (c) 2014-2022 CEA/DAM.
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
Provide access to admin commands with the right level (tm) of abstraction.
"""

import errno
import json

from ctypes import (addressof, byref, c_int, c_char_p, c_void_p, cast, pointer,
                    POINTER, Structure, c_size_t)

from phobos.core.const import (PHO_FS_LTFS, PHO_FS_POSIX, # pylint: disable=no-name-in-module
                               PHO_FS_RADOS, PHO_RSC_DIR,
                               PHO_RSC_TAPE, PHO_RSC_RADOS_POOL,
                               PHO_RSC_NONE, DSS_NONE,
                               str2rsc_family, str2dss_type)
from phobos.core.glue import admin_device_status, jansson_dumps  # pylint: disable=no-name-in-module
from phobos.core.dss import DSSHandle
from phobos.core.ffi import (CommInfo, ExtentInfo, LayoutInfo, LIBPHOBOS_ADMIN,
                             Id)

def string_list2c_array(l, getter):
    c_string_list = (c_char_p * len(l))()
    c_string_list[:] = [getter(e).encode('utf-8') for e in l]
    return c_string_list


class AdminHandle(Structure): # pylint: disable=too-few-public-methods
    """Admin handler"""
    _fields_ = [
        ('comm', CommInfo),
        ('dss', DSSHandle),
        ('daemon_is_online', c_int),
    ]

class Client(object):
    """Wrapper on the phobos admin client"""
    def __init__(self, lrs_required=True):
        super(Client, self).__init__()
        self.lrs_required = lrs_required
        self.handle = None

    def __enter__(self):
        self.init(self.lrs_required)
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        self.fini()

    def init(self, lrs_required):
        """Admin client initialization."""
        if self.handle is not None:
            self.fini()

        self.handle = AdminHandle()

        rc = LIBPHOBOS_ADMIN.phobos_admin_init(byref(self.handle), lrs_required)
        if rc:
            raise EnvironmentError(rc, 'Admin initialization failed')

    def fini(self):
        """Admin client finalization."""
        if self.handle is not None:
            LIBPHOBOS_ADMIN.phobos_admin_fini(byref(self.handle))
            self.handle = None

    def fs_format(self, media_list, nb_streams, fs_type, unlock=False, # pylint: disable=too-many-arguments
                  force=False):
        """Format media through the LRS layer."""
        fs_type = fs_type.lower()
        if fs_type == 'ltfs':
            rsc_family = PHO_RSC_TAPE
            fs_type_enum = PHO_FS_LTFS
        elif fs_type == 'posix':
            rsc_family = PHO_RSC_DIR
            fs_type_enum = PHO_FS_POSIX
        elif fs_type == 'rados':
            rsc_family = PHO_RSC_RADOS_POOL
            fs_type_enum = PHO_FS_RADOS
        else:
            raise EnvironmentError(errno.EOPNOTSUPP,
                                   "Unknown filesystem type '%s'" % fs_type)

        c_id = Id * len(media_list)
        mstruct = [Id(rsc_family, name=medium_id) for medium_id in media_list]
        rc = LIBPHOBOS_ADMIN.phobos_admin_format(byref(self.handle),
                                                 c_id(*mstruct),
                                                 len(media_list),
                                                 nb_streams,
                                                 fs_type_enum,
                                                 unlock,
                                                 force)
        if rc:
            raise EnvironmentError(rc,
                                   "Failed to format every medium in '%s'" %
                                   str(media_list))

    def device_add(self, dev_family, dev_names, keep_locked):
        """Add devices to the LRS."""
        c_id = Id * len(dev_names)
        dev_ids = [Id(dev_family, name=name) for name in dev_names]

        rc = LIBPHOBOS_ADMIN.phobos_admin_device_add(byref(self.handle),
                                                     c_id(*dev_ids),
                                                     len(dev_ids), keep_locked)
        if rc:
            raise EnvironmentError(rc, "Failed to add device(s) '%s'" %
                                   dev_names)

    def device_migrate(self, dev_names, host):
        """Migrate devices (for now, only tape drives)."""
        c_id = Id * len(dev_names)
        dev_ids = [Id(PHO_RSC_TAPE, name=name) for name in dev_names]
        c_host = c_char_p(host.encode('utf-8'))
        count = c_int(0)

        rc = LIBPHOBOS_ADMIN.phobos_admin_drive_migrate(byref(self.handle),
                                                        c_id(*dev_ids),
                                                        len(dev_ids), c_host,
                                                        byref(count))
        if rc:
            raise EnvironmentError(rc, "Failed to migrate device(s) '%s'" %
                                   dev_names)

        return count.value

    def device_delete(self, dev_family, dev_names):
        """Remove devices to phobos system."""
        c_id = Id * len(dev_names)
        dev_ids = [Id(dev_family, name=name) for name in dev_names]
        count = c_int(0)

        rc = LIBPHOBOS_ADMIN.phobos_admin_device_delete(byref(self.handle),
                                                        c_id(*dev_ids),
                                                        len(dev_ids),
                                                        byref(count))

        if rc:
            raise EnvironmentError(rc, "Failed to delete device(s) '%s'" %
                                   dev_names)

        return count.value

    def sched_conf_get(self, config_items):
        values = (c_char_p * len(config_items))()

        rc = LIBPHOBOS_ADMIN.phobos_admin_sched_conf_get(
            byref(self.handle),
            string_list2c_array(config_items, lambda c: c.section),
            string_list2c_array(config_items, lambda c: c.key),
            byref(values),
            c_size_t(len(config_items)))
        if rc:
            raise EnvironmentError(rc,
                                   "Failed to query scheduler configuration")

        for i in range(len(config_items)):
            print(f"{config_items[i].key}: {values[i].decode('utf-8')}")


    def sched_conf_set(self, config_items):
        rc = LIBPHOBOS_ADMIN.phobos_admin_sched_conf_set(
            byref(self.handle),
            string_list2c_array(config_items, lambda c: c.section),
            string_list2c_array(config_items, lambda c: c.key),
            string_list2c_array(config_items, lambda c: c.value),
            c_size_t(len(config_items)))

        if rc:
            raise EnvironmentError(rc,
                                   "Failed to update scheduler configuration")

    def ping(self):
        """Ping the phobos daemon."""
        rc = LIBPHOBOS_ADMIN.phobos_admin_ping(byref(self.handle))

        if rc:
            raise EnvironmentError(rc, "Failed to ping phobosd")

    def device_lock(self, dev_family, dev_names, is_forced):
        """Wrapper for the device lock command."""
        c_id = Id * len(dev_names)
        dev_ids = [Id(dev_family, name=name) for name in dev_names]

        rc = LIBPHOBOS_ADMIN.phobos_admin_device_lock(byref(self.handle),
                                                      c_id(*dev_ids),
                                                      len(dev_ids), is_forced)
        if rc:
            raise EnvironmentError(rc, "Failed to lock device(s) '%s'" %
                                   dev_names)

    def device_unlock(self, dev_family, dev_names, is_forced):
        """Wrapper for the device unlock command."""
        c_id = Id * len(dev_names)
        dev_ids = [Id(dev_family, name=name) for name in dev_names]

        rc = LIBPHOBOS_ADMIN.phobos_admin_device_unlock(byref(self.handle),
                                                        c_id(*dev_ids),
                                                        len(dev_ids),
                                                        is_forced)
        if rc:
            raise EnvironmentError(rc, "Failed to unlock device(s) '%s'" %
                                   dev_names)

    def device_status(self, family):
        """Query the status of the local devices"""
        return admin_device_status(addressof(self.handle), family)

    def layout_list(self, res, is_pattern, medium, degroup): # pylint: disable=too-many-locals
        """List layouts."""
        n_layouts = c_int(0)
        layouts = pointer(LayoutInfo())

        enc_medium = medium.encode('utf-8') if medium else None

        enc_res = [elt.encode('utf-8') for elt in res]
        c_res_strlist = c_char_p * len(enc_res)

        rc = LIBPHOBOS_ADMIN.phobos_admin_layout_list(byref(self.handle),
                                                      c_res_strlist(*enc_res),
                                                      len(enc_res),
                                                      is_pattern,
                                                      enc_medium,
                                                      byref(layouts),
                                                      byref(n_layouts))
        if rc:
            raise EnvironmentError(rc, "Failed to list the extent(s) '%s'" %
                                   res)

        if not degroup:
            list_lyts = [layouts[i] for i in range(n_layouts.value)]
        else:
            list_lyts = []
            for i in range(n_layouts.value):
                extents = layouts[i].extents
                cnt = layouts[i].ext_count
                for j in range(cnt):
                    if medium is None or \
                        medium in extents[j].media.name:
                        lyt = type(layouts[i])()
                        pointer(lyt)[0] = layouts[i]
                        lyt.ext_count = 1
                        lyt.extents = pointer(extents[j])

                        list_lyts.append(lyt)

        return list_lyts, layouts, n_layouts

    def medium_locate(self, rsc_family, medium_id):
        """Locate a medium by calling phobos_admin_medium_locate API"""
        hostname = c_char_p(None)
        rc = LIBPHOBOS_ADMIN.phobos_admin_medium_locate(
            byref(self.handle),
            byref(Id(rsc_family, name=medium_id)),
            byref(hostname))
        if rc:
            raise EnvironmentError(rc, "Failed to locate medium '%s'" %
                                   medium_id)

        return hostname.value.decode('utf-8') if hostname.value else ""

    def clean_locks(self, global_mode, force, #pylint: disable=too-many-arguments
                    type_str, family_str, lock_ids):
        """Clean all locks from database based on given parameters."""
        lock_type = str2dss_type(type_str) if type_str else DSS_NONE
        enc_ids = [elt.encode('utf-8') for elt in lock_ids] if lock_ids else []
        c_ids_strlist = c_char_p * len(enc_ids)
        dev_family = (str2rsc_family(family_str) if family_str
                      else PHO_RSC_NONE)

        rc = LIBPHOBOS_ADMIN.phobos_admin_clean_locks(byref(self.handle),
                                                      global_mode,
                                                      force,
                                                      lock_type,
                                                      dev_family,
                                                      c_ids_strlist(*enc_ids),
                                                      len(enc_ids))

        if rc:
            raise EnvironmentError(rc, "Failed to clean lock(s)")

    @staticmethod
    def layout_list_free(layouts, n_layouts):
        """Free a previously obtained layout list."""
        LIBPHOBOS_ADMIN.phobos_admin_layout_list_free(layouts, n_layouts)

    @staticmethod
    def lib_scan(lib_type, lib_dev_path):
        """Scan and return a list of dictionnaries representing the properties
        of elements in a library of type lib_type.

        The only working implementation is for PHO_LIB_SCSI, which performs a
        SCSI scan of a given device.
        """
        jansson_t = c_void_p(None)
        LIBPHOBOS_ADMIN.phobos_admin_lib_scan(lib_type,
                                              lib_dev_path.encode('utf-8'),
                                              byref(jansson_t))
        return json.loads(jansson_dumps(jansson_t.value))
