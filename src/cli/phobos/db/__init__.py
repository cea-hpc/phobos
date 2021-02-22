#
#  All rights reserved (c) 2014-2018 CEA/DAM.
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

"""
Database module helpers
"""

from contextlib import contextmanager
from distutils.version import LooseVersion
import json
import logging
import os
import psycopg2

from phobos.core import cfg

ORDERED_SCHEMAS = ["1.1", "1.2", "1.91"]
CURRENT_SCHEMA_VERSION = ORDERED_SCHEMAS[-1]
AVAIL_SCHEMAS = set(ORDERED_SCHEMAS)

LOGGER = logging.getLogger(__name__)

def get_sql_script(schema_version, script_name):
    """Return the content of a script for a given schema version"""
    if schema_version not in AVAIL_SCHEMAS:
        raise ValueError("Unknown schema version: %s" % (schema_version,))
    this_dir = os.path.dirname(__file__)
    script_path = os.path.join(this_dir, "sql", schema_version, script_name)
    with open(script_path) as script_file:
        return script_file.read()


class Migrator:
    """StrToInt JSONB conversion engine"""
    def __init__(self, conn=None):
        self.conn = None
        # Externally provided connection
        self.ext_conn = conn

        # { source_version: (target_version, migration_function) }
        self.convert_funcs = {
            "0": ("1.91", lambda: self.create_schema("1.91")),
            "1.1": ("1.2", self.convert_1_to_2),
            "1.2": ("1.91", self.convert_2_to_3),
        }

        self.reachable_versions = set(
            version for version, _ in self.convert_funcs.values()
        )

    @contextmanager
    def connect(self):
        """Connect to the database"""
        try:
            if self.ext_conn:
                self.conn = self.ext_conn
                yield self.ext_conn
            else:
                conn_str = cfg.get_val("dss", "connect_string")
                with psycopg2.connect(conn_str) as conn:
                    self.conn = conn
                    yield conn
        finally:
            self.conn = None

    def create_schema(self, schema_version=CURRENT_SCHEMA_VERSION):
        """Setup all phobos tables and types"""
        schema_script = get_sql_script(schema_version, "schema.sql")
        with self.connect(), self.conn.cursor() as cursor:
            cursor.execute(schema_script)

    def drop_tables(self):
        """Drop all phobos tables and types"""
        schema_version = self.schema_version()
        if schema_version == "0":
            return
        drop_schema_script = get_sql_script(schema_version, "drop_schema.sql")
        with self.connect(), self.conn.cursor() as cursor:
            cursor.execute(drop_schema_script)

    def convert_schema_1_to_2(self):
        """
        DB schema changes:
        - device.model : from enum to varchar(32)
        - media.model : from enum top varchar(32)
        """
        cur = self.conn.cursor()
        #Alter model from enum to varchar(32) and drop old enum types
        cur.execute("""
            -- increase device id size from 32 to 255
            ALTER TABLE device ALTER id TYPE varchar(255);

            -- increase media id size from 32 to 255
            ALTER TABLE media ALTER id TYPE varchar(255);

            -- device model from enum to varchar(32)
            ALTER TABLE device ALTER model TYPE varchar(32);

            -- media model from enum to varchar(32)
            ALTER TABLE media ALTER model TYPE varchar(32);

            -- fs_label can be NULL
            ALTER TABLE media ALTER fs_label DROP NOT NULL;

            -- therefore, fs_label has special no default value
            ALTER TABLE media ALTER fs_label DROP DEFAULT;

            -- drop old dev_model type.
            DROP TYPE dev_model;

            -- drop old media_model type.
            DROP TYPE tape_model;

            -- add the media.tags field
            ALTER TABLE media ADD COLUMN tags JSONB;

            -- create index for faster tag filtering
            CREATE INDEX ON media USING gin(tags);

            -- create schema_info table for version tracking
            CREATE TABLE schema_info (
                version varchar(32) PRIMARY KEY
            );

            -- Insert current schema version
            INSERT INTO schema_info VALUES ('1.2');
        """)
        self.conn.commit()
        cur.close()

    def convert_1_to_2(self):
        """Convert DB from model 1 (phobos v1.1) to 2 (phobos v1.2)"""
        with self.connect():
            self.convert_schema_1_to_2()

    def convert_schema_2_to_3(self):
        """
        DB schema changes:
        - add uuid column into object table
        - add version column into object table
        - create deprecated_object table: as object table, adding deprec_time
        """
        cur = self.conn.cursor()
        cur.execute("""
            -- add uuid to object table and extent table
            ALTER TABLE object ADD uuid varchar(36) UNIQUE
                DEFAULT uuid_generate_v4();
            ALTER TABLE extent ADD uuid varchar(36) UNIQUE;

            -- copy uuid value from object table into extent table
            UPDATE extent SET uuid = object.uuid from object
                where extent.oid = object.oid;

            -- add version to object table and extent table
            ALTER TABLE object ADD version integer DEFAULT 1 NOT NULL;
            ALTER TABLE extent ADD version integer DEFAULT 1 NOT NULL;

            -- move primary key of extent table from (oid) to (uuid, version)
            ALTER TABLE extent DROP CONSTRAINT extent_pkey;
            ALTER TABLE extent ADD PRIMARY KEY (uuid, version);

            -- create deprecated_object table as object table with deprec_time
            CREATE TABLE deprecated_object (
                oid         varchar(1024),
                uuid        varchar(36),
                version     integer DEFAULT 1 NOT NULL,
                user_md     jsonb,
                deprec_time timestamp DEFAULT now(),
                PRIMARY KEY (uuid, version)
            );

            -- add media operation flags
            ALTER TABLE media ADD put boolean DEFAULT TRUE;
            ALTER TABLE media ADD get boolean DEFAULT TRUE;
            ALTER TABLE media ADD delete boolean DEFAULT TRUE;

            -- Update current schema version
            UPDATE schema_info SET version = '1.91';
        """)
        self.conn.commit()
        cur.close()

    def convert_2_to_3(self):
        """Convert DB from model 2 (phobos v1.2) to 3 (phobos v1.91)"""
        with self.connect():
            self.convert_schema_2_to_3()

    def migrate(self, target_version=None):
        """Convert DB schema up to a given phobos version"""
        target_version = target_version if target_version is not None \
                                        else CURRENT_SCHEMA_VERSION

        # Check that the target version can be reached before migrating
        if target_version not in self.reachable_versions:
            raise ValueError(
                "Don't know how to migrate to version %s" % (target_version,)
            )

        # Check that the current version is anterior to the target version
        current_version = self.schema_version()
        if LooseVersion(current_version) > LooseVersion(target_version):
            raise ValueError(
                "Cannot migrate to an older version (current version: %s, "
                "target version: %s)"
                % (current_version, target_version)
            )

        # Perform successive migrations. To enforce consistency, the new
        # current_version is retrieved from database at each step
        while current_version != target_version:
            try:
                next_version, converter = self.convert_funcs[current_version]
            except KeyError:
                raise RuntimeError(
                    "Inconsistent migration table, cannot migrate from %s"
                    % (current_version)
                ) from KeyError
            LOGGER.info(
                "Migrating from %s to %s", current_version, next_version
            )
            converter()
            current_version = self.schema_version()
            if current_version != next_version:
                raise RuntimeError(
                    "Migration to version %s unexpectedly led to version %s"
                    % (next_version, current_version)
                )

    def schema_version(self):
        """Return the current version of the database schema"""
        # Be optimistic and attempt to retrieve version from schema_info table
        with self.connect(), self.conn.cursor() as cursor:
            # Is there a "schema_info" table?
            cursor.execute("""
                SELECT 1
                FROM information_schema.tables
                WHERE table_name='schema_info'
            """)

            # Schema info present, use it
            if cursor.fetchone() is not None:
                cursor.execute("SELECT version FROM schema_info")
                versions = cursor.fetchall()

                # Check for inconsistencies in database
                if len(versions) > 1:
                    LOGGER.warning(
                        "Invalid db state: multiple versions are specified"
                    )

                # Table created but not value: this state is invalid
                if not versions:
                    raise RuntimeError(
                        "Invalid db state: schema_info table present but empty"
                    )

                # In case of multiple detected versions, take the latest
                versions = [row[0] for row in versions]
                return max(versions, key=lambda v: LooseVersion)

            # No schema_info table, if the "media" table does not exist either,
            # the schema has not been initialized
            cursor.execute("""
                SELECT 1
                FROM information_schema.tables
                WHERE table_name='media'
            """)
            if cursor.fetchone() is None:
                return "0"

            # Otherwise: 1.1
            return "1.1"
