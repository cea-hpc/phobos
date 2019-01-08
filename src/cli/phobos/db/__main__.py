#!/usr/bin/python

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
Convert phobos database
  * from 0 (phobos v1.0) to 1 (phobos v1.1):
  - update db schema
  - retrieve all entries from extent and media and set the json integer fields
    with the appropriate type.
  * from 1 (phobos v1.1) to 2 (phobos v1.2):
  - media and device model are updated from enum type to varchar
  - create schema_info table
"""

import argparse
from contextlib import contextmanager
from distutils.version import LooseVersion
import logging
import json
import psycopg2
import sys

from phobos.cli import PhobosActionContext
from phobos.core import cfg

CURRENT_SCHEMA_VERSION = "1.2"

LOGGER = logging.getLogger(__name__)

class Migrator:
    """StrToInt JSONB conversion engine"""
    def __init__(self, **kwargs):
        self.conn = None

        # { source_version: (target_version, migration_function) }
        self.convert_funcs = {
            "1.1": ("1.2", self.convert_1_to_2),
        }

        self.reachable_versions = set(
            version for version, _ in self.convert_funcs.itervalues()
        )

    @contextmanager
    def connect(self):
        conn_str = cfg.get_val("dss", "connect_string")
        with psycopg2.connect(conn_str) as conn:
            self.conn = conn
            yield conn
        self.conn = None

    def convert_schema_1_to_2(self):
        """
        DB schema changes:
        - device.id    : from varchar(32) to varchar(255)
        - media.id     : from varchar(32) to varchar(255)
        - device.model : from enum to varchar(32)
        - media.model  : from enum top varchar(32)
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
            self.convert_schema_1_to_2();

    def convert(self, target_version=None):
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
                )
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
            version = None
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


def migrate(args):
    """Perform database schema migration from arguments"""
    cfg.load_file()

    migrator = Migrator()
    if args.schema_version:
        print migrator.schema_version()
        sys.exit(0)

    target_version = args.target_version
    if not args.yes:
        print "Your are about to upgrade database schema from version %s "\
              "to version %s." % (migrator.schema_version(), target_version)
        confirm = raw_input("Do you want to continue? [y/N]: ")
        if confirm != 'y':
            sys.exit(1)

    migrator.convert(args.target_version)


def main(argv=None):
    migrator = Migrator()

    parser = argparse.ArgumentParser(
        prog="phobos_db",
        description="Phobos database setup and schemas",
    )

    subparsers = parser.add_subparsers(title="action", dest="action")
    subparsers.required = True

    # migrate subparser
    migrate_parser = subparsers.add_parser(
        "migrate",
        help="Manage phobos database schema initialization and migrations. By "
             "default, migrates the database to the most recent schema."
    )
    migrate_parser.set_defaults(main=migrate)
    migrate_parser.add_argument(
        "-t", "--target-version", choices=migrator.reachable_versions,
        default=CURRENT_SCHEMA_VERSION,
        help="The schema version to migrate to",
    )
    migrate_parser.add_argument(
        "-y", "--yes", action="store_true", help="Don't ask for confirmation",
    )
    migrate_parser.add_argument(
        "-V", "--schema-version", action="store_true",
        help="Print the current schema version and exit",
    )

    # Parse args and execute appropriate function
    args = parser.parse_args(argv)
    args.main(args)

if __name__ == '__main__':
    logging.basicConfig(format=PhobosActionContext.CLI_LOG_FORMAT_REG)
    main()

# -*- mode: Python; c-basic-offset: 4; indent-tabs-mode: nil; -*-
# vim:expandtab:shiftwidth=4:tabstop=4:ft=python:
