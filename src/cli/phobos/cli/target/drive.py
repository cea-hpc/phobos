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
Drive target for Phobos CLI
"""

import json
import sys

from phobos.cli.action import ActionOptHandler
from phobos.cli.action.add import AddOptHandler
from phobos.cli.action.list import ListOptHandler
from phobos.cli.action.resource_delete import ResourceDeleteOptHandler
from phobos.cli.action.status import StatusOptHandler
from phobos.cli.action.unlock import UnlockOptHandler
from phobos.cli.common import env_error_format
from phobos.cli.common.args import add_list_arguments
from phobos.cli.common.exec import exec_delete_medium_device
from phobos.cli.common.utils import (check_output_attributes,
                                     handle_sort_option, set_library)
from phobos.cli.target.device import DeviceLockOptHandler, DeviceOptHandler
from phobos.core.admin import Client as AdminClient
from phobos.core.const import PHO_RSC_TAPE, DSS_DEVICE # pylint: disable=no-name-in-module
from phobos.core.ffi import (DevInfo, DriveStatus, ResourceFamily)
from phobos.output import dump_object_list


class DriveListOptHandler(ListOptHandler):
    """
    Specific version of the 'list' command for tape drives, with a couple
    extra-options.
    """
    descr = 'list all drives'

    @classmethod
    def add_options(cls, parser):
        """Add resource-specific options."""
        super(DriveListOptHandler, cls).add_options(parser)
        parser.add_argument('-m', '--model', help='filter on model')

        attr = list(DevInfo().get_display_dict().keys())
        attr.sort()
        add_list_arguments(parser, attr, "name", True, True, True)


class DriveLoadOptHandler(ActionOptHandler):
    """Drive load sub-parser"""

    label = 'load'
    descr = 'load a tape into a drive'

    @classmethod
    def add_options(cls, parser):
        """Add resource-specific options."""
        super(DriveLoadOptHandler, cls).add_options(parser)
        parser.add_argument('res',
                            help='Target drive (could be the path or the '
                                 'serial number)')
        parser.add_argument('tape_label', help='Tape label to load')
        parser.add_argument('--library',
                            help="Library containing the target drive and tape")


class DriveLookupOptHandler(ActionOptHandler):
    """Drive lookp sub-parser"""

    label = 'lookup'
    descr = 'lookup a drive from library'

    @classmethod
    def add_options(cls, parser):
        """Add resource-specific options."""
        super(DriveLookupOptHandler, cls).add_options(parser)
        parser.add_argument('res',
                            help='Drive to lookup (could be '
                                 'the path or the serial number)')
        parser.add_argument('--library',
                            help="Library containing the drive to lookup")


class DriveMigrateOptHandler(ActionOptHandler):
    """Migrate an existing drive"""
    label = 'migrate'
    descr = ('migrate existing drive to another host or library '
             '(only the DSS is modified)')

    @classmethod
    def add_options(cls, parser):
        """Add resource-specific options."""
        super(DriveMigrateOptHandler, cls).add_options(parser)

        parser.add_argument('res', nargs='+', help='Drive(s) to update')
        parser.add_argument('--host', help='New host for these drives')
        parser.add_argument('--new-library',
                            help='New library for these drives')
        parser.add_argument('--library',
                            help="Library containing migrated drive(s) "
                                 "(This option is to target the good drive(s) "
                                 "among the existing libraries, not to set a "
                                 "new library.)")


class DriveScsiReleaseOptHandler(ActionOptHandler):
    """Release the SCSI reservation of an existing drive"""
    label = 'scsi_release'
    descr = 'release the SCSI reservation of an existing drive'

    @classmethod
    def add_options(cls, parser):
        """Add resource-specific options."""
        super(DriveScsiReleaseOptHandler, cls).add_options(parser)
        parser.add_argument('res', nargs='+',
                            help="Drive(s) with a SCSI reservation to release"
                                 "and a mounted tape into.")
        parser.add_argument('--library',
                            help="Library containing the drives to release")


class DriveUnloadOptHandler(ActionOptHandler):
    """Drive unload sub-parser"""

    label = 'unload'
    descr = 'unload a tape from a drive'

    @classmethod
    def add_options(cls, parser):
        """Add resource-specific options."""
        super(DriveUnloadOptHandler, cls).add_options(parser)
        parser.add_argument('res',
                            help='Target drive (could be the path or the '
                                 'serial number)')
        parser.add_argument('--tape-label',
                            help='Tape label to unload (unload is done only if '
                                 'the drive contains this label, any tape is '
                                 'unloaded if this option is not set)')
        parser.add_argument('--library',
                            help="Library containing the target drive")


class DriveOptHandler(DeviceOptHandler):
    """Tape Drive options and actions."""
    label = 'drive'
    descr = 'handle tape drives (use ID or device path to identify resource)'
    family = ResourceFamily(ResourceFamily.RSC_TAPE)
    verbs = [
        AddOptHandler,
        DeviceLockOptHandler,
        DriveListOptHandler,
        DriveLoadOptHandler,
        DriveLookupOptHandler,
        DriveMigrateOptHandler,
        DriveScsiReleaseOptHandler,
        DriveUnloadOptHandler,
        ResourceDeleteOptHandler,
        StatusOptHandler,
        UnlockOptHandler,
    ]

    library = None

    def exec_scsi_release(self):
        """Release SCSI reservation of given drive(s)"""
        resources = self.params.get('res')
        set_library(self)

        try:
            with AdminClient(lrs_required=False) as adm:
                count = adm.device_scsi_release(resources, self.library)

        except EnvironmentError as err:
            self.logger.error("%s", env_error_format(err))
            sys.exit(abs(err.errno))

        if count > 0:
            self.logger.info("Release SCSI reservation of %d drive(s)"
                             " successfully", count)

    def exec_migrate(self):
        """Migrate devices host"""
        resources = self.params.get('res')
        host = self.params.get('host')
        new_lib = self.params.get('new_library')
        set_library(self)

        if host is None and new_lib is None:
            self.logger.info("No migrate to be performed")
            return

        try:
            with AdminClient(lrs_required=False) as adm:
                count = adm.device_migrate(resources, self.library, host,
                                           new_lib)

        except EnvironmentError as err:
            self.logger.error("%s", env_error_format(err))
            sys.exit(abs(err.errno))

        if count > 0:
            self.logger.info("Migrated %d device(s) successfully", count)

    def exec_list(self):
        """List devices and display results."""
        attrs = list(DevInfo().get_display_dict().keys())
        check_output_attributes(attrs, self.params.get('output'), self.logger)

        kwargs = {}
        if self.params.get('model'):
            kwargs['model'] = self.params.get('model')

        if self.params.get('library'):
            kwargs['library'] = self.params.get('library')

        if self.params.get('status'):
            kwargs["adm_status"] = self.params.get('status')

        kwargs = handle_sort_option(self.params, DevInfo(), self.logger,
                                    **kwargs)

        objs = []
        if self.params.get('res'):
            for serial in self.params.get('res'):
                curr = self.filter(serial, **kwargs)
                if not curr:
                    continue
                assert len(curr) == 1
                objs.append(curr[0])
        else:
            objs = list(self.client.devices.get(family=self.family, **kwargs))

        if len(objs) > 0:
            dump_object_list(objs, attr=self.params.get('output'),
                             fmt=self.params.get('format'))

    def exec_status(self):
        """Display I/O and drive status"""
        try:
            with AdminClient(lrs_required=True) as adm:
                status = json.loads(adm.device_status(PHO_RSC_TAPE))
                # disable pylint's warning as it's suggestion does not work
                for i in range(len(status)): #pylint: disable=consider-using-enumerate
                    status[i] = DriveStatus(status[i])

                dump_object_list(sorted(status, key=lambda x: x.address),
                                 self.params.get('output'))

        except EnvironmentError as err:
            self.logger.error("Cannot read status of drives: %s",
                              env_error_format(err))
            sys.exit(abs(err.errno))

    def exec_delete(self):
        """Remove a device"""
        exec_delete_medium_device(self, DSS_DEVICE)

    def exec_lookup(self):
        """Lookup drive information from the library."""
        res = self.params.get('res')
        set_library(self)

        try:
            with AdminClient(lrs_required=False) as adm:
                drive_info = adm.drive_lookup(res, self.library)

        except EnvironmentError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))

        relativ_address = drive_info.ldi_addr.lia_addr - \
                          drive_info.ldi_first_addr
        print("Drive %d: address %s" % (relativ_address,
                                        hex(drive_info.ldi_addr.lia_addr)))
        print("State: %s" % ("full" if drive_info.ldi_full else "empty"))

        if drive_info.ldi_full:
            print("Loaded tape id: %s" % (drive_info.ldi_medium_id.name))

    def exec_load(self):
        """Load a tape into a drive"""
        res = self.params.get('res')
        tape_label = self.params.get('tape_label')
        set_library(self)

        try:
            with AdminClient(lrs_required=False) as adm:
                adm.load(res, tape_label, self.library)

        except EnvironmentError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))

    def exec_unload(self):
        """Unload a tape from a drive"""
        res = self.params.get('res')
        tape_label = self.params.get('tape_label')
        set_library(self)

        try:
            with AdminClient(lrs_required=False) as adm:
                adm.unload(res, tape_label, self.library)

        except EnvironmentError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))
