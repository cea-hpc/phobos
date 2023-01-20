#!/bin/bash

if hash phobos_tape_library_test >>/dev/null 2>&1 ; then
    binary_test="phobos_tape_library_test"
else
    binary_test="$(dirname $0)/phobos_tape_library_test"
fi

phobos_bin="${phobos:-phobos}"

if (( $# != 2 && $# != 3 )); then
    echo "Usage:"
    echo "$(basename $0) drive_paths tape_labels [log_level]"
    echo ""
    echo "drive_slots and tape_labels must be written in nodeset syntax."
    echo "log level is an integer from 0/DISABLED to 5/DEBUG"
    echo "(default is 3/INFO).\n"
    echo ""
    echo "This command is only a wrapper around the execution of ${binary_test}"
    echo "to build the comma separated list of drive serial numbers and tape"
    echo "labels."
    exit 1
fi

tapes_list=$(nodeset --separator=',' -e $2)
drives_list=""
for drive_path in `nodeset -e $1`; do
    drive_serial=$(${phobos_bin} drive list ${drive_path})
    if [ "${drive_serial}" == "" ]; then
        echo "Error: ${drive_path} is not in phobos db to get its serial"
             "number."
        exit 1
    else
        if [ "${drives_list}" == "" ]; then
            drives_list="${drive_serial}"
        else
            drives_list="${drives_list},${drive_serial}"
        fi
    fi
done

if (( $# == 3 )); then
    log_level="$3"
else
    log_level="3"
fi

cmd="${binary_test} ${drives_list} ${tapes_list} ${log_level}"

echo "We run the following commandline:"
echo "${cmd}"
${cmd}
