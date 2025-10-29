#!/usr/bin/env python3

#
#  all rights reserved (c) 2014-2025 cea/dam.
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

import filecmp
import os
import shutil
from subprocess import Popen, PIPE
import tempfile
import time
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
            # Add --restrict-key option because since PostgreSQL 13.22, the
            # pg_dump command appends a `\restrict key` to the dump file. If the
            # key is not provided, it is generated randomly.
            process = Popen("psql --version | awk '{print $3}'",
                            shell=True, stdout=PIPE)
            out_version, _ = process.communicate()

            if float(out_version) >= 13.22:
                process = Popen(['sudo', '-u', 'postgres', 'pg_dump', '-s',
                                 '-U', 'phobos', f'--restrict-key=key{idx}',
                                 '-f', '/tmp/schema_dump', 'phobos_test'])
            else:
                process = Popen(['sudo', '-u', 'postgres', 'pg_dump', '-s',
                                 '-U', 'phobos', '-f', '/tmp/schema_dump',
                                 'phobos_test'])

            process.wait()
            self.migrator.drop_tables()

            self.migrator.create_schema(ORDERED_SCHEMAS[idx - 1])
            self.migrator.migrate(version)
            if float(out_version) >= 13.22:
                process = Popen(['sudo', '-u', 'postgres', 'pg_dump', '-s',
                                 '-U', 'phobos', f'--restrict-key=key{idx}',
                                 '-f', '/tmp/migrate_dump', 'phobos_test'])
            else:
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

    def exit_put_get_migration(self, success, msg=None):
        """Function to cleanup"""
        Popen("pid=$(pgrep phobosd); kill $pid; while kill -0 $pid; \
               do sleep 1; done", shell=True).wait()
        shutil.rmtree('/run/phobosd')
        os.unlink('oid-file')

        self.migrator.drop_tables()
        if not success:
            self.fail(msg)

    def test_put_get_migration(self):
        """Test doing a get after migration"""
        # Create schema of the 1.91
        self.migrator.create_schema(ORDERED_SCHEMAS[2])
        # We simulate a put by initializing the DSS with all the information
        # inserted by a put with phobos 1.91. We can't just do a put because
        # one version of phobos requires a specific schema version.
        # The test is launched with the latest version and not 1.91.

        # Retrieve only the hostname part before the first '.' as phobos does.
        host = os.uname()[1].split('.')[0]
        size = os.stat('/etc/hosts').st_size

        with tempfile.TemporaryDirectory() as tmpdir:
            self.migrator.execute("""
                INSERT INTO device
                    (family, model, id, host, adm_status, path, lock, lock_ts)
                     VALUES ('dir', NULL, '%s:%s', '%s',
                             'unlocked', '%s', '', 0);

                INSERT INTO extent
                    (oid, uuid, version, state, lyt_info, extents)
                     VALUES ('oid', '04f88176-3923-4fe8-881e-c9a213db0faa', 1,
                           'sync', '{"name": "simple", "major": 0, "minor": 1}',
                           '[{"sz": %d, "fam": "dir",
                              "addr": "08/ad/08ad81f4_oid.s0",
                              "media": "%s"}]');

                INSERT INTO media (family, model, id, adm_status, fs_type,
                                   fs_label, address_type, fs_status, lock,
                                   lock_ts, stats, tags, put, get, delete)
                    VALUES ('dir', NULL, '%s', 'unlocked',
                            'POSIX', '%s', 'HASH1', 'used', '',
                            0, '{"nb_obj": 1, "last_load": 0, "nb_errors": 0,
                                 "logc_spc_used": %d,
                                 "phys_spc_free": 17784107008,
                                 "phys_spc_used": 3679174656}',
                            '[]', 't', 't', 't');

                INSERT INTO object (oid, uuid, version, user_md)
                    VALUES ('oid', '04f88176-3923-4fe8-881e-c9a213db0faa', 1,
                            '{}');
            """ % (host, tmpdir, host, tmpdir, size, tmpdir, tmpdir, tmpdir,
                   size))

            os.makedirs(f'{tmpdir}/08/ad')
            shutil.copyfile('/etc/hosts', f'{tmpdir}/08/ad/08ad81f4_oid.s0')

            with open(f'{tmpdir}/.phobos_dir_label', 'w+') as fd:
                fd.write(f'{tmpdir}')

            # Migrate to the last version
            self.migrator.migrate(ORDERED_SCHEMAS[-1])
            self.assertEqual(self.migrator.schema_version(),
                             ORDERED_SCHEMAS[-1])

            os.mkdir('/run/phobosd')

            Popen("DAEMON_PID_FILEPATH=/run/phobosd/phobosd.pid $phobosd",
                  shell=True, stdout=PIPE)

            # Check that phobosd is running
            nb_try = 0
            process = Popen('$phobos phobosd ping', shell=True, stdout=PIPE)
            result, _ = process.communicate()
            while process.returncode and nb_try < 5:
                time.sleep(0.5)
                nb_try += 1
                process = Popen('$phobos phobosd ping', shell=True, stdout=PIPE)
                result, _ = process.communicate()

            if process.returncode:
                self.exit_put_get_migration(False, "Failed to ping phobosd")

            process = Popen('$phobos get oid oid-file', shell=True, stdout=PIPE)
            result, _ = process.communicate()
            if process.returncode:
                print(result.decode('utf-8'))
                self.exit_put_get_migration(False, "Failed to get 'oid'")

            if not filecmp.cmp('/etc/hosts', 'oid-file'):
                self.exit_put_get_migration(False, "'oid-file' is different "
                                                   "than '/etc/hosts'")

            self.exit_put_get_migration(True)

    def test_2_2_to_3(self):
        """Test migration between 2.2 and 3.0"""
        self.migrator.create_schema("2.2")

        self.migrator.execute("""
            INSERT INTO object(oid, object_uuid, version, lyt_info, obj_status)
                VALUES ('aries', 'dacfaeba-24ef-431b-a7b3-205dc1e8a34a', 2,
                        '{"name": "raid1", "attrs": {"repl_count": "2"},
                          "major": 0, "minor": 2}', 'complete');

            INSERT INTO deprecated_object(oid, object_uuid, version, lyt_info,
                                          obj_status)
                VALUES ('aries', 'dacfaeba-24ef-431b-a7b3-205dc1e8a34b', 1,
                        '{"name": "raid1", "attrs": {"repl_count": "1"},
                          "major": 0, "minor": 2}', 'readable');

            INSERT INTO layout (object_uuid, version, layout_index) VALUES
                ('dacfaeba-24ef-431b-a7b3-205dc1e8a34a', 2, 1),
                ('dacfaeba-24ef-431b-a7b3-205dc1e8a34b', 1, 1)
        """)

        self.migrator.migrate("3.0")
        self.assertEqual(self.migrator.schema_version(), "3.0")
        self.assertEqual(
            self.migrator.execute("""
                SELECT object_uuid, version, copy_name, lyt_info, copy_status
                FROM copy ORDER BY version;""", output=True),
            [
                ('dacfaeba-24ef-431b-a7b3-205dc1e8a34b', 1, 'source',
                 {"name": "raid1", "attrs": {"repl_count": "1"},
                  "major": 0, "minor": 2}, 'readable'),
                ('dacfaeba-24ef-431b-a7b3-205dc1e8a34a', 2, 'source',
                 {"name": "raid1", "attrs": {"repl_count": "2"},
                  "major": 0, "minor": 2}, 'complete')
            ]
        )
        self.assertEqual(
            self.migrator.execute("""
                SELECT object_uuid, version, copy_name FROM layout
                ORDER BY version;
            """, output=True),
            [
                ('dacfaeba-24ef-431b-a7b3-205dc1e8a34b', 1, 'source'),
                ('dacfaeba-24ef-431b-a7b3-205dc1e8a34a', 2, 'source')
            ]
        )
        self.migrator.drop_tables()

    def test_3_to_3_2(self):
        """Test migration between 3.0 and 3.2"""
        self.migrator.create_schema("3.0")

        self.migrator.execute("""
            INSERT INTO object (oid, object_uuid, version)
                VALUES ('blob', '4757e720-651e-4daa-8bb0-45a86b231a34', 2);

            INSERT INTO deprecated_object (oid, object_uuid, version)
                VALUES ('blob', '4757e720-651e-4daa-8bb0-45a86b231a34', 1);

            INSERT INTO copy (object_uuid, version, copy_name, lyt_info)
                VALUES ('4757e720-651e-4daa-8bb0-45a86b231a34', 1, 'source',
                        '{"name": "raid1", "attrs": {"raid1.repl_count": "2"},
                          "major": 0, "minor": 2}'),
                       ('4757e720-651e-4daa-8bb0-45a86b231a34', 2, 'source',
                        '{"name": "raid4", "major": 0, "minor": 1}');

            INSERT INTO layout (object_uuid, version, extent_uuid, layout_index, copy_name) VALUES
                ('4757e720-651e-4daa-8bb0-45a86b231a34', 1, '0780c865-ba1d-4677-b22c-84e7fdfc6925', 0, 'source'),
                ('4757e720-651e-4daa-8bb0-45a86b231a34', 1, 'a27b8867-eda9-411a-9435-74ddd284faa2', 1, 'source'),
                ('4757e720-651e-4daa-8bb0-45a86b231a34', 1, 'c0be5ee0-202b-404f-80d2-ade630910825', 2, 'source'),
                ('4757e720-651e-4daa-8bb0-45a86b231a34', 1, '6a17a9cc-d5db-401a-82d6-220d0113ea34', 3, 'source'),
                ('4757e720-651e-4daa-8bb0-45a86b231a34', 1, 'ed97f05f-c5b7-4bd4-b021-ae3271bd15d1', 4, 'source'),
                ('4757e720-651e-4daa-8bb0-45a86b231a34', 1, '826283ca-289d-42dd-a156-c429ac11181e', 5, 'source'),
                ('4757e720-651e-4daa-8bb0-45a86b231a34', 2, '5c1df00f-38c3-4198-94f3-624143671e48', 0, 'source'),
                ('4757e720-651e-4daa-8bb0-45a86b231a34', 2, 'bc886ddb-6ffb-4d39-8180-f3cd70736847', 1, 'source'),
                ('4757e720-651e-4daa-8bb0-45a86b231a34', 2, '2b7f9624-eabe-4a81-a3a5-13499cb765f4', 2, 'source'),
                ('4757e720-651e-4daa-8bb0-45a86b231a34', 2, '5d184974-700c-4bc5-9528-a720c37b74f5', 3, 'source'),
                ('4757e720-651e-4daa-8bb0-45a86b231a34', 2, '8f0cf40c-a73a-45c0-b14f-b16457e1c9b8', 4, 'source'),
                ('4757e720-651e-4daa-8bb0-45a86b231a34', 2, 'b3daba00-d068-49bc-b35d-668e09afb5d3', 5, 'source');

            INSERT INTO extent (extent_uuid, size, medium_library) VALUES
                ('0780c865-ba1d-4677-b22c-84e7fdfc6925', 931840, 'legacy'),
                ('a27b8867-eda9-411a-9435-74ddd284faa2', 931840, 'legacy'),
                ('c0be5ee0-202b-404f-80d2-ade630910825', 931840, 'legacy'),
                ('6a17a9cc-d5db-401a-82d6-220d0113ea34', 931840, 'legacy'),
                ('ed97f05f-c5b7-4bd4-b021-ae3271bd15d1', 876320, 'legacy'),
                ('826283ca-289d-42dd-a156-c429ac11181e', 876320, 'legacy'),
                ('5c1df00f-38c3-4198-94f3-624143671e48', 931840, 'legacy'),
                ('bc886ddb-6ffb-4d39-8180-f3cd70736847', 931840, 'legacy'),
                ('2b7f9624-eabe-4a81-a3a5-13499cb765f4', 931840, 'legacy'),
                ('5d184974-700c-4bc5-9528-a720c37b74f5', 438160, 'legacy'),
                ('8f0cf40c-a73a-45c0-b14f-b16457e1c9b8', 438160, 'legacy'),
                ('b3daba00-d068-49bc-b35d-668e09afb5d3', 438160, 'legacy')
        """)

        self.migrator.migrate("3.2")
        self.assertEqual(self.migrator.schema_version(), "3.2")
        self.assertEqual(
            self.migrator.execute("""
                SELECT oid, size FROM object;
            """, output=True),
            [
                ('blob', 2740000)
            ]
        )
        self.assertEqual(
            self.migrator.execute("""
                SELECT oid, size FROM deprecated_object;
            """, output=True),
            [
                ('blob', 2740000)
            ]
        )
        self.migrator.drop_tables()

if __name__ == '__main__':
    unittest.main(buffer=True)
