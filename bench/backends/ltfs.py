#
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
import os
import logging
import sys

sys.path.insert(0, "../utils")

from utils.utils import run


def resolve_link(path):
    if not os.path.islink(path):
        return path

    link = os.readlink(path)
    if link[0] == '/':
        return link
    else:
        return os.path.dirname(path) + "/" + link


class Backend:

    def __init__(self, lib_cache):
        self.lib_cache = lib_cache
        self.file_to_tape = dict()
        self.tapes = list()

    def __enter__(self):
        return self

    def __exit__(self, exception, value, traceback):
        for tape in self.tapes:
            if self.is_mounted(tape):
                self.umount(tape)

    def add_drive(self, drive):
        # Nothing to do for LTFS here
        pass

    def init_tape(self, tape: str):
        self.tapes.append(tape)
        drive = self.lib_cache.load(tape)
        self.format(drive.sgdevice(), tape)

    def is_mounted(self, tape):
        mountpoint = f"/mnt/ltfs-{tape}"
        return os.path.exists(mountpoint) and os.path.ismount(mountpoint)

    def mount(self, drive_sg: str, tape: str):
        mountpoint = f"/mnt/ltfs-{tape}"
        if not os.path.exists(mountpoint):
            os.makedirs(mountpoint)

        run("ltfs", "-o", f"devname={drive_sg}", "-o", "sync_type=close",
            mountpoint)

    def umount(self, tape: str):
        run("umount", f"/mnt/ltfs-{tape}")

    def format(self, drive_sg: str, tape: str):
        run("mkltfs", "-f", "-d", drive_sg, "-n", tape,
            "-s", tape[:6])

    def path(self, tape, oid):
        mountpoint = f"/mnt/ltfs-{tape}"
        oid = oid.replace("/", "_")
        return f"{mountpoint}/{oid}"

    def put(self, oid, tape):
        drive = self.lib_cache.load(tape)
        if not self.is_mounted(tape):
            self.mount(drive.sgdevice(), tape)
        fd = os.open(oid, os.O_RDONLY)
        outfd = os.open(self.path(tape, oid), os.O_WRONLY | os.O_CREAT)

        size = os.lstat(resolve_link(oid)).st_size
        os.sendfile(outfd, fd, None, size)
        logging.debug(f"Put: copy {oid} to {self.path(tape, oid)} (size: {size})")

        os.close(outfd)
        os.close(fd)

        self.file_to_tape[oid.replace("/", "_")] = tape

    def get(self, oid, _tape):
        oid = oid.replace("/", "_")
        outpath = f"{oid}.out"

        tape = self.file_to_tape[oid]
        drive = self.lib_cache.load(tape)
        ltfsfd = os.open(self.path(tape, oid), os.O_RDONLY)
        outfd = os.open(outpath, os.O_WRONLY | os.O_CREAT, 0o644)

        size = os.lstat(self.path(tape, oid)).st_size
        os.sendfile(outfd, ltfsfd, None, size)
        logging.debug(f"Get: copy {self.path(tape, oid)} to {outpath} (size: {size})")

        os.close(outfd)
        os.close(ltfsfd)

        return outpath


if __name__ == "__main__":
    from filecmp import cmp
    import sys

    drive = sys.argv[1]
    tape = sys.argv[2]

    init(drive, tape)
    put("/etc/hosts", tape)
    put("test1", tape)
    put("test2", tape)
    print(cmp(get("/etc/hosts"), "/etc/hosts"))
    print(cmp(get("test1"), "test1"))
    print(cmp(get("test2"), "test2"))
