#!/usr/bin/env bash

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

# test preparation script
# prevent from simultaneous runs of 'make check'

lock_file=/tmp/phobos_test.lock
test_timeout=1800 #30 min

function delayed_cleanup
{
	# clean the file when no more 'make' command runs
	# or after test timeout
	(
		now=$(date +%s)
		lock_time=$(stat -c"%Y" $lock_file)
		time_out=$(($now + $test_timeout))
		# clean the file when no more 'make check' run
		while pgrep -s 0 make > /dev/null; do
			sleep 1
			now=$(date +%s)
			# clean the file after a given timeout
			if (($now > $time_out)); then
				echo timeout
				break
			fi
		done
		[[ "$(stat -c"%Y" $lock_file 2>/dev/null)" == "$lock_time" ]] \
			&& rm -f $lock_file
	) &
	disown $!
}

if [ -e $lock_file ]; then
	# get lock mtime
	lock_time=$(stat -c"%Y" $lock_file)
	now=$(date +%s)
	diff=$((($now-$lock_time)/60))
	sec=$((($now-$lock_time)%60))

	echo "test lock file is present:"
	echo "	taken: $diff min $sec sec ago"
	echo "	owner: $(stat -c "%U" $lock_file)"

	if (($lock_time + $test_timeout < $now)); then
		echo "lock file timed out: trying to aquiring it"
		rm -f $lock_file
	else
		exit 1
	fi
fi
# grant the lock only if the touch succeeds
touch $lock_file || exit 1

# start cleanup watchdog
delayed_cleanup

# allow someone else to aquire the lock after timeout
# (ignore error in the case the caller don't own the file)
chmod 777 $lock_file 2>/dev/null || true
