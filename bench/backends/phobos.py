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


"""
Phobos backend for the benchmark infrastructure.
"""

import os
import subprocess
import sys

sys.path.insert(0, "../utils")

from utils.utils import run, lto_generation_str


class Backend:

    def __init__(self, lib_cache):
        self.lib_cache = lib_cache

    def __enter__(self):
        run("phobos_db", "setup_tables")
        run("systemctl", "start", "phobosd")
        return self

    def __exit__(self, exception, value, traceback):
        run("systemctl", "stop", "phobosd")
        run("phobos_db", "drop_tables")
        self.lib_cache.unloadall()

    def add_drive(self, drive):
        run("phobos", "drive", "add", "--unlock", drive.sgdevice())

    def init_tape(self, tape):
        run("phobos", "tape", "add",
                "--unlock",
                "-T", tape,
                "--type", lto_generation_str(tape),
                tape)
        run("phobos", "tape", "format", tape)

    def put(self, source_file, target_tape=None):
        oid = _source_file_to_oid(source_file)

        if target_tape:
            run("phobos", "put", "-T", target_tape, source_file, oid)
        else:
            run("phobos", "put", source_file, oid)

    def get(self, source_file, target_tape=None):
        oid = _source_file_to_oid(source_file)
        dest = oid.replace("oid", "output")

        try:
            if os.path.exists(dest):
                # Phobos does not overwrite destination file if it exists
                run("rm", "-f", dest)
            run("phobos", "get", oid, dest)
        except Exception as e:
            # Ensure rm on error to avoid tedious manual cleanup
            run("rm", dest)
            raise e

        return dest


def _source_file_to_oid(source_file):
    return source_file + '.oid'


if __name__ == '__main__':
    import filecmp
    import argparse

    def _clean_test(source_file):
        oid = _source_file_to_oid(source_file)
        dest = source_file.replace("input", "output")

        os.unlink(dest)

        args = ['/usr/bin/phobos', 'delete', oid]

        p = subprocess.Popen(args)
        p.wait()
        return p.returncode


    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source_file")
    parser.add_argument('-t', '--target_tape', default=None)
    args = parser.parse_args()

    put(args.source_file, args.target_tape)
    get(args.source_file)

    dest = args.source_file.replace("input", "output")

    assert filecmp.cmp(args.source_file, dest)

    _clean_test(args.source_file)
