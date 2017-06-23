#!/usr/bin/python

#
#  All rights reserved (c) 2014-2017 CEA/DAM.
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

"""Unit tests for phobos.dss"""

import sys
import unittest
import os

from random import randint

from phobos.dss import Client
from phobos.dss import GenericError as DSSError
from phobos.dss import CliMedia, CliDevice

from phobos.capi.dss import layout_info, media_info, dev_info, PHO_DEV_DIR
from phobos.capi.dss import dev_family2str


class DSSClientTest(unittest.TestCase):
    """
    This test case issue requests to the DSS to stress the python bindings.
    """

    def test_client_connect(self):
        """Connect to backend with valid parameters."""
        cli = Client()
        cli.connect()
        cli.disconnect()

    def test_client_connect_refused(self):
        """Connect to backend with invalid parameters."""
        cli = Client()
        environ_save = os.environ['PHOBOS_DSS_connect_string']
        os.environ['PHOBOS_DSS_connect_string'] = \
                "dbname='tata', user='titi', password='toto'"
        self.assertRaises(DSSError, cli.connect)
        os.environ['PHOBOS_DSS_connect_string'] = environ_save

    def test_list_devices_by_family(self):
        """List devices family by family."""
        cli = Client()
        cli.connect()
        for fam in ('tape', 'disk', 'dir'):
            for dev in cli.devices.get(family=fam):
                self.assertEqual(dev_family2str(dev.family), fam)
        cli.disconnect()

    def test_list_media(self):
        """List media."""
        cli = Client()
        cli.connect()
        for mda in cli.media.get():
            self.assertTrue(isinstance(mda, CliMedia))
        cli.disconnect()

    def test_list_extents(self):
        """List extents."""
        cli = Client()
        cli.connect()
        for ext in cli.extents.get():
            self.assertTrue(isinstance(ext, layout_info))
        cli.disconnect()

    def test_getset(self):
        """GET / SET an object to validate the whole chain."""
        cli = Client()
        cli.connect()

        dev = dev_info()
        dev.family = PHO_DEV_DIR
        dev.model = ''
        dev.path = '/tmp/test_%d' % randint(0, 1000000)
        dev.host = 'localhost'
        dev.serial = '__TEST_MAGIC_%d' % randint(0, 1000000)

        rc = cli.devices.insert([dev])
        self.assertEqual(rc, 0)

        res = cli.devices.get(serial=dev.serial)
        for devt in res:
            self.assertTrue(isinstance(devt, CliDevice))
            self.assertEqual(devt.serial, dev.serial)

        rc = cli.devices.delete(res)
        self.assertEqual(rc, 0)

        cli.disconnect()

if __name__ == '__main__':
    unittest.main()
