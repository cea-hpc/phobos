#!/bin/bash
# test preparation script
# prevent from simultaneous runs of 'make check'

lock_file=/tmp/phobos_test.lock
test_timeout=300 #5 min

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
