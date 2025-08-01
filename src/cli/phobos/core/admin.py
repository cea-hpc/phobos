#!/usr/bin/env python3

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
Provide access to admin commands with the right level (tm) of abstraction.
"""

import errno
import json

from ctypes import (addressof, byref, c_int, c_char_p, c_void_p, pointer,
                    Structure, c_size_t, c_bool)

from phobos.core.const import (PHO_FS_LTFS, PHO_FS_POSIX, # pylint: disable=no-name-in-module
                               PHO_FS_RADOS, PHO_RSC_TAPE,
                               PHO_RSC_NONE, DSS_NONE,
                               str2rsc_family, str2dss_type,
                               PHO_ADDR_HASH1, PHO_ADDR_PATH,
                               str2fs_type)
from phobos.core.glue import admin_device_status, jansson_dumps  # pylint: disable=no-name-in-module
from phobos.core.dss import DSSHandle, dss_sort
from phobos.core.ffi import (CommInfo, Id, LayoutInfo, LibDrvInfo, LIBPHOBOS,
                             LIBPHOBOS_ADMIN, MediaInfo, MediaStats,
                             OperationFlags, StringArray)


def string_list2c_array(str_list, getter):
    """Convert a Python list into a C list. A getter can be used to select
    elements from the list"""
    c_string_list = (c_char_p * len(str_list))()
    c_string_list[:] = [getter(e).encode('utf-8') for e in str_list]
    return c_string_list

class AdminHandle(Structure): # pylint: disable=too-few-public-methods
    """Admin handler"""
    _fields_ = [
        ('phobosd_comm', CommInfo),
        ('dss', DSSHandle),
        ('phobosd_is_online', c_int),
    ]

def init_medium(media, fstype, tags):
    """Initialize a medium with given fstype and tags"""
    media.fs.type = str2fs_type(fstype)
    media.addr_type = (PHO_ADDR_PATH if fstype == PHO_FS_RADOS
                       else PHO_ADDR_HASH1)
    media.stats = MediaStats()
    media.flags = OperationFlags()
    media.tags = tags if tags else []

class Client: # pylint: disable=too-many-public-methods
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
        phobos_context_func = LIBPHOBOS.phobos_context
        phobos_context_func.restype = c_void_p
        phobos_admin_init_func = LIBPHOBOS_ADMIN.phobos_admin_init
        phobos_admin_init_func.argtypes = (c_void_p, c_bool, c_void_p)
        rc = phobos_admin_init_func(byref(self.handle),
                                    lrs_required,
                                    phobos_context_func())
        if rc:
            raise EnvironmentError(rc, 'Admin initialization failed')

    def fini(self):
        """Admin client finalization."""
        if self.handle is not None:
            LIBPHOBOS_ADMIN.phobos_admin_fini(byref(self.handle))
            self.handle = None

    def fs_format(self, media_list, rsc_family, library, nb_streams, fs_type, # pylint: disable=too-many-arguments
                  unlock=False, force=False):
        """Format media through the LRS layer."""
        fs_type = fs_type.lower()
        if fs_type == 'ltfs':
            fs_type_enum = PHO_FS_LTFS
        elif fs_type == 'posix':
            fs_type_enum = PHO_FS_POSIX
        elif fs_type == 'rados':
            fs_type_enum = PHO_FS_RADOS
        else:
            raise EnvironmentError(errno.EOPNOTSUPP,
                                   "Unknown filesystem type '%s'" % fs_type)

        c_id = Id * len(media_list)
        mstruct = [Id(rsc_family, name=medium_id, library=library)
                   for medium_id in media_list]
        rc = LIBPHOBOS_ADMIN.phobos_admin_format(byref(self.handle),
                                                 c_id(*mstruct),
                                                 len(media_list),
                                                 nb_streams,
                                                 fs_type_enum,
                                                 unlock,
                                                 force)
        if rc:
            raise EnvironmentError(rc,
                                   "Failed to format some medium in '%s'" %
                                   str(media_list))

    def device_add(self, dev_family, dev_names, keep_locked, library):
        """Add devices to the LRS."""
        c_id = Id * len(dev_names)
        dev_ids = [Id(dev_family, name=name, library=library)
                   for name in dev_names]
        c_library = c_char_p(library.encode('utf-8'))

        rc = LIBPHOBOS_ADMIN.phobos_admin_device_add(byref(self.handle),
                                                     c_id(*dev_ids),
                                                     len(dev_ids), keep_locked,
                                                     c_library)
        if rc:
            raise EnvironmentError(rc, "Failed to add device(s) '%s'" %
                                   dev_names)

    def device_scsi_release(self, dev_names, library):
        """Release LTFS reservation of devices (for now, only tape drives)."""
        c_id = Id * len(dev_names)
        dev_ids = [Id(PHO_RSC_TAPE, name=name, library=library)
                   for name in dev_names]
        count = c_int(0)

        rc = LIBPHOBOS_ADMIN.phobos_admin_drive_scsi_release(byref(self.handle),
                                                             c_id(*dev_ids),
                                                             len(dev_ids),
                                                             byref(count))
        if rc:
            raise EnvironmentError(rc, "Failed to release SCSI reservation of"
                                   " device(s) '%s'" % dev_names)
        return count.value

    def device_migrate(self, dev_names, library, host, new_lib):
        """Migrate devices (for now, only tape drives)."""
        c_id = Id * len(dev_names)
        dev_ids = [Id(PHO_RSC_TAPE, name=name, library=library)
                   for name in dev_names]

        c_host = c_char_p(host.encode('utf-8')) if host else None
        c_new_lib = c_char_p(new_lib.encode('utf-8')) if new_lib else None
        count = c_int(0)

        rc = LIBPHOBOS_ADMIN.phobos_admin_drive_migrate(byref(self.handle),
                                                        c_id(*dev_ids),
                                                        len(dev_ids), c_host,
                                                        c_new_lib, byref(count))
        if rc:
            raise EnvironmentError(rc, "Failed to migrate device(s) '%s'" %
                                   dev_names)

        return count.value

    def device_delete(self, dev_family, dev_names, library):
        """Remove devices to phobos system."""
        c_id = Id * len(dev_names)
        dev_ids = [Id(dev_family, name=name, library=library)
                   for name in dev_names]
        count = c_int(0)

        rc = LIBPHOBOS_ADMIN.phobos_admin_device_delete(byref(self.handle),
                                                        c_id(*dev_ids),
                                                        len(dev_ids),
                                                        byref(count))

        if rc:
            raise EnvironmentError(rc, "Failed to delete device(s) '%s'" %
                                   dev_names)

        return count.value, len(dev_ids)

    def sched_conf_get(self, config_items):
        """Query LRS configuration"""
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

        for cfg, value in zip(config_items, values):
            print(f"{cfg.key}: {value.decode('utf-8')}")


    def sched_conf_set(self, config_items):
        """Update LRS configuration"""
        rc = LIBPHOBOS_ADMIN.phobos_admin_sched_conf_set(
            byref(self.handle),
            string_list2c_array(config_items, lambda c: c.section),
            string_list2c_array(config_items, lambda c: c.key),
            string_list2c_array(config_items, lambda c: c.value),
            c_size_t(len(config_items)))

        if rc:
            raise EnvironmentError(rc,
                                   "Failed to update scheduler configuration")

    def ping_lrs(self):
        """Ping the LRS daemon."""
        rc = LIBPHOBOS_ADMIN.phobos_admin_ping_lrs(byref(self.handle))

        if rc:
            raise EnvironmentError(rc, "Failed to ping phobosd")

    @staticmethod
    def ping_tlc(library):
        """Ping the TLC daemon."""
        library_is_up = c_bool(False)
        c_library = c_char_p(library.encode('utf-8'))
        rc = LIBPHOBOS_ADMIN.phobos_admin_ping_tlc(c_library,
                                                   byref(library_is_up))

        if rc:
            raise EnvironmentError(rc, "Failed to ping TLC")

        if not library_is_up:
            raise EnvironmentError(errno.ENODEV,
                                   "TLC is up but cannot access the Library")

    def device_lock(self, dev_family, dev_names, dev_library, is_forced):
        """Wrapper for the device lock command."""
        c_id = Id * len(dev_names)
        dev_ids = [Id(dev_family, name=name, library=dev_library)
                   for name in dev_names]

        rc = LIBPHOBOS_ADMIN.phobos_admin_device_lock(byref(self.handle),
                                                      c_id(*dev_ids),
                                                      len(dev_ids), is_forced)
        if rc:
            raise EnvironmentError(rc, "Failed to lock device(s) '%s'" %
                                   dev_names)

    def device_unlock(self, dev_family, dev_names, dev_library, is_forced):
        """Wrapper for the device unlock command."""
        c_id = Id * len(dev_names)
        dev_ids = [Id(dev_family, name=name, library=dev_library)
                   for name in dev_names]

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

    def layout_list(self, res, is_pattern, medium, degroup, **kwargs): # pylint: disable=too-many-locals
        """List layouts."""
        n_layouts = c_int(0)
        layouts = pointer(LayoutInfo())
        library_name = kwargs.get('library')
        copy_name = kwargs.get('copy_name')
        orphan = kwargs.get('orphan')

        enc_medium = medium.encode('utf-8') if medium else None
        enc_library = library_name.encode('utf-8') if library_name else None
        enc_copy_name = copy_name.encode('utf-8') if copy_name else None

        enc_res = [elt.encode('utf-8') for elt in res]
        c_res_strlist = c_char_p * len(enc_res)

        sort, kwargs = dss_sort('layout', **kwargs)
        sref = byref(sort) if sort else None

        rc = LIBPHOBOS_ADMIN.phobos_admin_layout_list(byref(self.handle),
                                                      c_res_strlist(*enc_res),
                                                      len(enc_res),
                                                      is_pattern,
                                                      enc_medium,
                                                      enc_library,
                                                      enc_copy_name,
                                                      orphan,
                                                      byref(layouts),
                                                      byref(n_layouts),
                                                      sref)
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
                        if library_name is None or \
                            library_name in extents[j].media.library:
                            lyt = type(layouts[i])()
                            pointer(lyt)[0] = layouts[i]
                            lyt.ext_count = 1
                            lyt.extents = pointer(extents[j])
                            list_lyts.append(lyt)

        return list_lyts, layouts, n_layouts

    def medium_locate(self, rsc_family, medium_id, library):
        """Locate a medium by calling phobos_admin_medium_locate API"""
        hostname = c_char_p(None)
        rc = LIBPHOBOS_ADMIN.phobos_admin_medium_locate(
            byref(self.handle),
            byref(Id(rsc_family, name=medium_id, library=library)),
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

    def medium_add(self, media, fstype, tags=None):
        """Add a new medium"""
        for med in media:
            init_medium(med, fstype, tags)

        c_media = MediaInfo * len(list(media))

        rc = LIBPHOBOS_ADMIN.phobos_admin_media_add(byref(self.handle),
                                                    c_media(*media),
                                                    len(list(media)))
        if rc:
            raise EnvironmentError(rc, "Failed to add media")

    def medium_delete(self, med_family, med_names, library):
        """Remove mediums to phobos system."""
        c_id = Id * len(med_names)
        med_ids = [Id(med_family, name=name, library=library)
                   for name in med_names]
        count = c_int(0)

        rc = LIBPHOBOS_ADMIN.phobos_admin_media_delete(byref(self.handle),
                                                       c_id(*med_ids),
                                                       len(med_ids),
                                                       byref(count))
        if rc:
            raise EnvironmentError(rc, "Failed to delete media(s) '%s'" %
                                   med_names)
        return count.value, len(med_ids)

    def medium_import(self, fstype, media, check_hash, tags=None):
        """Import a new medium"""
        for med in media:
            init_medium(med, fstype, tags)

        c_media = MediaInfo * len(list(media))

        rc = LIBPHOBOS_ADMIN.phobos_admin_media_import(byref(self.handle),
                                                       c_media(*media),
                                                       len(list(media)),
                                                       check_hash)

        if rc:
            raise EnvironmentError(rc, "Failed to import tape(s)")

    def dump_logs(self, fd, log_filter):
        """Dump all persistent logs to the given file"""
        rc = LIBPHOBOS_ADMIN.phobos_admin_dump_logs(byref(self.handle), fd,
                                                    log_filter)
        if rc:
            raise EnvironmentError(rc, "Failed to dump logs")

    def clear_logs(self, log_filter, clear_all_log):
        """Clear all persistent logs"""
        rc = LIBPHOBOS_ADMIN.phobos_admin_clear_logs(byref(self.handle),
                                                     log_filter,
                                                     clear_all_log)
        if rc:
            raise EnvironmentError(rc, "Failed to clear logs")

    def notify_media_update(self, media):
        """Send a notification that the information of some media was updated"""

        c_id = Id * len(media)
        ids = [medium.rsc.id for medium in media]

        rc = LIBPHOBOS_ADMIN.phobos_admin_notify_media_update(
            byref(self.handle), c_id(*ids), len(media), None
        )
        if rc:
            raise EnvironmentError(rc, "Failed to notify media update")

    def drive_lookup(self, res, library):
        """Lookup a drive"""
        drive_info = LibDrvInfo()

        rc = LIBPHOBOS_ADMIN.phobos_admin_drive_lookup(
            byref(self.handle),
            byref(Id(PHO_RSC_TAPE, name=res, library=library)),
            byref(drive_info))
        if rc:
            raise EnvironmentError(rc, f"Failed to lookup the drive {res}")

        return drive_info

    def load(self, drive_serial_or_path, tape_label, library):
        """Load a tape into a drive"""
        rc = LIBPHOBOS_ADMIN.phobos_admin_load(
            byref(self.handle),
            byref(Id(PHO_RSC_TAPE, name=drive_serial_or_path,
                     library=library)),
            byref(Id(PHO_RSC_TAPE, name=tape_label, library=library)))

        if rc:
            raise EnvironmentError(rc, f"Failed to load {tape_label} into "
                                       f"drive {drive_serial_or_path} in "
                                       f"library {library}")

    def unload(self, drive_serial_or_path, tape_label, library):
        """Load a tape into a drive"""
        rc = LIBPHOBOS_ADMIN.phobos_admin_unload(
            byref(self.handle),
            byref(Id(PHO_RSC_TAPE, name=drive_serial_or_path,
                     library=library)),
            byref(Id(PHO_RSC_TAPE, name=tape_label, library=library))
            if tape_label else None)

        if rc:
            if tape_label:
                raise EnvironmentError(rc, f"Failed to unload {tape_label} "
                                           f"from drive "
                                           f"{drive_serial_or_path} "
                                           f"in library {library}")

            raise EnvironmentError(rc, f"Failed to unload drive "
                                       f"{drive_serial_or_path} in library "
                                       f"{library}")

    def repack(self, family, medium, library, tags):
        """Repack a tape"""
        tags = StringArray(tags)
        rc = LIBPHOBOS_ADMIN.phobos_admin_repack(
            byref(self.handle), byref(Id(family, name=medium,
                                         library=library)),
            byref(tags))

        if rc:
            raise EnvironmentError(rc, f"Failed to repack {medium} from "
                                       f"library {library}")

    def medium_rename(self, family, media, library, new_lib):
        """Rename medium (for now, only the library)."""
        c_id = Id * len(media)
        med_ids = [Id(family=family, name=med, library=library)
                   for med in media]
        count = c_int(0)
        c_new_lib = c_char_p(new_lib.encode('utf-8'))

        rc = LIBPHOBOS_ADMIN.phobos_admin_media_library_rename( \
                                byref(self.handle), c_id(*med_ids),
                                len(med_ids), c_new_lib, byref(count))
        if rc:
            raise EnvironmentError(rc, "Failed to rename media(s) '%s'" % media)

        return count.value

    @staticmethod
    def layout_list_free(layouts, n_layouts):
        """Free a previously obtained layout list."""
        LIBPHOBOS_ADMIN.phobos_admin_layout_list_free(layouts, n_layouts)

    @staticmethod
    def lib_scan(lib_type, library, with_refresh):
        """Scan and return a list of dictionnaries representing the properties
        of elements in a library of type lib_type.

        The only working implementation is for PHO_LIB_SCSI through the TLC.
        """
        jansson_t = c_void_p(None)
        rc = LIBPHOBOS_ADMIN.phobos_admin_lib_scan(lib_type,
                                                   library.encode('utf-8'),
                                                   with_refresh,
                                                   byref(jansson_t))
        if rc:
            raise EnvironmentError(rc, f"Failed to scan the library "
                                       f"{library}")

        return json.loads(jansson_dumps(jansson_t.value))

    @staticmethod
    def lib_refresh(lib_type, library):
        """Reload the library internal cache of the TLC"""
        rc = LIBPHOBOS_ADMIN.phobos_admin_lib_refresh(
            lib_type, library.encode('utf-8'))
        if rc:
            raise EnvironmentError(rc,
                                   f"Failed to refresh the cache of the "
                                   f"library {library}")
