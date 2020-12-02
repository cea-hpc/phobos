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
"""Helpers to manage postgres database and user setup."""

from contextlib import contextmanager

import psycopg2


@contextmanager
def _allow_pg_prog_error(pattern):
    """Filter and log psycopg2.ProgrammingError if the error message contains a
    given pattern.
    """
    try:
        yield
    except psycopg2.ProgrammingError as exc:
        if pattern not in str(exc):
            raise exc
        print(str(exc).strip())

def setup_db(database, user, password):
    """Create the phobos database, main user and load required postgres
    extensions. Uses environment to connect to the database.
    """
    if user is None or database is None:
        raise ValueError("database and user cannot be None")

    # Connect to "postgres" (admin) database
    with psycopg2.connect(dbname="postgres") as conn:
        conn.autocommit = True
        with conn.cursor() as cursor:
            # Admin input: no privilege escalation here, hence we don't care
            # about SQL injections. Some parts of these statements cannot be
            # prepared.

            # Create user
            with _allow_pg_prog_error("already exists"):
                cursor.execute("CREATE USER %s" % (user,))

            if password is not None:
                # Even if the user exists, update the password
                cursor.execute(
                    "ALTER USER %s " % (user,) +
                    "WITH PASSWORD %s", (password,)
                )

            # Create database
            with _allow_pg_prog_error("already exists"):
                cursor.execute(
                    "CREATE DATABASE %s WITH OWNER %s"
                    % (database, user)
                )

    # Connect to the newly created database to create extensions
    with psycopg2.connect(dbname=database) as conn:
        conn.autocommit = True
        with conn.cursor() as cursor:
            # Create btree_gin extension
            cursor.execute("""
                -- Create btree_gin extension
                CREATE EXTENSION IF NOT EXISTS btree_gin SCHEMA public;

                -- Create uuid-ossp extension
                CREATE EXTENSION IF NOT EXISTS "uuid-ossp" SCHEMA public;
            """)

def drop_db(database, user):
    """Drop the phobos database and user"""
    with psycopg2.connect(dbname="postgres") as conn:
        conn.autocommit = True
        with conn.cursor() as cursor:
            with _allow_pg_prog_error("does not exist"):
                cursor.execute("DROP DATABASE %s" % (database,))
            with _allow_pg_prog_error("does not exist"):
                cursor.execute("DROP USER %s" % (user,))
            # Don't drop the btree_gin extension in case another database use it
