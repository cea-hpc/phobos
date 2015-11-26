#!/usr/bin/python

# Phobos project - CEA/DAM
# Henri Doreau <henri.doreau@cea.fr>

"""Unit tests for phobos.dss"""

import sys
import unittest

from random import randint

from phobos.dss import Client
from phobos.dss import GenericError as DSSError

from phobos.capi.dss import layout_info, media_info, dev_info, PHO_DEV_DIR
from phobos.capi.dss import dev_family2str


class DSSClientTest(unittest.TestCase):
    """
    This test case issue requests to the DSS to stress the python bindings.
    """

    def test_client_connect(self):
        """Connect to backend with valid parameters."""
        cli = Client()
        cli.connect(dbname='phobos', user='phobos', password='phobos')
        cli.disconnect()

    def test_client_connect_refused(self):
        """Connect to backend with invalid parameters."""
        cli = Client()
        self.assertRaises(DSSError, cli.connect,
                          dbname='tata', user='titi', password='toto')
        self.assertRaises(DSSError, cli.connect, inval0=0, inval1=1)
        self.assertRaises(DSSError, cli.connect)

    def test_list_devices_by_family(self):
        """List devices family by family."""
        cli = Client()
        cli.connect(dbname='phobos', user='phobos', password='phobos')
        for fam in ('tape', 'disk', 'dir'):
            for dev in cli.devices.get(family=fam):
                self.assertEqual(dev_family2str(dev.family), fam)
        cli.disconnect()

    def test_list_media(self):
        """List media."""
        cli = Client()
        cli.connect(dbname='phobos', user='phobos', password='phobos')
        for mda in cli.media.get():
            self.assertTrue(isinstance(mda, media_info))
        cli.disconnect()

    def test_list_extents(self):
        """List extents."""
        cli = Client()
        cli.connect(dbname='phobos', user='phobos', password='phobos')
        for ext in cli.extents.get():
            self.assertTrue(isinstance(ext, layout_info))
        cli.disconnect()

    def test_getset(self):
        """GET / SET an object to validate the whole chain."""
        cli = Client()
        cli.connect(dbname='phobos', user='phobos', password='phobos')

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
            self.assertTrue(isinstance(devt, dev_info))
            self.assertEqual(devt.serial, dev.serial)

        rc = cli.devices.delete(res)
        self.assertEqual(rc, 0)

        cli.disconnect()

if __name__ == '__main__':
    unittest.main()
