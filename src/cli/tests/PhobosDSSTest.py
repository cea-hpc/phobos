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

from phobos.core.dss import Client
from phobos.core.ffi import MediaInfo, DevInfo
from phobos.core.const import dev_family2str, PHO_DEV_DIR


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
        self.assertRaises(EnvironmentError, cli.connect)
        os.environ['PHOBOS_DSS_connect_string'] = environ_save

    def test_list_devices_by_family(self):
        """List devices family by family."""
        with Client() as client:
            for fam in ('tape', 'disk', 'dir'):
                for dev in client.devices.get(family=fam):
                    self.assertEqual(dev_family2str(dev.family), fam)

    def test_list_media(self):
        """List media."""
        with Client() as client:
            for mda in client.media.get():
                # replace with assertIsInstance when we drop pre-2.7 support
                self.assertTrue(isinstance(mda, MediaInfo))

    def test_getset(self):
        """GET / SET an object to validate the whole chain."""
        with Client() as client:
            insert_list = []
            for i in range(10):
                dev = DevInfo()
                dev.family = PHO_DEV_DIR
                dev.model = ''
                dev.path = '/tmp/test_%d' % randint(0, 1000000)
                dev.host = 'localhost'
                dev.serial = '__TEST_MAGIC_%d' % randint(0, 1000000)

                insert_list.append(dev)

            client.devices.insert(insert_list)

            # now retrieve them one by one and check serials
            for dev in insert_list:
                res = client.devices.get(serial=dev.serial)
                for retrieved_dev in res:
                    # replace with assertIsInstance when we drop pre-2.7 support
                    self.assertTrue(isinstance(retrieved_dev, dev.__class__))
                    self.assertEqual(retrieved_dev.serial, dev.serial)

            client.devices.delete(res)

    def test_manipulate_empty(self):
        """SET/DEL empty and None objects."""
        with Client() as client:
            client.devices.insert([])
            client.devices.insert(None)
            client.devices.delete([])
            client.devices.delete(None)

            client.media.insert([])
            client.media.insert(None)
            client.media.delete([])
            client.media.delete(None)

    def test_media_lock_unlock(self):
        """Test media lock and unlock wrappers"""
        with Client() as client:
            # Create a dummy media in db
            label = '/some/path_%d' % randint(0, 1000000)
            client.media.add(PHO_DEV_DIR, 'POSIX', None, label, locked=False)

            # Get the created media from db
            media = client.media.get(id=label)[0]

            # It should not be locked yet
            self.assertFalse(media.is_locked())

            # Lock it in db
            client.media.lock([media])

            # Media cannot be locked twice
            with self.assertRaises(EnvironmentError):
                client.media.lock([media])

            # Retrieve an up-to-date version
            media = client.media.get(id=label)[0]

            # This one should be locked
            self.assertTrue(media.is_locked())

            # Unlock it
            client.media.unlock([media])

            # Unlocking twice works
            client.media.unlock([media])

            # The up-to-date version isn't locked anymore
            media = client.media.get(id=label)[0]
            self.assertFalse(media.is_locked())


if __name__ == '__main__':
    unittest.main()
