#  All rights reserved (c) 2014-2023 CEA/DAM.
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
import filecmp
import logging
import sys

sys.path.insert(0, "../utils")

from utils.utils import run


class TarTape:

    def __init__(self):
        self.at_index_begin = True
        self.drive_index = None
        self.drive_path = None
        self.current_index = 0
        self.objects = {} # "obj_name": index

    def on_load(self, drive):
        # When unloading and then loading again a tape, some libraries preserve
        # current index, but others rewind. To be technology consistent, we
        # force the rewind in all cases.
        run("mt", "-f", drive.nst(), "rewind")
        self.current_index = 0

    def put(self, oid, drive):
        self.move_to_index(len(self.objects), drive)
        run("tar", "cf", drive.nst(), oid)
        self.objects[oid] = len(self.objects)
        self.current_index = len(self.objects) # ready to write next index

    def get(self, oid, drive):
        self.move_to_index(self.objects[oid], drive)
        run("tar", "xf", drive.nst())
        self.at_index_begin = False
        return oid

    def move_to_index(self, index, drive):
        if self.current_index == index:
            if not self.at_index_begin:
                if index == 0:
                    run("mt", "-f", drive.nst(), "rewind")
                else:
                    run("mt", "-f", drive.nst(), "bsfm", "1")

        if index > self.current_index:
            run("mt", "-f", drive.nst(), "fsf",
                f"{index - self.current_index}")

        if index < self.current_index:
            if index == 0:
                run("mt", "-f", drive.nst(), "rewind")
            else:
                run("mt", "-f", drive.nst(), "bsfm",
                    f"{self.current_index - index + 1}")

        self.current_index = index
        self.at_index_begin = True


class Backend:

    def __init__(self, lib_cache):
        self.lib_cache = lib_cache
        self.tapes = dict()

    def __enter__(self):
        return self

    def __exit__(self, exception, value, traceback):
        pass

    def add_drive(self, drive):
        pass

    def init_tape(self, tape: str):
        t = TarTape()
        drive = self.lib_cache.load(tape)
        t.on_load(drive)
        self.tapes[tape] = t

    def put(self, path, tape):
        drive = self.lib_cache.load(tape)
        self.tapes[tape].put(path, drive)

    def get(self, path, tape):
        drive = self.lib_cache.load(tape)
        return self.tapes[tape].get(path, drive)


if __name__ == "__main__":
    import sys

    tape_slot_index = 1
    drive_index = 0
    drive_path = "/dev/nst0"

    tape = TarTape()
    tape.load(drive_index, drive_path)
    tape.put("/etc/hosts")
    tape.put("/etc/networks")
    tape.unload()
    tape.load(drive_index, drive_path)
    tape.get("/etc/networks")
    print(f'/etc/networks cmp: {filecmp.cmp("/etc/networks", "./etc/networks")}')
    tape.get("/etc/hosts")
    print(f'/etc/hosts cmp: {filecmp.cmp("/etc/hosts", "./etc/hosts")}')
    tape.put("/etc/passwd")
    tape.get("/etc/passwd")
    print(f'/etc/passwd cmp: {filecmp.cmp("/etc/passwd", "./etc/passwd")}')
    tape.put("/etc/shadow")
    tape.put("/etc/fstab")
    tape.get("/etc/shadow")
    print(f'/etc/shadow cmp: {filecmp.cmp("/etc/shadow", "./etc/shadow")}')
    tape.get("/etc/fstab")
    print(f'/etc/fstab cmp: {filecmp.cmp("/etc/fstab", "./etc/fstab")}')
