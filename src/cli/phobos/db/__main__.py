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
import logging
import sys

from phobos.cli import PhobosActionContext
from phobos.db import Migrator, CURRENT_SCHEMA_VERSION
from phobos.core import cfg

LOGGER = logging.getLogger(__name__)


def migrate(args, migrator):
    """Perform database schema migration from arguments"""
    schema_version = migrator.schema_version()
    target_version = args.target_version

    # Skip confirmation for '-y' and if the database wasn't initialized
    if not args.yes and schema_version != "0":
        print "Your are about to upgrade database schema from version %s "\
              "to version %s." % (migrator.schema_version(), target_version)
        confirm = raw_input("Do you want to continue? [y/N]: ")
        if confirm != 'y':
            sys.exit(1)

    migrator.migrate(args.target_version)


def print_schema_version(_args, migrator):
    print migrator.schema_version()


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
             "default, migrates or initializes the database to the most recent "
             "schema."
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

    # version subparser
    version_parser = subparsers.add_parser(
        "version",
        help="Print the current schema version and exit",
    )
    version_parser.set_defaults(main=print_schema_version)

    # setup_tables parser
    setup_tables_parser = subparsers.add_parser(
        "setup_tables",
        help="Setup phobos tables and types at a given version",
    )
    setup_tables_parser.add_argument(
        "-s", "--schema", default=CURRENT_SCHEMA_VERSION,
        help="Version of the schema to create",
    )
    setup_tables_parser.set_defaults(
        main=lambda _args, migrator: migrator.create_schema(args.schema)
    )

    # drop_tables parser
    drop_tables_parser = subparsers.add_parser(
        "drop_tables",
        help="Drop phobos tables",
    )
    drop_tables_parser.set_defaults(
        main=lambda _args, migrator: migrator.drop_tables()
    )

    # Parse args and conf, then execute appropriate function
    args = parser.parse_args(argv)
    cfg.load_file()
    args.main(args, migrator)


if __name__ == '__main__':
    logging.basicConfig(format=PhobosActionContext.CLI_LOG_FORMAT_REG)
    main()

# -*- mode: Python; c-basic-offset: 4; indent-tabs-mode: nil; -*-
# vim:expandtab:shiftwidth=4:tabstop=4:ft=python:
