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

from phobos.db import Migrator, ORDERED_SCHEMAS, CURRENT_SCHEMA_VERSION, \
                      AVAIL_SCHEMAS, FUTURE_SCHEMAS

class MigratorTest(unittest.TestCase):
    """Test the Migrator class (schema management)"""

    def setUp(self):
        """Set up self.migrator and drop the current db schema"""
        self.migrator = Migrator()

        # Start fresh
        self.migrator.drop_tables()
        self.assertEqual(self.migrator.schema_version(), "0")

        # From 2.1, the new 'library' column needs a default value from conf
        os.environ["PHOBOS_STORE_default_tape_library"] = "legacy"
        os.environ["PHOBOS_STORE_default_dir_library"] = "legacy"
        os.environ["PHOBOS_STORE_default_rados_library"] = "legacy"
        # From 2.3, the new copy_name column needs a default value from conf
        os.environ["PHOBOS_COPY_default_copy_name"] = "source"

    def test_setup_drop_tables(self):
        """Test setting up and dropping the schema"""
        self.assertEqual(self.migrator.schema_version(), "0")

        # Create various schema versions
        for version in AVAIL_SCHEMAS:
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
        """
        Check 2 databases that implement schema N, with one created from
        scratch, and the second from migration of a database implementing schema
        N-1, are equal
        """
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
                          "schema creation for version " + version)

    def test_future_migrations(self):
        """Test migrations between current and future versions"""
        for version in FUTURE_SCHEMAS:
            self.migrator.create_schema(CURRENT_SCHEMA_VERSION)
            self.migrator.migrate(version)
            self.assertEqual(
                self.migrator.schema_version(), version
            )
            self.migrator.drop_tables()

    def test_1_95_to_2_0(self):
        """
        Test migration between 1.95 and 2.0, checking object and extent
        information are correctly scattered
        """
        self.migrator.create_schema("1.95")

        self.migrator.execute("""
            INSERT INTO object(oid, uuid, user_md)
                VALUES ('aries', 'dacfaeba-24ef-431b-a7b3-205dc1e8a34a',
                        '{"test": "42"}');

            INSERT INTO extent(oid, uuid, version, state, lyt_info, extents)
                VALUES ('aries', 'dacfaeba-24ef-431b-a7b3-205dc1e8a34a', 1,
                        'sync',
                        '{"name": "raid1", "attrs": {"repl_count": "2"},
                          "major": 0, "minor": 2}',
                        '[{"sz": 42, "fam": "dir", "md5": "babecafe",
                           "addr": "ab/cd/part1", "media": "dir1"},
                          {"sz": 43, "fam": "dir", "md5": "babecaff",
                           "addr": "ef/01/part2", "media": "dir2"}]'
                );
        """)

        self.migrator.migrate("2.0")
        self.assertEqual(self.migrator.schema_version(), "2.0")
        self.assertEqual(
            self.migrator.execute("SELECT * FROM object;", output=True),
            [(
                'aries', {'test': '42'},
                'dacfaeba-24ef-431b-a7b3-205dc1e8a34a', 1,
                {"name": "raid1", "attrs": {"repl_count": "2"},
                 "major": 0, "minor": 2}, 'complete'
            )]
        )
        ext_uuids = self.migrator.execute(
            "SELECT extent_uuid FROM extent ORDER BY size;",
            output=True
        )
        self.assertEqual(
            self.migrator.execute("""
                SELECT extent_uuid, state, size, medium_family, medium_id,
                    address, hash->>'md5'
                FROM extent ORDER BY size;
            """, output=True),
            [
                (ext_uuids[0][0], 'sync', 42, 'dir', 'dir1', 'ab/cd/part1',
                 'babecafe'),
                (ext_uuids[1][0], 'sync', 43, 'dir', 'dir2', 'ef/01/part2',
                 'babecaff')
            ]
        )
        self.assertEqual(
            self.migrator.execute("SELECT * FROM layout;", output=True),
            [
                ('dacfaeba-24ef-431b-a7b3-205dc1e8a34a', 1, ext_uuids[0][0], 0),
                ('dacfaeba-24ef-431b-a7b3-205dc1e8a34a', 1, ext_uuids[1][0], 1)
            ]
        )
        self.migrator.drop_tables()

if __name__ == '__main__':
    unittest.main(buffer=True)
