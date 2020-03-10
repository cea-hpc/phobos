#!/usr/bin/env python2

#
#  all rights reserved (c) 2014-2018 cea/dam.
#
#  this file is part of phobos.
#
#  phobos is free software: you can redistribute it and/or modify it under
#  the terms of the gnu lesser general public license as published by
#  the free software foundation, either version 2.1 of the license, or
#  (at your option) any later version.
#
#  phobos is distributed in the hope that it will be useful,
#  but without any warranty; without even the implied warranty of
#  merchantability or fitness for a particular purpose.  see the
#  gnu lesser general public license for more details.
#
#  you should have received a copy of the gnu lesser general public license
#  along with phobos. if not, see <http://www.gnu.org/licenses/>.
#

import unittest

from phobos.db import Migrator, CURRENT_SCHEMA_VERSION

class MigratorTest(unittest.TestCase):
    """Test the Migrator class (schema management)"""

    def setUp(self):
        """Set up self.migrator and drop the current db schema"""
        self.migrator = Migrator()

        # Start fresh
        self.migrator.drop_tables()
        self.assertEqual(self.migrator.schema_version(), "0")

    def test_setup_drop_tables(self):
        """Test setting up and dropping the schema"""
        self.assertEqual(self.migrator.schema_version(), "0")

        # Create various schema versions
        for version in ["1.1", "1.2"]:
            self.migrator.drop_tables()
            self.migrator.create_schema(version)
            self.assertEqual(self.migrator.schema_version(), version)

        with self.assertRaisesRegexp(ValueError, "Unknown schema version: foo"):
            self.migrator.create_schema("foo")

    def test_migration(self):
        """Test migrations between schemas"""
        # Test migration from every valid versions
        for version in ["1.1", "1.2"]:
            self.migrator.create_schema(version)
            self.migrator.migrate()
            self.assertEqual(
                self.migrator.schema_version(), CURRENT_SCHEMA_VERSION,
            )
            self.migrator.drop_tables()

        # Migrate from 0 to latest
        self.assertEqual(self.migrator.schema_version(), "0")
        self.migrator.migrate()
        self.assertEqual(self.migrator.schema_version(), CURRENT_SCHEMA_VERSION)

        # Unreachable schema version (current is 1.2)
        with self.assertRaisesRegexp(ValueError, "Don't know how to migrate"):
            self.migrator.migrate("1.1")

if __name__ == '__main__':
    unittest.main(buffer=True)
