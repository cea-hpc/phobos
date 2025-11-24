% Phobos PSQL failover

This tutorial is for general understanding of how to setup automatic failover
and replication accross multiple nodes of the PSQL database. Adjustements may
be necessary for your amount of nodes, your network configuration and additional
features you may want to enable.

# Requirements
## Packages

Are needed:
 * PostgreSQL (we'll assume version 13 for this document)
 * repmgr (we'll assume version 13 aswell)

To download the latter, you can do the following:
```
dnf install -y https://download.postgresql.org/pub/repos/yum/reporpms/EL-9-x86_64/pgdg-redhat-repo-latest.noarch.rpm
dnf install -y repmgr_13
```

# Write-Ahead Logging (WAL)

Repmgr works by using the WAL mechanism to replicate information accross
multiple nodes. This mechanism corresponds to keeping a journal of all changes
done to the database, so that the database files are not updated for each
transaction.

The main advantage of this approach is that a transaction is considered
committed when it is written to this journal, instead of when the database
files are updated. Therefore, there are much less syncs necessary, and thus
much less disk writes.

This mechanism also ensures that in the event the database becomes inaccessible,
all ongoing transactions are not lost, and the database can be recovered by
simply reading the journal.

For repmgr, the WAL will be used to replicate data, as every node will only need
to mirror the journal to be up-to-date with every committed transaction, and
thus all the databases accross those nodes will be synchronised.

Check PostGreSQL's documentation about the file for more information
(https://www.postgresql.org/docs/11/wal-intro.html).

## Nodes

We will consider 2 nodes for this tutorial, but more can be added, as commands
are similar. There will be one node called the "primary", which will hold the
main database that can be written to and read from, and one (or more) node
called the "standby". The standby nodes will only have read-access to the main
database. The sum of the primary node and standby nodes will constitute a
"cluster".

For the rest of this document, we will use 2 virtual machines, named "VM0",
the primary node, and "VM1", the standby node. VM0's IP address will be
10.200.0.1, and VM1's address will be 10.200.0.2.

# Configuration
## PSQL

Since the PSQL database will be accessed by multiples nodes, the `pg_hba.conf`
file of both VM0 and VM1 has to include at least the following:
```
local      replication      repmgr                             trust
host       replication      repmgr        127.0.0.1/32         trust
host       replication      repmgr        10.200.0.2/24        trust
local      repmgr           repmgr                             trust
host       repmgr           repmgr        127.0.0.1/32         trust
host       repmgr           repmgr        10.200.0.2/24        trust
```

Check PostGreSQL's documentation about the file for more information
(https://www.postgresql.org/docs/12/auth-pg-hba-conf.html).

For the regular configuration, the `postgresql.conf` file of VM0 has to include
the following:
```
max_wal_senders = 10    # how many standby nodes can be used
max_replication_slots = 10      # how many replication slots can be used
wal_level = 'replica'   # the minimum level for data replication using the WAL
hot_standby = on    # allow user requests while in standby mode
archive_mode = on   # archive WAL segment when they are completed
archive_command = '/bin/true'   # the archival command to use, here a dummy one
shared_preload_libraries = 'repmgr'     # the library to preload so that proper
replication is possible accross multiple nodes
```

In this file, you must also set the addresses that can access the database.
```
listen_addresses = '*'  # '*' means everything, but you may want to restrict
the addresses for your network configuration
```
This parameter has to be set in the `postgresql.conf` file of every node in
your cluster, as if the primary becomes inaccessible, users may make requests
for data in the databases of your standby nodes.

## repmgr

First create the `/etc/repmgr/13/repmgr.conf` file on your primary node, and
add the following to it:
```
node_id=1
node_name=node1
conninfo='host=vm0 user=repmgr password=repmgr dbname=repmgr connect_timeout=2'
data_directory='/var/lib/pgsql/13/data/'
failover=automatic

promote_command='/usr/pgsql-13/bin/repmgr standby promote -f /etc/repmgr/13/repmgr.conf --log-to-file'
follow_command='/usr/pgsql-13/bin/repmgr standby follow -f /etc/repmgr/13/repmgr.conf --log-to-file --upstream-node-id=%n'
```

Then copy this file on every node in your network, changing the `node_id`,
`node_name` and `conninfo` as needed.

# Setting up the replication
## Create the PSQL user and database

After restarting the PSQL daemon to make sure the configuration is taken into
account, you can make the following PSQL commands on every node of your cluster:
```
create user repmgr with password 'repmgr' superuser;
create database repmgr with owner repmgr;
```

The password for user `repmgr` may not be needed, in which case you must change
the connection string in the configuration files above.

## Create the cluster and register the primary node

Now, execute the following commands as the `postgres` user on your primary node:
```
 $ /usr/pgsql-13/bin/repmgr -f /etc/repmgr/13/repmgr.conf primary register
 $ /usr/pgsql-13/bin/repmgr -f /etc/repmgr/13/repmgr.conf cluster show

 ID | Name  | Role    | Status    | Upstream | Location | Priority | Connection string
 ----+-------+---------+-----------+----------+----------+----------+-----------------------------------------
 1  | node1 | primary | * running |          | default  | 100      | host=vm0 user=repmgr password=repmgr dbname=repmgr connect_timeout=2
```
The first command will register the current node as the primary one of your
cluster. This means the cluster will consider this node to hold the main
database and make replicas of its data on the standby nodes.

The second command is just used to confirm and check the cluster is correctly
set up, and the node is indeed shown as primary.

## Clone the primary's data and register the standby nodes

In this section, all commands must be run on each standby node.

Before registering the standby nodes in your cluster, you must first clone the
main database's data on each of the standby nodes, so that they all have the
same initial starting point. To do this, run the following:
```
/usr/pgsql-13/bin/repmgr --host vm0 --username repmgr --dbname repmgr -f /var/lib/pgsql/repmgr.conf standby clone --dry-run
```
Here, the `--host`, `--username` and `--dbname` are necessary because they
correspond to where the data is cloned from, contrarely to the connection
string in the configuration file, which is for the local PSQL instance.

After checking the command suceeds, you can do it again but without the
`--dry-run` flag. Then, you may restart the PSQL daemon, either with systemctl
or with the command given in the log of the clone:
```
pg_ctl -D /var/lib/pgsql/13/data start
```

Finally, you can now register the standby node with the following:
```
 $ /usr/pgsql-13/bin/repmgr -f /etc/repmgr/13/repmgr.conf standby register
 $ /usr/pgsql-13/bin/repmgr -f /etc/repmgr/13/repmgr.conf cluster show

 ID | Name  | Role    | Status    | Upstream | Location | Priority | Connection string
 ----+-------+---------+-----------+----------+----------+----------+-----------------------------------------
 1  |  vm0  | primary | * running |          | default  | 100      | host=vm0 user=repmgr password=repmgr dbname=repmgr connect_timeout=2
 2  |  vm1  | standby |   running |          | default  | 100      | host=vm1 user=repmgr password=repmgr dbname=repmgr connect_timeout=2
```

The second command should now show you at least 2 nodes in your network,
the primary "node1", and the standby "node2" (and more). All standby nodes
should be marked with "node1" as their upstream node, as it is the primary
node of your cluster.

# Setting up automatic failover

With these above steps followed, you should now be able to add/update/remove
content in your main database, and see the corresponding changes replicated
accross all nodes of your cluster. We will now see how to set up the automatic
takeover of the standby nodes in case the primary node encounters an issue.

## Start the repmgr daemons

To enable automatic failover, you must start the repmgr daemon on all the nodes
of your cluster:
```
systemctl start repmgr-13
```

This daemon will start monitoring the primary node of your cluster, and may
takeover in case the primary becomes unusable.

You can see all of these changes and monitoring events with the following
command:
```
/usr/pgsql-13/bin/repmgr -f /etc/repmgr/13/repmgr.conf cluster event
```

## Automated redirection

In case a failure does occur on the primary node, the repmgr daemons will handle
the switchover, but that does not mean the users will stop targeting the old
primary node.

To prevent these issues, you can either:
 * use `pgbouncer`, a connection pooler for PSQL, as detailed in this
[wiki entry](https://github.com/EnterpriseDB/repmgr/blob/master/doc/repmgrd-node-fencing.md)
 * use `pgcat`, a "nextgen" PostgreSQL pooler and proxy (like PgBouncer) with
support for sharding, load balancing, failover and mirroring.
[wiki entry](https://github.com/postgresml/pgcat)
 * or you can setup an entry proxy that the users will target for all their
PSQL commands, alongside a probe to regularly check on the cluster's status.
Then, if the primary node changes, you can redirect the proxy to that new node.

## Removal of the failed node

Whatever you chose, you must also handle the removal of the failed node from
the cluster, and its fixing. For the removal, you can use the following command:
```
/usr/pgsql-13/bin/repmgr -f /etc/repmgr/13/repmgr.conf primary unregister
```
Note that this may fail for several reasons:
 * if the database is not available, in which case this command is unnecessary
as the cluster considers the node failed
 * if the database is back online, because the node is still registered as
primary that is followed by other nodes. In this case, you may have to force
the unregister.
 * if the cluster as performed an automatic switchover, in which case the node
is now considered a standby, the command to use should be:
```
/usr/pgsql-13/bin/repmgr -f /etc/repmgr/13/repmgr.conf standby unregister
```

Then, after the node is fixed, you can add back to the node to the cluster by
following the standby cloning and registering section of this document.

# Troubleshoot

## Postgresql says the `repmgr` shared library is not installed

To fix this, you must have the `repmgr.so` library available in the PSQL library
repository. For this, simply do:
```
ln -s /usr/pgsql-13/lib/repmgr.so /usr/lib64/pgsql/repmgr.so
```

## Postgresql says the `repmgr` extension is not installed

To fix this, you must have the `repmgr` extensions installed with the package in
the repository of available extensions. To make it available:
```
ln -s /usr/pgsql-13/share/extension /usr/share/pgsql/extension
```
