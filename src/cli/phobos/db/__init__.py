#
#  All rights reserved (c) 2014-2025 CEA/DAM.
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
from phobos.core.ffi import (LIBPHOBOS, ResourceFamily)

ORDERED_SCHEMAS = [
    "1.1", "1.2", "1.91", "1.92", "1.93", "1.95",
    "2.0", "2.1", "2.2", "3.0", "3.2",
]

FUTURE_SCHEMAS = []
CURRENT_SCHEMA_VERSION = ORDERED_SCHEMAS[-1]
AVAIL_SCHEMAS = set(ORDERED_SCHEMAS) | set(FUTURE_SCHEMAS)

LOGGER = logging.getLogger(__name__)

def get_sql_script(schema_version, script_name):
    """Return the content of a script for a given schema version"""
    if schema_version not in AVAIL_SCHEMAS:
        raise ValueError("Unknown schema version: %s" % (schema_version,))
    this_dir = os.path.dirname(__file__)
    script_path = os.path.join(this_dir, "sql", schema_version, script_name)
    with open(script_path) as script_file:
        return script_file.read()

def size_update_3_0_to_3_2(object_table): # pylint: disable=line-too-long
    """Return the size update SQL query for the given object_table"""
    return f"""
        WITH first_copy AS (
            SELECT DISTINCT ON (object_uuid, version)
                object_uuid,
                version,
                copy_name,
                lyt_info
            FROM copy
            ORDER BY object_uuid, version, copy_status DESC, copy_name
        ),
        object_sizes AS (
            SELECT
                o.object_uuid,
                o.version,
                SUM(e.size) AS total_size
            FROM {object_table} o
            INNER JOIN first_copy fc ON o.object_uuid = fc.object_uuid
                AND o.version = fc.version
            INNER JOIN layout l ON fc.object_uuid = l.object_uuid
                AND fc.version = l.version
                AND fc.copy_name = l.copy_name
                AND (
                    ((fc.lyt_info->>'name') = 'raid1'
                     AND l.layout_index % (fc.lyt_info->'attrs'->>'raid1.repl_count')::integer = 0)
                    OR
                    ((fc.lyt_info->>'name') = 'raid4'
                     AND l.layout_index % 3 != 2)
                )
            INNER JOIN extent e ON l.extent_uuid = e.extent_uuid
            GROUP BY o.object_uuid, o.version
        )
        UPDATE {object_table} o
        SET size = os.total_size
        FROM object_sizes os
        WHERE o.object_uuid = os.object_uuid
            AND o.version = os.version;
    """


class Migrator: # pylint: disable=too-many-public-methods
    """StrToInt JSONB conversion engine"""
    def __init__(self, conn=None):
        self.conn = None
        # Externally provided connection
        self.ext_conn = conn

        # { source_version: (target_version, migration_function) }
        self.convert_funcs = {
            "0": (ORDERED_SCHEMAS[-1],
                  lambda: self.create_schema(ORDERED_SCHEMAS[-1])),
            "1.1": ("1.2", self.convert_1_to_2),
            "1.2": ("1.91", self.convert_2_to_3),
            "1.91": ("1.92", self.convert_3_to_4),
            "1.92": ("1.93", self.convert_4_to_5),
            "1.93": ("1.95", self.convert_1_93_to_1_95),
            "1.95": ("2.0", self.convert_1_95_to_2_0),
            "2.0": ("2.1", self.convert_2_0_to_2_1),
            "2.1": ("2.2", self.convert_2_1_to_2_2),
            "2.2": ("3.0", self.convert_2_2_to_3),
            "3.0": ("3.2", self.convert_3_0_to_3_2),
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

    def execute(self, command, output=False): # pylint: disable=inconsistent-return-statements
        """Execute a command on the database, can return the command output"""
        with self.connect(), self.conn.cursor() as cursor:
            cursor.execute(command)
            if output:
                return cursor.fetchall()

    def create_schema(self, schema_version=CURRENT_SCHEMA_VERSION):
        """Setup all phobos tables and types"""
        schema_script = get_sql_script(schema_version, "schema.sql")
        self.execute(schema_script)

    def drop_tables(self):
        """Drop all phobos tables and types"""
        schema_version = self.schema_version()
        if schema_version == "0":
            return
        drop_schema_script = get_sql_script(schema_version, "drop_schema.sql")
        self.execute(drop_schema_script)

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
            CREATE EXTENSION IF NOT EXISTS "uuid-ossp";
            ALTER TABLE object ADD uuid varchar(36) UNIQUE
                DEFAULT uuid_generate_v4();
            ALTER TABLE extent ADD uuid varchar(36);

            -- copy uuid value from object table into extent table
            UPDATE extent SET uuid = object.uuid from object
                where extent.oid = object.oid;

            -- add version to object table and extent table
            ALTER TABLE object ADD version integer DEFAULT 1 NOT NULL;
            ALTER TABLE extent ADD version integer DEFAULT 1 NOT NULL;

            -- move primary key of extent table from (oid) to (uuid, version)
            ALTER TABLE extent DROP CONSTRAINT extent_pkey;
            ALTER TABLE extent ALTER COLUMN oid DROP NOT NULL;
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

    def convert_schema_3_to_4(self):
        """DB schema changes : create lock table"""
        cur = self.conn.cursor()
        cur.execute("""
            -- create lock table
            CREATE TABLE lock (
                id          varchar(2048),
                owner       varchar(256) NOT NULL,
                timestamp   timestamp DEFAULT now(),

                PRIMARY KEY (id)
            );

            -- remove old device locks
            ALTER TABLE device
            DROP COLUMN lock,
            DROP COLUMN lock_ts;

            -- remove old media locks
            ALTER TABLE media
            DROP COLUMN lock,
            DROP COLUMN lock_ts;

            -- update current schema version
            UPDATE schema_info SET version = '1.92';
        """)
        self.conn.commit()
        cur.close()

    def convert_3_to_4(self):
        """Convert DB from model 3 (phobos v1.91) to 4 (phobos v1.92)"""
        with self.connect():
            self.convert_schema_3_to_4()

    def convert_schema_4_to_5(self):
        """DB schema changes : create lock table"""
        cur = self.conn.cursor()
        cur.execute("""
            -- create enum lock_type
            CREATE TYPE lock_type AS ENUM('object', 'device', 'media',
                                          'media_update');

            -- drop old lock table
            DROP TABLE lock;

            -- create lock table
            CREATE TABLE lock (
                type        lock_type,
                id          varchar(2048),
                hostname    varchar(256) NOT NULL,
                owner       integer NOT NULL,
                timestamp   timestamp DEFAULT now(),

                PRIMARY KEY (type, id)
            );

            -- convert simple extents to raid1
            UPDATE extent
                SET lyt_info = json_build_object(
                                'name', 'raid1',
                                'attrs', json_build_object('raid1.repl_count', '1'),
                                'major', 0,
                                'minor', 2
                               )::jsonb
                WHERE lyt_info ->> 'name' = 'simple';

            -- new dev_family type with the 'rados_pool' value
            ALTER TYPE dev_family RENAME TO old_dev_family;
            CREATE TYPE dev_family AS ENUM ('disk', 'tape', 'dir',
                                            'rados_pool');

            -- use new type in device table
            ALTER TABLE device ALTER COLUMN family SET DATA TYPE dev_family
                USING family::text::dev_family;

            -- use new type in media table
            ALTER TABLE media ALTER COLUMN family SET DATA TYPE dev_family
                USING family::text::dev_family;

            -- delete old_dev_family type
            DROP TYPE old_dev_family;

            -- new fs_type type with the 'RADOS' value
            ALTER TYPE fs_type RENAME TO old_fs_type;
            CREATE TYPE fs_type AS ENUM ('POSIX', 'LTFS', 'RADOS');

            -- use new type in media table
            ALTER TABLE media ALTER COLUMN fs_type SET DATA TYPE fs_type
                USING fs_type::text::fs_type;

            -- delete old_fs_type type
            DROP TYPE old_fs_type;

            -- update current schema version
            UPDATE schema_info SET version = '1.93';
        """)
        self.conn.commit()
        cur.close()

    def convert_4_to_5(self):
        """Convert DB from model 4 (phobos v1.92) to 5 (phobos v1.93)"""
        with self.connect():
            self.convert_schema_4_to_5()

    def convert_schema_1_93_to_1_95(self):
        """DB schema changes : create logs table, remove disk from dev_family"""
        cur = self.conn.cursor()
        cur.execute("""
            -- create enum operation_type
            CREATE TYPE operation_type AS ENUM ('Library scan', 'Library open',
                                                'Device lookup',
                                                'Medium lookup',
                                                'Device load', 'Device unload');

            -- create logs table
            CREATE TABLE logs(
                family    dev_family,
                device    varchar(2048),
                medium    varchar(2048),
                uuid      varchar(36) UNIQUE DEFAULT uuid_generate_v4(),
                errno     integer NOT NULL,
                cause     operation_type,
                message   jsonb,
                time      timestamp DEFAULT now(),

                PRIMARY KEY (uuid)
            );

            -- new dev_family type without the 'disk' value
            ALTER TYPE dev_family RENAME TO old_dev_family;
            CREATE TYPE dev_family AS ENUM ('tape', 'dir', 'rados_pool');

            -- use new type in device table
            ALTER TABLE device ALTER COLUMN family SET DATA TYPE dev_family
                USING family::text::dev_family;

            -- use new type in media table
            ALTER TABLE media ALTER COLUMN family SET DATA TYPE dev_family
                USING family::text::dev_family;

            -- use new type in logs table
            ALTER TABLE logs ALTER COLUMN family SET DATA TYPE dev_family
                USING family::text::dev_family;

            -- delete old_dev_family type
            DROP TYPE old_dev_family;

            -- update current schema version
            UPDATE schema_info SET version = '1.95';
        """)
        self.conn.commit()
        cur.close()

    def convert_1_93_to_1_95(self):
        """Convert DB from v1.93 to v1.95"""
        with self.connect():
            self.convert_schema_1_93_to_1_95()

    def convert_schema_1_95_to_2_0(self):
        """DB schema changes: create layout table"""
        cur = self.conn.cursor()
        cur.execute("""
            -- add extent to lock_type
            ALTER TABLE lock ALTER COLUMN type TYPE VARCHAR(255);
            DROP TYPE lock_type;
            CREATE TYPE lock_type AS ENUM(
                'object', 'device', 'media', 'media_update', 'extent'
            );
            ALTER TABLE lock ALTER COLUMN type TYPE lock_type
                USING (type::lock_type);

            -- create layout table
            CREATE TABLE layout (
                object_uuid     varchar(36),
                version         integer DEFAULT 1 NOT NULL,
                extent_uuid     varchar(36),
                layout_index    integer,

                PRIMARY KEY (object_uuid, version, layout_index)
            );

            -- update object table
            ALTER TABLE object ADD lyt_info jsonb;

            UPDATE object
                SET lyt_info = extent.lyt_info
                FROM extent
                WHERE object.uuid = extent.uuid
                    AND object.version = extent.version;

            ALTER TABLE object RENAME uuid TO object_uuid;
            ALTER TABLE object RENAME CONSTRAINT object_uuid_key
                TO object_object_uuid_key;

            -- update deprecated_object table
            ALTER TABLE deprecated_object ADD lyt_info jsonb;

            UPDATE deprecated_object
                SET lyt_info = extent.lyt_info
                FROM extent
                WHERE deprecated_object.uuid = extent.uuid
                    AND deprecated_object.version = extent.version;

            ALTER TABLE deprecated_object RENAME uuid TO object_uuid;

            -- update and remove primary key of extent table
            ALTER TABLE extent DROP CONSTRAINT extent_pkey;
            ALTER TABLE extent RENAME TO old_extent;

            -- object_uuid, object_version and lyt_index are temporary variables
            -- used for the migration, they will be removed further
            CREATE TABLE extent (
                extent_uuid     varchar(36) UNIQUE DEFAULT uuid_generate_v4(),
                state           extent_state,
                size            bigint,
                medium_family   dev_family,
                medium_id       varchar(255),
                address         varchar(1024),
                hash            jsonb,
                info            jsonb,
                object_uuid     varchar(36),
                object_version  integer,
                lyt_index       integer,

                PRIMARY KEY (extent_uuid)
            );

            INSERT INTO extent (extent_uuid, state, size, medium_family,
                                medium_id, address, hash,
                                object_uuid, object_version, lyt_index)
                SELECT uuid_generate_v4(),
                       state,
                       cast(value->>'sz' AS bigint),
                       cast(value->>'fam' AS dev_family),
                       value->>'media',
                       value->>'addr',
                       (case
                           when value?&array['md5', 'xxh128'] then
                              json_build_object('md5', value->>'md5',
                                                'xxh128', value->>'xxh128')
                           when value?'md5' then
                              json_build_object('md5', value->>'md5')
                           when value?'xxh128' then
                              json_build_object('xxh128', value->>'xxh128')
                           else '{}'
                       end)::jsonb,
                       uuid,
                       version,
                       ordinality-1
                FROM old_extent,
                    LATERAL jsonb_array_elements(extents) WITH ordinality;

            DROP TABLE old_extent;

            -- set layout table
            INSERT INTO layout (object_uuid, version,
                                extent_uuid, layout_index)
                SELECT object_uuid, object_version, extent_uuid, lyt_index
                FROM extent;

            ALTER TABLE extent
                DROP object_uuid,
                DROP object_version,
                DROP lyt_index;

            -- remove outdated functions and indexes
            DROP FUNCTION IF EXISTS extents_mda_idx(jsonb) CASCADE;

            -- new operation_type type with the 'Device mount' value
            ALTER TYPE operation_type RENAME TO old_operation_type;
            CREATE TYPE operation_type AS ENUM (
                'Library scan', 'Library open',
                'Device lookup', 'Medium lookup',
                'Device load', 'Device unload',
                'LTFS mount', 'LTFS umount',
                'LTFS format', 'LTFS df',
                'LTFS sync'
            );

            -- use new type in logs table
            ALTER TABLE logs ALTER COLUMN cause
                SET DATA TYPE operation_type
                USING cause::text::operation_type;

            -- delete old_operation_type type
            DROP TYPE old_operation_type;

            -- new fs_status type with the 'importing' value
            ALTER TYPE fs_status RENAME TO old_fs_status;
            CREATE TYPE fs_status AS ENUM (
                'blank', 'empty', 'used', 'full', 'importing'
            );

            -- use new type in media table
            ALTER TABLE media ALTER COLUMN fs_status
                SET DATA TYPE fs_status
                USING fs_status::text::fs_status;

            -- delete old_fs_status type
            DROP TYPE old_fs_status;

            -- create enum obj_status
            CREATE TYPE obj_status AS ENUM ('incomplete', 'readable',
                                            'complete');

            -- add obj_status attributes to object and deprecated object tables
            -- with the default set to complete
            ALTER TABLE object ADD obj_status obj_status DEFAULT 'complete';
            ALTER TABLE deprecated_object ADD obj_status obj_status
                DEFAULT 'complete';

            -- set immediately change the defaults to incomplete
            ALTER TABLE object ALTER COLUMN obj_status SET DEFAULT 'incomplete';
            ALTER TABLE deprecated_object ALTER COLUMN obj_status SET
                DEFAULT 'incomplete';

            -- add offset to extent table
            -- the name 'offset' is a reserved keyword
            ALTER TABLE extent ADD COLUMN offsetof bigint;
            UPDATE extent SET offsetof = -1;

            -- update current schema version
            UPDATE schema_info SET version = '2.0';
        """)
        self.conn.commit()
        cur.close()

    def convert_1_95_to_2_0(self):
        """Convert DB from v1.95 to v2.0"""
        with self.connect():
            self.convert_schema_1_95_to_2_0()

    def convert_schema_2_0_to_2_1(self):
        """DB schema changes: add times to object tables and library to
           device and media"""
        default_tape_library = cfg.get_default_library(ResourceFamily.RSC_TAPE)
        default_dir_library = cfg.get_default_library(ResourceFamily.RSC_DIR)
        default_rados_library = cfg.get_default_library(
            ResourceFamily.RSC_RADOS_POOL)
        cur = self.conn.cursor()
        cur.execute(f"""
            -- add times to object tables
            ALTER TABLE object
                ADD creation_time timestamp DEFAULT now(),
                ADD access_time timestamp DEFAULT now();

            ALTER TABLE deprecated_object
                ADD creation_time timestamp DEFAULT now(),
                ADD access_time timestamp DEFAULT now();

            -- add 'library' column to device table
            ALTER TABLE device ADD library varchar(255) NOT NULL
                default 'legacy';
            UPDATE device SET library = '{default_tape_library}'
                WHERE family = 'tape';
            UPDATE device SET library = '{default_dir_library}'
                WHERE family = 'dir';
            UPDATE device SET library = '{default_rados_library}'
                WHERE family = 'rados_pool';
            ALTER TABLE device ALTER COLUMN library DROP DEFAULT;
            ALTER TABLE device DROP CONSTRAINT device_id_key;
            ALTER TABLE device DROP CONSTRAINT device_pkey;
            ALTER TABLE device ADD PRIMARY KEY (family, id, library);

            -- add 'library' column to media table
            ALTER TABLE media ADD library varchar(255) NOT NULL
                default 'legacy';
            UPDATE media SET library = '{default_tape_library}'
                WHERE family = 'tape';
            UPDATE media SET library = '{default_dir_library}'
                WHERE family = 'dir';
            UPDATE media SET library = '{default_rados_library}'
                WHERE family = 'rados_pool';
            ALTER TABLE media ALTER COLUMN library DROP DEFAULT;
            ALTER TABLE media DROP CONSTRAINT media_id_key;
            ALTER TABLE media DROP CONSTRAINT media_pkey;
            ALTER TABLE media ADD PRIMARY KEY (family, id, library);

            -- add 'library' column to extent table
            ALTER TABLE extent ADD medium_library varchar(255) NOT NULL
                default 'legacy';
            UPDATE extent SET medium_library = '{default_tape_library}'
                WHERE medium_family = 'tape';
            UPDATE extent SET medium_library = '{default_dir_library}'
                WHERE medium_family = 'dir';
            UPDATE extent SET medium_library = '{default_rados_library}'
                WHERE medium_family = 'rados_pool';
            ALTER TABLE extent ALTER COLUMN medium_library DROP DEFAULT;

            -- add 'library' column to log table
            ALTER TABLE logs ADD library varchar(255) NOT NULL default 'legacy';
            UPDATE logs SET library = '{default_tape_library}'
                WHERE family = 'tape';
            UPDATE logs SET library = '{default_dir_library}'
                WHERE family = 'dir';
            UPDATE logs SET library = '{default_rados_library}'
                WHERE family = 'rados_pool';
            ALTER TABLE logs ALTER COLUMN library DROP DEFAULT;

            -- resize 'device' and 'medium' to 255 into logs
            ALTER TABLE logs ALTER COLUMN device TYPE varchar(255);
            ALTER TABLE logs ALTER COLUMN medium TYPE varchar(255);

            -- update current schema version
            UPDATE schema_info SET version = '2.1';
        """)
        self.conn.commit()
        cur.close()

    def convert_2_0_to_2_1(self):
        """Convert DB from v2.0 to v2.1"""
        with self.connect():
            self.convert_schema_2_0_to_2_1()

    def convert_schema_2_1_to_2_2(self):
        """DB schema changes: add _grouping and groupings columnst"""
        cur = self.conn.cursor()
        cur.execute(f"""
            -- add _grouping to object and deprecated_object tables
            ALTER TABLE object ADD _grouping varchar(255);
            ALTER TABLE deprecated_object ADD _grouping varchar(255);

            -- add groupings to media table
            ALTER TABLE media ADD COLUMN groupings JSONB;

            -- update current schema version
            UPDATE schema_info SET version = '2.2';
        """)
        self.conn.commit()
        cur.close()

    def convert_2_1_to_2_2(self):
        """Convert DB from v2.1 to v2.2"""
        with self.connect():
            self.convert_schema_2_1_to_2_2()

    def convert_schema_2_2_to_3(self):
        """DB schema changes from v2.2 to v3.0: create copy table"""
        default_copy_name = cfg.get_default_copy_name()
        cur = self.conn.cursor()
        cur.execute(f"""
            -- create new type copy_status
            CREATE TYPE copy_status AS ENUM (
                'incomplete', 'readable', 'complete');

            -- create copy table
            CREATE TABLE copy (
                object_uuid     varchar(36),
                version         integer DEFAULT 1 NOT NULL,
                copy_name       varchar(1024),
                lyt_info        jsonb,
                copy_status     copy_status DEFAULT 'incomplete',
                creation_time   timestamp DEFAULT now(),
                access_time     timestamp DEFAULT now(),

                PRIMARY KEY (object_uuid, version, copy_name)
            );

            INSERT INTO copy (object_uuid, version, copy_name, lyt_info,
                              copy_status, creation_time, access_time)
                SELECT object_uuid, version, '{default_copy_name}', lyt_info,
                       obj_status::text::copy_status, creation_time,
                       access_time
                FROM object;

            INSERT INTO copy (object_uuid, version, copy_name, lyt_info,
                              copy_status, creation_time, access_time)
                SELECT object_uuid, version, '{default_copy_name}', lyt_info,
                       obj_status::text::copy_status, creation_time,
                       access_time
                FROM deprecated_object;

            -- update layout table
            ALTER TABLE layout ADD copy_name varchar(1024);
            UPDATE layout SET copy_name = '{default_copy_name}';
            ALTER TABLE layout DROP CONSTRAINT layout_pkey;
            ALTER TABLE layout ADD PRIMARY KEY
                (object_uuid, version, layout_index, copy_name);

            -- update object table
            ALTER TABLE object DROP lyt_info;
            ALTER TABLE object DROP obj_status;
            ALTER TABLE object DROP access_time;

            -- update deprecated object table
            ALTER TABLE deprecated_object DROP lyt_info;
            ALTER TABLE deprecated_object DROP obj_status;
            ALTER TABLE deprecated_object DROP access_time;

            DROP TYPE obj_status;

            -- update lock table
            ALTER TABLE lock ADD is_early boolean DEFAULT FALSE;
            UPDATE lock SET is_early = FALSE;

            -- update current schema version
            UPDATE schema_info SET version = '3.0';
        """)
        self.conn.commit()
        cur.close()

    def convert_2_2_to_3(self):
        """Convert DB from v2.2 to v3.0"""
        with self.connect():
            self.convert_schema_2_2_to_3()

    def convert_schema_3_0_to_3_2(self):
        """DB schema changes: append last_locate timestamp to lock,
           size to object and deprecated object, ctime to extent"""
        cur = self.conn.cursor()
        migration_object = size_update_3_0_to_3_2("object")
        migration_depr = size_update_3_0_to_3_2("deprecated_object")
        cur.execute(f"""
            -- update lock table
            ALTER TABLE lock ADD last_locate timestamp DEFAULT NULL;

            -- update object and deprecated object table
            ALTER TABLE object ADD size bigint DEFAULT -1;
            ALTER TABLE deprecated_object ADD size bigint DEFAULT -1;

            {migration_object}
            {migration_depr}

            -- update extent table
            ALTER TABLE extent ADD creation_time timestamp DEFAULT now();
            UPDATE extent SET creation_time = c.creation_time FROM
                (copy JOIN layout USING (object_uuid, version, copy_name)) c
                    WHERE extent.extent_uuid = c.extent_uuid;

            -- update current schema version
            UPDATE schema_info SET version = '3.2';
        """)
        self.conn.commit()
        cur.close()

    def convert_3_0_to_3_2(self):
        """Convert DB from v3.0 to v3.2"""
        with self.connect():
            self.convert_schema_3_0_to_3_2()

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
