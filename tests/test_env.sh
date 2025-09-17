#
#  All rights reserved (c) 2014-2022 CEA/DAM.
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
# Defines python and phobos environment for running tests in-tree.
# This must be sourced from another script in 'src/tests'.

bin=${BASH_SOURCE[0]}
bin=$(readlink -e -- "$bin")
if [ -z "$bin" ]; then
	echo "BASH_SOURCE is not expected to be empty." >&2
	# don't exit, we are in a shell!
	return 1
fi

test_bin_dir=$(dirname "$bin")

# define phython paths for in-tree tests
PY=$(which python3)
ARCH=$(uname -m)
PY_VERSION=$($PY --version 2>&1 | cut -d' ' -f2 | cut -d'.' -f1,2)
cli_dir="$(readlink -e $test_bin_dir/../src/cli)"
PHO_PYTHON_PATH="$cli_dir/build/lib.linux-$ARCH-$PY_VERSION/"
export PYTHONPATH="$PHO_PYTHON_PATH"

lrs_dir="$(readlink -e $test_bin_dir/../src/lrs)"
tlc_dir="$(readlink -e $test_bin_dir/../src/tlc)"
hsm_dir="$(readlink -e $test_bin_dir/../src/hsm)"
tape_lib_certif_dir="$(readlink -e $test_bin_dir/../tape_library_certification)"

# library paths
PHO_ADMINLIB_PATH="$(readlink -e $test_bin_dir/../src/admin/.libs/)"
PHO_IOLIB_PATH="$(readlink -e $test_bin_dir/../src/io-modules/.libs/)"
PHO_LAYOUTLIB_PATH="$(readlink -e $test_bin_dir/../src/layout-modules/.libs/)"
PHO_LDMLIB_PATH="$(readlink -e $test_bin_dir/../src/ldm-modules/.libs/)"
PHO_STORELIB_PATH="$(readlink -e $test_bin_dir/../src/store/.libs/)"

export LD_LIBRARY_PATH="$LD_LIBRARY_PATH:$PHO_STORELIB_PATH:\
$PHO_ADMINLIB_PATH:$PHO_PYTHON_PATH:$PHO_LAYOUTLIB_PATH:\
$PHO_LDMLIB_PATH:$PHO_IOLIB_PATH"

# phobos stuff
ldm_helper=$(readlink -e $test_bin_dir/../scripts/)/pho_ldm_helper

export PHOBOS_CFG_FILE="$test_bin_dir/phobos.conf"
export PHOBOS_LTFS_cmd_format="$ldm_helper format_ltfs '%s' '%s'"
export PHOBOS_LTFS_cmd_mount="$ldm_helper mount_ltfs '%s' '%s'"
export PHOBOS_LTFS_cmd_umount="$ldm_helper umount_ltfs '%s' '%s'"
export PHOBOS_LTFS_cmd_release="$ldm_helper release_ltfs '%s'"

phobos="$cli_dir/scripts/phobos"
[ x$DEBUG = x1 ] && phobos="$phobos -vvv"

valg_phobos="$LOG_COMPILER $phobos"

phobosd="$lrs_dir/phobosd"
tlc="$tlc_dir/phobos_tlc"
phobos_hsm_sync_dir="$hsm_dir/phobos_hsm_sync_dir"
valg_phobos_hsm_sync_dir="$LOG_COMPILER $phobos_hsm_sync_dir"

# utils function
# display error message and exits
function error {
    echo "$*"
    exit 1
}

true
