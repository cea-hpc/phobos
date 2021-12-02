% Phobos setup/migration and issues

# Installation
## Requirements for tape access
You need to install LTFS >= 2.4 to enable phobos access to tapes.

LTFS RPM can be found on IBM Fix Central: [ltfs 2.4](https://www-945.ibm.com/support/fixcentral/swg/selectFixes?parent=Tape%20drivers%20and%20software&product=ibm/Storage_Tape/Long+Term+File+System+LTFS&release=2.4&platform=Linux&function=all)

You can also retrieve its sources on gihub:
https://github.com/LinearTapeFileSystem/ltfs

If you want to build RPMs from these sources, you can find packaging resources
(i.e. spec file) for LTFS here: https://github.com/piste2750/rpm-ltfs

Note: since LTFS 2.4, `lin_tape` driver is no longer needed to access tapes.
LTFS now uses the standard linux tape driver (st).

## Phobos installation
Install phobos and its requirements:
```
yum install phobos
```

## Database setup

### On RHEL8/CentOS8

Install postgresql:
```
dnf install postgresql-server postgresql-contrib
```

Initialize postgresql directories:
```
postgresql-setup --initdb --unit postgresql
```

Edit `/var/lib/pgsql/data/pg_hba.conf` to authorize access from phobos host
(localhost in this example):
```
# "local" is for Unix domain socket connections only
local   all             all                                     trust
# IPv4 local connections:
host    all             all             127.0.0.1/32            md5
# IPv6 local connections:
host    all             all             ::1/128                 md5
```

Start the database server:
```
systemctl start postgresql
```

Finally, create phobos database and tables as postgres user (the password for
SQL phobos user will be prompted for unless provided with -p):
```
sudo -u postgres phobos_db setup_db -s
```

### On RHEL7/CentOS7

Install postgresql version >= 9.4 (from EPEL or any version compatible with
Postgres 9.4):
```
yum install postgresql94-server postgresql94-contrib
```

Initialize postgresql directories:
```
/usr/pgsql-9.4/bin/postgresql94-setup initdb
```

Edit `/var/lib/pgsql/9.4/data/pg_hba.conf` to authorize access from phobos host
(localhost in this example):
```
# "local" is for Unix domain socket connections only
local   all             all                                     trust
# IPv4 local connections:
host    all             all             127.0.0.1/32            md5
# IPv6 local connections:
host    all             all             ::1/128                 md5
```

Start the database server:
```
systemctl start postgresql-9.4.service
```

Finally, create phobos database and tables as postgres user (the password for
SQL phobos user will be prompted for unless provided with -p):
```
sudo -u postgres phobos_db setup_db -s
```

### Error on database setup/migration
If the database setup failed because `generate_uuid_v4()` is missing, it means
the psql extension `uuid-ossp` is missing. To make it available, execute the
following command as SQL phobos user. Phobos user needs to have `create` rights
to execute the command.

```
CREATE EXTENSION "uuid-ossp";
```

In case SQL phobos user does not have `create` rights, you can still use the
first following command to give them to it and change them back with the second
one:

```
ALTER USER phobos WITH SUPERUSER;
ALTER USER phobos WITHOUT SUPERUSER;
```

# Upgrade of an existing instance

After upgrading phobos, run the DB conversion script (credentials to connect to
the database will be retrieved from /etc/phobos.conf):
```
/usr/sbin/phobos_db migrate
# 'y' to confirm
```
