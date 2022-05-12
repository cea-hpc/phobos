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

""" RADOS Unit tests for phobos.cli These should be executed only if RADOS
    modules are installed """

import unittest
import errno
import rados
import PhobosCLITest
from phobos.core.dss import Client

def pho_rados_pool_add(poolname, cluster):
    """ Add a RADOS pool to the Ceph cluster if it does not already exist"""
    if not cluster.pool_exists(poolname):
        cluster.create_pool(poolname)

def pho_rados_pool_remove(poolname, cluster):
    """ Removing a RADOS pool from the Ceph cluster if it exists """
    if cluster.pool_exists(poolname):
        cluster.delete_pool(poolname)

class RadosPoolAddTest(PhobosCLITest.BasicExecutionTest):
    """
        This sub-test suite adds RADOS pools and makes sure that both regular
        and abnormal cases are handled properly.
    """
    def setUp(self):
        """Set up self.cluster and self.dss"""
        self.cluster = rados.Rados(conffile='/etc/ceph/ceph.conf')
        self.dss = Client()

        self.cluster.connect()
        self.dss.connect()

    def tearDown(self):
        """Disconnect self.cluster and self.dss"""
        self.dss.disconnect()
        self.cluster.shutdown()

    def test_rados_pool_add(self):
        """Test adding RADOS pools. Simple case."""
        # Pool do not have specific names
        pho_rados_pool_add('pho_pool_add', self.cluster)
        pho_rados_pool_add('pho_pool_add1', self.cluster)
        pho_rados_pool_add('pho_pool_add2', self.cluster)

        self.pho_execute(['rados_pool', 'add', 'pho_pool_add'])
        self.pho_execute(['rados_pool', 'add', 'pho_pool_add1',
                          'pho_pool_add2'])

        media_list = [medium.name for medium in  self.dss.media.get()]
        self.assertTrue(len(media_list) == 3)
        self.assertIn("pho_pool_add", media_list)
        self.assertIn("pho_pool_add1", media_list)
        self.assertIn("pho_pool_add2", media_list)

        device_list = [device.name for device in  self.dss.devices.get()]
        self.assertTrue(len(device_list) == 3)
        self.assertIn(PhobosCLITest.gethostname_short() + ":pho_pool_add",
                      device_list)
        self.assertIn(PhobosCLITest.gethostname_short() + ":pho_pool_add1",
                      device_list)
        self.assertIn(PhobosCLITest.gethostname_short() + ":pho_pool_add2",
                      device_list)

        pho_rados_pool_remove('pho_pool_add', self.cluster)
        pho_rados_pool_remove('pho_pool_add1', self.cluster)
        pho_rados_pool_remove('pho_pool_add2', self.cluster)

    def test_rados_pool_list(self):
        """ List RADOS pools"""
        pho_rados_pool_add('pho_pool_list', self.cluster)

        self.pho_execute(['rados_pool', 'add', 'pho_pool_list'])

        output, _ = self.pho_execute_capture(['rados_pool', 'list'])
        self.assertTrue(output.count("pho_pool_list") == 1)

        path = "%s:%s" % (PhobosCLITest.gethostname_short(), 'pho_pool_list')
        self.pho_execute(['-v', 'rados_pool', 'list', '-o', 'all', path])

        pho_rados_pool_remove('pho_pool_list', self.cluster)

    def test_rados_pool_lock_unlock(self):
        """ Test lock/unlock on RADOS pools """
        pho_rados_pool_add('pho_pool_lock_unlock', self.cluster)

        self.pho_execute(['rados_pool', 'add', 'pho_pool_lock_unlock'])

        self.pho_execute(['rados_pool', 'lock', 'pho_pool_lock_unlock'])
        output, _ = self.pho_execute_capture(['rados_pool', 'list', '-o', 'all',
                                              'pho_pool_lock_unlock'])
        self.assertIn("locked", output)

        self.pho_execute(['rados_pool', 'unlock', 'pho_pool_lock_unlock'])
        output, _ = self.pho_execute_capture(['rados_pool', 'list', '-o', 'all',
                                              'pho_pool_lock_unlock'])
        self.assertIn("unlocked", output)

        pho_rados_pool_remove('pho_pool_lock_unlock', self.cluster)

    def test_rados_pool_tags(self):
        """Test adding a RADOS pool with tags."""
        pho_rados_pool_add('pho_pool_tags', self.cluster)

        self.pho_execute(['rados_pool', 'add', 'pho_pool_tags', '--tags',
                          'tag-foo,tag-bar'])
        output, _ = self.pho_execute_capture(['rados_pool', 'list', '-o', 'all',
                                              'pho_pool_tags'])
        self.assertIn("['tag-foo', 'tag-bar']", output)

        pho_rados_pool_remove('pho_pool_tags', self.cluster)

    def test_rados_pool_update(self):
        """Test updating a RADOS pool."""
        pho_rados_pool_add('pho_pool_update', self.cluster)

        self.pho_execute(['rados_pool', 'add', 'pho_pool_update', '--tags',
                          'tag-baz'])

        # Check inserted media
        output, _ = self.pho_execute_capture(['rados_pool', 'list', '--output',
                                              'tags', 'pho_pool_update'])
        self.assertEqual(output.strip(), "['tag-baz']")

        # Update media
        self.pho_execute(['rados_pool', 'update', '-T', '', 'pho_pool_update'])

        # Check updated media
        output, _ = self.pho_execute_capture(['rados_pool', 'list', '--output',
                                              'tags', 'pho_pool_update'])
        self.assertEqual(output.strip(), "[]")

        pho_rados_pool_remove('pho_pool_update', self.cluster)


    def test_rados_pool_add_missing(self):
        """Adding a non-existent RADOS pool should raise an error."""
        pho_rados_pool_remove('pho_pool_add_missing', self.cluster)
        self.pho_execute(['rados_pool', 'add', 'pho_pool_add_missing'],
                         code=errno.ENODEV)

    def test_rados_pool_add_double(self):
        """Adding a RADOS pool twice should raise an error."""
        pho_rados_pool_add('pho_pool_double', self.cluster)
        self.pho_execute(['-v', 'rados_pool', 'add', "pho_pool_double"])
        self.pho_execute(['-v', 'rados_pool', 'add', "pho_pool_double"],
                         code=errno.EEXIST)
        pho_rados_pool_remove('pho_pool_double', self.cluster)

    def test_rados_pool_add_correct_and_missing(self): # pylint: disable=invalid-name
        """
        Adding existing and a non-existent RADOS pools should add the correct
        pools and raise an error because of the missing one.
        """
        pho_rados_pool_add('pho_pool_correct1', self.cluster)
        pho_rados_pool_add('pho_pool_correct2', self.cluster)
        pho_rados_pool_remove('pho_pool_invalid', self.cluster)

        self.pho_execute(['-v', 'rados_pool', 'add', 'pho_pool_correct1',
                          'pho_pool_invalid', 'pho_pool_correct2'],
                         code=errno.ENODEV)

        output, _ = self.pho_execute_capture(['rados_pool', 'list'])
        self.assertIn('pho_pool_correct1', output)
        self.assertNotIn('pho_pool_invalid', output)
        self.assertIn('pho_pool_correct2', output)

        pho_rados_pool_remove('pho_pool_correct1', self.cluster)
        pho_rados_pool_remove('pho_pool_correct2', self.cluster)

if __name__ == '__main__':
    unittest.main(buffer=True)
