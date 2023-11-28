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
from filecmp import cmp
import sys
import logging

sys.path.insert(0, "../utils")

from utils.utils import run

def _erase_tape(device):
    run("mt", "-f", device, "erase")

def _rewind_tape(device):
    run("mt", "-f", device, "rewind")

def _go_to_mark_tape(device, index):
    run("mt", "-f", device, "asf", str(index))

def _go_to_end_tape(device):
    run("mt", "-f", device, "eod")

def _get_current_file_index_on_tape(device):
    output = run("mt", "-f", device, "status")

    for i, line in enumerate(output.split()):
        if 'number=' not in line or output.split()[i-1] != 'File':
            continue
        return int(line.split('=')[1].split(',')[0])

    return -1

class Backend:

    def __init__(self, lib_cache):
        self.lib_cache = lib_cache
        # key: "<tapeid>",
        # value: {
        #    "<oid1>": { "device": "/dev/nst0", "idx": 3 },
        #    "<oid2>": { ... },
        #    ...
        # }
        self.tapes = dict()

    def __enter__(self):
        return self

    def __exit__(self, exception, value, traceback):
        pass

    def add_drive(self, drive):
        pass

    def init_tape(self, tape: str):
        drive = self.lib_cache.load(tape)
        _rewind_tape(drive.nst())
        _erase_tape(drive.nst())
        self.tapes[tape] = dict()

    def put(self, object_path, tape):
        logging.debug(f"Put: {object_path}")
        drive = self.lib_cache.load(tape)
        _go_to_end_tape(drive.nst())

        run("dd", f"if={object_path}", f"of={drive.nst()}")

        next_idx = _get_current_file_index_on_tape(drive.nst())
        assert tape in self.tapes
        self.tapes[tape][object_path] = {
            'device': drive.nst(),
            'idx': next_idx - 1
        }
        logging.debug(f"Position: {self.tapes[tape][object_path]}")

    def get(self, object_path, tape):
        logging.debug(f"Get: {object_path}")
        assert tape in self.tapes
        target = self.tapes[tape][object_path]
        _go_to_mark_tape(target['device'], target['idx'])

        run("dd", f"if={target['device']}", f"of={object_path}.out")

        return f"{object_path}.out"

if __name__ == "__main__":
    TAPE = '/dev/nst1'

    _rewind_tape(TAPE)
    _erase_tape(TAPE)
    put('/tmp/entry-1', TAPE)
    put('/tmp/entry-large', TAPE)
    put('/tmp/entry-2', TAPE)
    get('/tmp/entry-large')
    get('/tmp/entry-1')
    get('/tmp/entry-2')

    print(cmp('/tmp/entry-1', '/tmp/entry-1.out'))
    print(cmp('/tmp/entry-2', '/tmp/entry-2.out'))
    print(cmp('/tmp/entry-large', '/tmp/entry-large.out'))
