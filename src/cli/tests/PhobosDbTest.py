#!/usr/bin/env python3

#
#  all rights reserved (c) 2014-2022 cea/dam.
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

"""
Unit tests for database migration module
"""

import os
from subprocess import Popen, PIPE
import unittest

from phobos.db import Migrator, ORDERED_SCHEMAS, CURRENT_SCHEMA_VERSION

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
        for version in ORDERED_SCHEMAS:
            self.migrator.drop_tables()
            self.migrator.create_schema(version)
            self.assertEqual(self.migrator.schema_version(), version)

        with self.assertRaisesRegex(ValueError, "Unknown schema version: foo"):
            self.migrator.create_schema("foo")

    def test_migration(self):
        """Test migrations between schemas"""
        # Test migration from every valid versions
        for version in ORDERED_SCHEMAS:
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

        # Unreachable schema version (current is CURRENT_SCHEMA_VERSION)
        with self.assertRaisesRegex(ValueError, "Don't know how to migrate"):
            self.migrator.migrate(ORDERED_SCHEMAS[0])

        with self.assertRaisesRegex(
            ValueError,
            "Cannot migrate to an older version"
        ):
            self.migrator.migrate(ORDERED_SCHEMAS[-2])

    def test_migration_integrity(self):
        for idx, version in enumerate(ORDERED_SCHEMAS):
            if idx == 0:
                continue
            self.migrator.create_schema(version)
            process = Popen(['sudo', '-u', 'postgres', 'pg_dump', '-s',
                             '-U', 'phobos', '-f', '/tmp/schema_dump',
                             'phobos_test'])
            process.wait()
            self.migrator.drop_tables()

            self.migrator.create_schema(ORDERED_SCHEMAS[idx - 1])
            self.migrator.migrate(version)
            process = Popen(['sudo', '-u', 'postgres', 'pg_dump', '-s',
                             '-U', 'phobos', '-f', '/tmp/migrate_dump',
                             'phobos_test'])
            process.wait()
            self.migrator.drop_tables()

            process = Popen(['diff', '/tmp/schema_dump', '/tmp/migrate_dump'],
                            stdout=PIPE)
            out_diff, _ = process.communicate()

            os.unlink("/tmp/schema_dump")
            os.unlink("/tmp/migrate_dump")

            if process.returncode:
                print(out_diff.decode('utf-8'))
                self.fail("DB is different between after a migrate and a new " +
                     "schema creation")

if __name__ == '__main__':
    unittest.main(buffer=True)
