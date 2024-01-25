#!/bin/python3
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

import argparse
import sys
import os
import json
import filecmp
import logging

from typing import List
from operator import itemgetter
from itertools import groupby
from subprocess import Popen, PIPE
from datetime import datetime

from backends import (
    dd,
    tar,
    ltfs,
    phobos
)
from utils.utils import (
    display_error,
    run
)

from phobos.core.admin import Client as AdminClient
from phobos.core.const import PHO_LIB_SCSI


class InvalidAction(Exception):

    def __init__(self, line: int, action: str):
        self.line = line
        self.action = action

    def __str__(self):
        return f"Invalid action type '{self.action}' at line '{self.line}'"


class Action:

    def __init__(self, t, path: str, medium: str = None):
        self.t = t
        self.path = path
        self.medium = medium

    def is_put(self) -> bool:
        return self.t == 'put'

    def is_get(self) -> bool:
        return self.t == 'get'

    def __repr__(self):
        if self.medium:
            return f"{self.t} {self.medium} -> {self.path}"
        else:
            return f"{self.t} {self.path}"


class FileParser:

    def __init__(self, path: str):
        self.file = open(path, "r")
        self.line = 0

    def __iter__(self):
        return self

    def __next__(self):
        line = self.file.readline().strip().rstrip("\n")
        self.line += 1
        if line == "":
            self.file.close()
            raise StopIteration

        line = [x for x in line.split(" ") if x]
        assert len(line) == 2 or len(line) == 3

        t = line[0].lower()
        if t != "get" and t != "put":
            raise InvalidAction(self.line, action)

        if len(line) == 3:
            return Action(t, line[1], line[2])
        else:
            return Action(t, line[1])


def drive_lookup(st_drive):
    sg = os.readlink(f"/sys/class/scsi_tape/{st_drive}/device/generic")
    sg = sg.split("/")[1]
    with open(f"/sys/class/scsi_tape/{st_drive}/device/vpd_pg80", "rb") as f:
        content = f.read()
        assert content[1] == 0x80
        length = content[3]
        assert len(content) >= 4 + length
        serial = content[4:4+length].decode('utf-8')

    return sg, serial


class Drive:

    def __init__(self, item):
        # Split by space and remove empty string (device_id contains multiple
        # consecutive spaces).
        device_id = [x for x in item['device_id'].split(' ') if x]
        self.model = device_id[1]
        self.serial = device_id[2]

        self.address = item['address']
        self.source_address = item.get('source_address', None)
        self.volume = item.get('volume', None)
        self.st = None
        self.sg = None

    def load(self, slot):
        self.source_address = slot.address
        self.volume = slot.volume

    def unload(self):
        self.source_address = None
        self.volume = None

    def sgdevice(self):
        return f"/dev/{self.sg}"

    def nst(self):
        return f"/dev/n{self.st}"

    def __repr__(self):
        s = f"{hex(self.address)}: {self.serial} ({self.model})"

        if self.volume is not None:
            s += f" ({self.volume} {self.source_address})"
        if self.st is not None:
            s += f" ({self.st},{self.sg})"

        return s


class Slot:

    def __init__(self, item):
        self.volume = item.get('volume', None)
        self.address = item['address']

    def empty(self):
        self.volume = None

    def fill(self, tape):
        self.volume = tape

    def __repr__(self):
        if self.volume is not None:
            return f"{hex(self.address)}: {self.volume}"
        else:
            return f"{hex(self.address)}: empty"


def filter_drives_tapes(lib_data):
    return (
        sorted(map(Slot, filter(lambda item: item['type'] == "slot", lib_data)),
               key=lambda slot: slot.address),
        sorted(map(Drive, filter(lambda item: item['type'] == "drive",
                                 lib_data)),
               key=lambda drive: drive.address)
    )


def drive_lto_generation(model: str) -> int:
    if model in ["ULT3580-TD5", "ULTRIUM-TD5" ]:
        return 5
    elif model in ["ULT3580-TD6", "ULTRIUM-TD6" ]:
        return 6
    else:
        logging.error(f"Unknow drive LTO model '{model}'")
        return -1


def tape_lto_generation(name: str) -> int:
    # XXX this is assuming a particular pattern in the names of the tapes but
    # this is simpler than reimplementing the same logic as Phobos and will work
    # for our purposes.
    if name.endswith("L5"):
        return 5
    elif name.endswith("L6"):
        return 6
    else:
        logging.error(f"Unknow LTO generation for tape '{name}'")


def tape_drive_compat(drive: Drive, tape: str) -> bool:
    tape_generation = tape_lto_generation(tape)
    drive_generation = drive_lto_generation(drive.model)

    # XXX this is minimalistic but will fit our purposes
    if tape_generation == 5:
        return drive_generation == 5 or drive_generation == 6
    elif tape_generation == 6:
        return drive_generation == 6 or drive_generation == 7
    else:
        return False


def can_load_tape(drive: Drive, tape: str) -> bool:
    if drive.volume is not None:
        # Cannot load in full drive
        return False

    return tape_drive_compat(drive, tape)


class LibCache:

    def __init__(self, lib_data):
        self.load_cache(lib_data)

    def load_cache(self, lib_data):
        self.slots, self.drives = filter_drives_tapes(lib_data)
        self.slots, self.drives = list(self.slots), list(self.drives)

        for index in range(len(self.drives)):
            # Assume that st start from 0 and there is one per drive
            sg, serial = drive_lookup(f"st{index}")
            cached = filter(lambda d: d.serial == serial, self.drives)
            cached = list(cached)
            assert len(cached) == 1

            cached[0].st = f"st{index}"
            cached[0].sg = sg

    def __repr__(self):
        s = "Slots:"
        for slot in self.slots:
            s += '\n' + slot.__repr__()
        s += "\n\nDrives:"
        for drive in self.drives:
            s += '\n' + drive.__repr__()

        return s

    def drive_index(self, drive: Drive):
        return drive.address - self.drives[0].address

    def find_loaded_tape(self, tape: str) -> Drive:
        drive = list(filter(lambda drive: drive.volume == tape, self.drives))
        if len(drive) == 0:
            return None

        assert len(drive) == 1

        return drive[0]

    def slot_index(self, tape: str):
        slot = list(filter(lambda slot: slot.volume == tape, self.slots))
        if len(slot) == 1:
            return slot[0].address - self.slots[0].address

        drive = list(filter(lambda drive: drive.volume == tape, self.drives))
        assert len(drive) == 1

        return drive[0].source_address - self.slots[0].address

    def first_empty_drive(self, tape: str):
        drives = list(filter(lambda drive: can_load_tape(drive, tape),
                             self.drives))
        if len(drives) == 0:
            return None

        return drives[0]

    def source_slot(self, drive: Drive) -> Slot:
        assert drive.source_address is not None

        return self.slots[drive.source_address - self.slots[0].address]

    def load(self, tape: str) -> Drive:
        drive = self.find_loaded_tape(tape)
        if drive is not None:
            return drive

        drive = self.first_empty_drive(tape)
        assert drive

        slot_index = self.slot_index(tape)
        # Slots are sorted by address
        slot = self.slots[slot_index]

        # Slot indexes start at 1 but drives at 0
        run("mtx", "load", str(slot_index + 1), str(self.drive_index(drive)))

        drive.load(slot)
        slot.empty()

        return drive

    def unload(self, tape: str):
        drive = self.find_loaded_tape(tape)
        assert drive is not None

        slot = self.source_slot(drive)
        # mtx slot indexes start at 1 but drives at 0
        run("mtx", "unload",
            str(drive.source_address - self.slots[0].address + 1),
            str(self.drive_index(drive)))

        slot.fill(drive.volume)
        drive.unload()

    def reload_cache(self):
        with AdminClient(lrs_required=False) as adm:
            lib_data = adm.lib_scan(PHO_LIB_SCSI, "/dev/changer", True)
            self.load_cache(lib_data)

    def unloadall(self):
        self.reload_cache()
        for d in self.drives:
            if d.volume is not None:
                self.unload(d.volume)


def get_backend(backend_name: str):
    if backend_name == "phobos":
        return phobos
    elif backend_name == "ltfs":
        return ltfs
    elif backend_name == "dd":
        return dd
    elif backend_name == "tar":
        return tar
    else:
        raise Exception()


class Benchmark:

    def __init__(self, step, source_file, backend):
        self.step = step
        self.source_file = source_file
        self.backend = backend

    def __enter__(self):
        self.start = datetime.now()
        print(f"{self.step} {self.source_file} with backend '{self.backend}'",
              f"started at {self.start}")
        return self

    def __exit__(self, exception, value, stacktrace):
        self.end = datetime.now()
        print(f"{self.step} finished at {self.end}.",
              f"Duration: {self.end - self.start}")


def main(args):
    actions = list(FileParser(args.source_file))
    backend = get_backend(args.backend)
    if args.debug:
        logging.basicConfig(
            format='%(levelname)s %(message)s',
            level=logging.DEBUG
        )
    else:
        logging.basicConfig(
            format='%(levelname)s %(message)s',
            level=logging.INFO
        )

    with AdminClient(lrs_required=False) as adm:
        lib_data = adm.lib_scan(PHO_LIB_SCSI, "/dev/changer", True)
        lib_cache = LibCache(lib_data)

    # fetch list of unique tapes in the action list to format
    tapes = map(itemgetter(0),
                groupby(
                    sorted(filter(lambda action: action.medium is not None,
                                  actions),
                           key=lambda action: action.medium),
                    key=lambda action: action.medium))

    with backend.Backend(lib_cache) as b:
        for d in lib_cache.drives:
            b.add_drive(d)

        for tape in tapes:
            try:
                b.init_tape(tape)
            except OSError as e:
                display_error(e, f"Failed to initialize {tape}")

        with Benchmark("Setup benchmark", args.source_file, args.backend):
            for action in actions:
                if action.is_get() and action.medium:
                    b.put(action.path, action.medium)
                    if args.check:
                        out = b.get(action.path, action.medium)
                        match = filecmp.cmp(out, action.path)
                        if not match:
                            logging.error(f"{out} != {action.path}")
                        else:
                            logging.info(f"{out} == {action.path}")

        # clean cache before test
        run("sync")
        run("bash", "-c", "echo 3 > /proc/sys/vm/drop_caches")

        with Benchmark("Benchmark", args.source_file, args.backend):
            for action in actions:
                if action.is_put():
                    b.put(action.path, action.medium)
                elif action.is_get():
                    b.get(action.path, action.medium)

if __name__ == "__main__":

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("backend",
                        choices=["phobos", "ltfs", "dd", "tar"],
                        help="Name of the backend")
    parser.add_argument("source_file",
                        help="Benchmark input file")
    parser.add_argument("--check",
                        help="Read each file after a put to check the " +
                        "integrity of the data",
                        action='store_true')
    parser.add_argument('-d', "--debug", action='store_true')

    main(parser.parse_args())
