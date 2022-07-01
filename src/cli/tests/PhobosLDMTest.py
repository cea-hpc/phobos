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

"""Unit tests for phobos.ldm"""

import errno
import os
import subprocess
import unittest
import re

from phobos.core.admin import Client as AdminClient
from phobos.core.const import PHO_LIB_SCSI # pylint: disable=no-name-in-module


# Conventional lib device for phobos tests
LIB_TEST_DEV = "/dev/changer"

class LdmTest(unittest.TestCase):
    """
    This test case issue requests to the DSS to stress the python bindings.
    """

    # XXX: refactor this test to split the code in subfunctions
    # pylint: disable=too-many-branches,too-many-locals,too-many-statements
    def test_lib_scan(self):
        """Test LdmAdapter.scan() against mtx output"""
        if not os.access(LIB_TEST_DEV, os.W_OK):
            self.skipTest("%s not writable or absent" % (LIB_TEST_DEV,))

        # Check that mtx is available
        try:
            subprocess.check_call(["mtx"])
        except OSError as exc:
            if exc.errno == errno.ENOENT:
                self.skipTest("mtx not available")

        # Ensure a tape is loaded
        subprocess.call(
            ["mtx", "-f", LIB_TEST_DEV, "load", "1"],
            # Silence output (it should not be verbose enough to stall)
            stderr=subprocess.PIPE, stdout=subprocess.PIPE,
        )

        # Retrieve mtx output
        mtx = subprocess.Popen(
            ["mtx", "-f", LIB_TEST_DEV, "status"],
            stdout=subprocess.PIPE,
        )
        mtx_output, _ = mtx.communicate()

        # Parse mtx output:
        #   Storage Changer /dev/changer:4 Drives, 24 Slots ( 4 Import/Export )
        # Data Transfer Element 0:Full (Storage Element 2 Loaded):VolumeTag = P00001L5 # pylint: disable=line-too-long
        # Data Transfer Element 1:Empty
        # ...
        #       Storage Element 1:Empty
        #       Storage Element 2:Full :VolumeTag=P00002L5
        # ...
        #       Storage Element 21 IMPORT/EXPORT:Empty
        rel_addr_re =\
            re.compile(r".*Element (?P<rel_addr>\d+)(?: IMPORT/EXPORT)?:.*")
        rel_addr_loaded_re =\
            re.compile(r".*Storage Element (?P<rel_addr>\d+) Loaded.*")
        volume_re = re.compile(r".*:VolumeTag ?= ?(?P<volume>\w+).*")

        mtx_elts = {}
        imp_exp_base_addr = None
        for line in mtx_output.splitlines():
            line = line.strip().decode('utf-8')
            # Ignore lines we don't want to parse
            if " Element " not in line:
                continue
            full = ":Full" in line
            # mtx has its own way of displaying scsi addresses of the lib
            # components, the obscure address translations done here tries to
            # normalize it
            rel_addr = int(rel_addr_re.match(line).group("rel_addr"))

            if "Data Transfer Element" in line:
                elt_type = u"drive"
            elif "Storage Element" in line:
                if "IMPORT/EXPORT" in line:
                    if imp_exp_base_addr is None:
                        imp_exp_base_addr = rel_addr
                    rel_addr -= imp_exp_base_addr
                    elt_type = u"import/export"
                else:
                    # slot numbers start at 1
                    rel_addr -= 1
                    elt_type = u"slot"

            elt = {"full": full, "rel_addr": rel_addr}
            rel_addr_loaded_m = rel_addr_loaded_re.match(line)
            if rel_addr_loaded_m:
                # slot numbers start at 1
                elt["rel_addr_loaded"] =\
                    int(rel_addr_loaded_m.group("rel_addr")) - 1
            volume_m = volume_re.match(line)
            if volume_m:
                elt["volume"] = str(volume_m.group("volume"))
            mtx_elts.setdefault(elt_type, []).append(elt)

        # Retrieve data as seen by ldm_lib_scan
        with AdminClient(lrs_required=False) as adm:
            raw_lib_data = adm.lib_scan(PHO_LIB_SCSI, LIB_TEST_DEV)

        # Find the base address for each type to the first address seen (they
        # should be sorted)
        base_addrs = {}
        for lib_elt in raw_lib_data:
            base_addrs.setdefault(lib_elt["type"], lib_elt["address"])

        # Reshape data to be comparable to mtx
        lib_elts = {}
        for lib_elt in raw_lib_data:
            elt = {
                "full": lib_elt["full"],
                "rel_addr": lib_elt["address"] - base_addrs[lib_elt["type"]],
            }
            if "volume" in lib_elt:
                elt["volume"] = lib_elt["volume"]
            # Source address is not supported for slots in mtx
            if lib_elt["type"] != "slot" and "source_address" in lib_elt:
                elt["rel_addr_loaded"] =\
                    lib_elt["source_address"] - base_addrs["slot"]
            lib_elts.setdefault(lib_elt["type"], []).append(elt)

        # Check lib_elts conformity
        self.maxDiff = None # pylint: disable=invalid-name
        self.assertGreaterEqual(len(lib_elts.pop("arm")), 1)
        self.assertEqual(mtx_elts, lib_elts)


if __name__ == '__main__':
    unittest.main(buffer=True)
