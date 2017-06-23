#!/bin/bash

#
#  All rights reserved (c) 2014-2017 CEA/DAM.
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

set -e

# test simple layout (default)
bash ./test_store.sh

# test raid1 (replication, default parameters)
bash ./test_store.sh raid1

# test raid1 (replication, 3 replicas total)
export PHOBOS_LAYOUT_RAID1_repl_count=3
bash ./test_store.sh raid1

trap - EXIT ERR
