#!/usr/bin/env bash

cd /var/lib/pgsql/db
pg_ctl -D /var/lib/pgsql/db/ -l logfile start
