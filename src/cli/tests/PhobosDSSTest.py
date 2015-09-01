#!/usr/bin/python

# Phobos project - CEA/DAM
# Henri Doreau <henri.doreau@cea.fr>

"""Unit tests for phobos.dss"""

import sys
import unittest

from phobos.dss import Client, GenericError as DSSError


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
            for x in cli.device_get(family=fam):
                self.assertEqual(x.family, fam)
        cli.disconnect()

if __name__ == '__main__':
    unittest.main()
