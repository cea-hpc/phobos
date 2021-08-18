#!/usr/bin/env python3

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

import unittest
import os

from random import randint

from phobos.core.dss import Client
from phobos.core.ffi import MediaInfo
from phobos.core.const import PHO_RSC_DIR, PHO_RSC_TAPE, rsc_family2str # pylint: disable=no-name-in-module


class DSSClientTest(unittest.TestCase):
    """
    This test case issue requests to the DSS to stress the python bindings.
    """

    def test_client_connect(self): # pylint: disable=no-self-use
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
            media = client.media.get()
            self.assertEqual(media[0].name, media[-len(media)].name)
            self.assertEqual(media[len(media) - 1].name, media[-1].name)

    def test_list_media_by_tags(self):
        """List media with tags."""
        with Client() as client:
            blank_medium = MediaInfo(family=PHO_RSC_DIR, name='m0', model=None)
            client.media.add(blank_medium, 'POSIX')
            blank_medium.name = 'm1'
            client.media.add(blank_medium, 'POSIX', tags=['foo'])
            blank_medium.name = 'm2'
            client.media.add(blank_medium, 'POSIX', tags=['foo', 'bar'])
            blank_medium.name = 'm3'
            client.media.add(blank_medium, 'POSIX', tags=['goo', 'foo'])
            blank_medium.name = 'm4'
            client.media.add(blank_medium, 'POSIX', tags=['bar', 'goo'])
            blank_medium.name = 'm5'
            client.media.add(blank_medium, 'POSIX', tags=['foo', 'bar', 'goo'])
            n_tags = {'foo': 4, 'bar': 3, 'goo': 3}
            n_bar_foo = 2

            for tag, n_tag in n_tags.items():
                num = 0
                for medium in client.media.get(tags=tag):
                    self.assertTrue(tag in medium.tags)
                    num += 1
                self.assertEqual(num, n_tag)

            num = 0
            for medium in client.media.get(tags=['bar', 'foo']):
                self.assertTrue('bar' in medium.tags)
                self.assertTrue('foo' in medium.tags)
                num += 1
            self.assertEqual(num, n_bar_foo)

    def test_getset(self):
        """GET / SET an object to validate the whole chain."""
        with Client() as client:
            insert_list = []
            for _ in range(10):
                medium = MediaInfo(name='/tmp/test_%d' % randint(0, 1000000),
                                   family=PHO_RSC_DIR, model=None,
                                   is_adm_locked=False)

                insert_list.append(medium)

            client.media.insert(insert_list)

            # now retrieve them one by one and check serials
            for medium in insert_list:
                res = client.media.get(id=medium.name)
                for retrieved_med in res:
                    # replace with assertIsInstance when we drop pre-2.7 support
                    self.assertTrue(isinstance(retrieved_med, medium.__class__))
                    self.assertEqual(retrieved_med.name, medium.name)

            client.media.delete(res)

    def test_add_sqli(self): # pylint: disable=no-self-use
        """The input data is sanitized and does not cause an SQL injection."""
        # Not the best place to test SQL escaping, but most convenient one.
        with Client() as client:
            medium = MediaInfo(family=PHO_RSC_TAPE, name="TAPE_SQLI_0'; <sqli>",
                               model="lto8")
            client.media.add(medium, "LTFS")

    def test_manipulate_empty(self): # pylint: disable=no-self-use
        """SET/DEL empty and None objects."""
        with Client() as client:
            client.media.insert([])
            client.media.insert(None)
            client.media.delete([])
            client.media.delete(None)

    def test_media_lock_unlock(self):
        """Test media lock and unlock wrappers"""
        with Client() as client:
            # Create a dummy media in db
            medium = MediaInfo(name='/some/path_%d' % randint(0, 1000000),
                               family=PHO_RSC_DIR, model=None,
                               is_adm_locked=False)
            client.media.add(medium, 'POSIX')

            # Get the created media from db
            media = client.media.get(id=medium.name)[0]

            # It should not be locked yet
            self.assertFalse(media.is_locked())

            # Lock it in db
            client.media.lock([media])

            # Media cannot be locked twice
            with self.assertRaises(EnvironmentError):
                client.media.lock([media])

            # Retrieve an up-to-date version
            media = client.media.get(id=medium.name)[0]

            # This one should be locked
            self.assertTrue(media.is_locked())

            # Unlock it
            client.media.unlock([media])

            # The up-to-date version isn't locked anymore
            media = client.media.get(id=medium.name)[0]
            self.assertFalse(media.is_locked())


if __name__ == '__main__':
    unittest.main(buffer=True)
