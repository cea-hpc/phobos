#!/usr/bin/env python3

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
Phobos command-line interface utilities.

Phobos action handlers (AH). AHs are descriptors of the objects that phobos can
manipulate. They expose both command line subparsers, to retrieve and validate
specific command line parameters and the API entry points for phobos to trigger
actions.
"""

import sys

# Without this import, a serialization error occurs because an assert
# inside protoc fails on pho_request__descriptor when trying to send a request
# from the store to the lrs. The descriptor inside the request differs from the
# global descriptor, causing the assert to fail. The descriptor inside the
# request comes from libphobos_admin, whereas the global descriptor comes from
# libphobos_store. Keep this import until the problem is resolved.
import phobos.core.admin # pylint: disable=unused-import

from phobos.cli.common import PhobosActionContext
from phobos.cli.target.copy import CopyOptHandler
from phobos.cli.target.dir import DirOptHandler
from phobos.cli.target.drive import DriveOptHandler
from phobos.cli.target.extent import ExtentOptHandler
from phobos.cli.target.lib import LibOptHandler
from phobos.cli.target.lock import LocksOptHandler
from phobos.cli.target.logs import LogsOptHandler
from phobos.cli.target.object import ObjectOptHandler
from phobos.cli.target.phobosd import PhobosdOptHandler
from phobos.cli.target.rados import RadosPoolOptHandler
from phobos.cli.target.sched import SchedOptHandler
from phobos.cli.target.store import (StoreDeleteOptHandler, StoreGetOptHandler,
                                     StoreGetMDOptHandler,
                                     StoreLocateOptHandler, StoreMPutOptHandler,
                                     StorePutOptHandler, StoreRenameOptHandler,
                                     StoreUndeleteOptHandler)
from phobos.cli.target.tape import TapeOptHandler
from phobos.cli.target.tlc import TLCOptHandler


HANDLERS = [
    # Resource interfaces
    CopyOptHandler,
    DirOptHandler,
    DriveOptHandler,
    ExtentOptHandler,
    LibOptHandler,
    LocksOptHandler,
    LogsOptHandler,
    ObjectOptHandler,
    PhobosdOptHandler,
    RadosPoolOptHandler,
    SchedOptHandler,
    TapeOptHandler,
    TLCOptHandler,

    # Store command interfaces
    StoreDeleteOptHandler,
    StoreLocateOptHandler,
    StoreGetOptHandler,
    StoreGetMDOptHandler,
    StoreMPutOptHandler,
    StorePutOptHandler,
    StoreRenameOptHandler,
    StoreUndeleteOptHandler
]

def phobos_main(args=None):
    """
    Entry point for the `phobos' command. Indirectly provides
    argument parsing and execution of the appropriate actions.
    """
    with PhobosActionContext(HANDLERS,
                             args if args is not None else sys.argv[1::]
                            ) as pac:
        pac.run()
