#!/bin/python3
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


from subprocess import Popen, PIPE, STDOUT
import os
import logging


def run(*args):
    logging.debug("%s", " ".join(args))
    p = Popen(args, stdout=PIPE, stderr=PIPE, shell=False)
    stdout, stderr = p.communicate()
    if p.returncode:
        raise OSError(p.returncode,
                      "Command: " + " ".join(args) + "\n" +
                      stdout.decode('utf-8') + "\n" +
                      stderr.decode('utf-8'))

    return stdout.decode('utf-8')


def display_error(e: OSError, msg):
    logging.error(f"{msg}: {os.strerror(e.errno)}")
    for l in e.strerror.split('\n'):
        logging.error(l)

def lto_generation_str(tape: str) -> str:
    if tape.endswith("L5"):
        return "LTO5"
    elif tape.endswith("L6"):
        return "LTO6"
    else:
        logging.error(f"Unknow LTO generation for tape '{name}'")
