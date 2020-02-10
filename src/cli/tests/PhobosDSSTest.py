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
from phobos.core.ffi import DevInfo, Id, MediaInfo, Resource
from phobos.core.const import PHO_RSC_DIR, PHO_RSC_TAPE, rsc_family2str


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
                    self.assertEqual(rsc_family2str(dev.family), fam)

    def test_list_media(self):
        """List media."""
        with Client() as client:
            for mda in client.media.get():
                # replace with assertIsInstance when we drop pre-2.7 support
                self.assertTrue(isinstance(mda, MediaInfo))

            # Check negative indexation
            medias = client.media.get()
            self.assertEqual(medias[0].name, medias[-len(medias)].name)
            self.assertEqual(medias[len(medias) - 1].name, medias[-1].name)

    def test_list_media_by_tags(self):
        with Client() as client:
            client.media.add(PHO_RSC_DIR, 'POSIX', None, 'm0')
            client.media.add(PHO_RSC_DIR, 'POSIX', None, 'm1', tags=['foo'])
            client.media.add(PHO_RSC_DIR, 'POSIX', None, 'm2', \
                                                    tags=['foo', 'bar'])
            client.media.add(PHO_RSC_DIR, 'POSIX', None, 'm3', \
                                                    tags=['goo', 'foo'])
            client.media.add(PHO_RSC_DIR, 'POSIX', None, 'm4', \
                                                    tags=['bar', 'goo'])
            client.media.add(PHO_RSC_DIR, 'POSIX', None, 'm5', \
                                                    tags=['foo', 'bar', 'goo'])
            n_tags = {'foo': 4, 'bar': 3, 'goo': 3}
            n_bar_foo = 2

            for tag, n_tag in n_tags.iteritems():
                n = 0
                for medium in client.media.get(tags=tag):
                    self.assertTrue(tag in medium.tags)
                    n += 1
                self.assertEqual(n, n_tag)

            n = 0
            for medium in client.media.get(tags=['bar', 'foo']):
                self.assertTrue('bar' in medium.tags)
                self.assertTrue('foo' in medium.tags)
                n += 1
            self.assertEqual(n, n_bar_foo)

    def test_getset(self):
        """GET / SET an object to validate the whole chain."""
        with Client() as client:
            insert_list = []
            for i in range(10):
                id = Id(PHO_RSC_DIR, '__TEST_MAGIC_%d' % randint(0, 1000000))
                rsc = Resource(id=id, model='')
                dev = DevInfo(rsc=rsc, path='/tmp/test_%d' % randint(0,1000000),
                             host='localhost')

                insert_list.append(dev)

            client.devices.insert(insert_list)

            # now retrieve them one by one and check serials
            for dev in insert_list:
                res = client.devices.get(serial=dev.rsc.id.name)
                for retrieved_dev in res:
                    # replace with assertIsInstance when we drop pre-2.7 support
                    self.assertTrue(isinstance(retrieved_dev, dev.__class__))
                    self.assertEqual(retrieved_dev.rsc.id.name,
                                     dev.rsc.id.name)

            client.devices.delete(res)

    def test_add_sqli(self):
        """The input data is sanitized and does not cause an SQL injection."""
        # Not the best place to test SQL escaping, but most convenient one.
        with Client() as client:
            client.media.add(
                PHO_RSC_TAPE, "LTFS", "lto8", "TAPE_SQLI_0'; <sqli>",
            )

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
            name = '/some/path_%d' % randint(0, 1000000)
            client.media.add(PHO_RSC_DIR, 'POSIX', None, name, locked=False)

            # Get the created media from db
            media = client.media.get(id=name)[0]

            # It should not be locked yet
            self.assertFalse(media.is_locked())

            # Lock it in db
            client.media.lock([media])

            # Media cannot be locked twice
            with self.assertRaises(EnvironmentError):
                client.media.lock([media])

            # Retrieve an up-to-date version
            media = client.media.get(id=name)[0]

            # This one should be locked
            self.assertTrue(media.is_locked())

            # Unlock it
            client.media.unlock([media])

            # Unlocking twice works
            client.media.unlock([media])

            # The up-to-date version isn't locked anymore
            media = client.media.get(id=name)[0]
            self.assertFalse(media.is_locked())


if __name__ == '__main__':
    unittest.main(buffer=True)
