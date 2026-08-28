[dss] - Configuring how to connect to the database
==================================================

This section explains how Phobos' connect to the database. All database-related
parameters should be listed under the **[dss]** section.

*connect_string*
----------------

The **connect_string** parameter defines which database Phobos will connect to
and how it will do so. Phobos accepts any string, but this string will be passed
to PostgreSQL as a connection string. PostgreSQL expects the string to follow
the "keyword=value" format, with each setting separated by a space. Typically,
the value should include the following four parameters:

    * **dbname** :   The database name

    * **host** :     Name of host to connect to

    * **user** :     PostgreSQL user name to connect as

    * **password** : Password to be used

For more information on connection strings, refer to the official PostgreSQL
documentation (https://www.postgresql.org/docs/current/libpq-connect.html#LIBPQ-CONNSTRING)

If the **connect_string** is not specified, Phobos defaults to the following
**connect_string = dbname=phobos host=localhost**.

Example:

.. code:: ini

    [dss]
    connect_string = dbname=phobos host=localhost user=phobos password=phobos

*retry_max_seconds*
-------------------

The **retry_max_seconds** parameter defines the maximum total time in seconds
spent retrying failed DSS requests.

Retries use an exponential backoff (1s, 2s, 4s, 8s, ...).

The value 0 means no retry and is the default.

Example:

.. code:: ini

    [dss]
    retry_max_seconds = 128
