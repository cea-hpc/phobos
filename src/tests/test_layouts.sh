#!/bin/bash

set -e

# test simple layout (default)
bash ./test_store.sh

# test raid1 (replication, default parameters)
bash ./test_store.sh raid1

# test raid1 (replication, 3 replicas total)
export PHOBOS_LAYOUT_RAID1_repl_count=3
bash ./test_store.sh raid1

trap - EXIT ERR
