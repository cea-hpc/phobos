# Defines python and phobos environment for running tests in-tree.
# This must be sourced from another script in 'src/tests'.

bin=$(readlink -e -- "$0")
if [ -z "$bin" ]; then
	echo "This script must be sourced from another script." >&2
	# don't exit, we are in a shell!
	return 1
fi

test_bin_dir=$(dirname "$bin")

# define phython paths for in-tree tests
PY=$(which python)
ARCH=$(uname -m)
PY_VERSION=$($PY -c 'import sys; print "%d.%d" % (sys.version_info[0],\
                                                  sys.version_info[1])')
cli_dir="$test_bin_dir/../cli"
PHO_PYTHON_PATH="$cli_dir/build/lib.linux-$ARCH-$PY_VERSION/"
export PYTHONPATH="$PHO_PYTHON_PATH"

# library paths
PHO_STORELIB_PATH="$test_bin_dir/../store/.libs/"
export LD_LIBRARY_PATH="$LD_LIBRARY_PATH:$PHO_STORELIB_PATH:$PHO_PYTHON_PATH"

# phobos stuff
ldm_helper=$(readlink -e $test_bin_dir/../../scripts/)/pho_ldm_helper

export PHOBOS_CFG_FILE="$test_bin_dir/phobos.conf"
export PHOBOS_LDM_cmd_format_ltfs="$ldm_helper format_ltfs '%s' '%s'"
export PHOBOS_LDM_cmd_mount_ltfs="$ldm_helper mount_ltfs '%s' '%s'"
export PHOBOS_LDM_cmd_umount_ltfs="$ldm_helper umount_ltfs '%s' '%s'"

# 'phobos' command
phobos="$cli_dir/scripts/phobos"
