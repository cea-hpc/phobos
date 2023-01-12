#!/bin/bash

# This scripts simulates with "mtx [status|load|unload]" commands how
# phobos manages a tape library during a massive format session and
# exits at first mtx error.

WAITING_DRIVE_NB_SECOND="1"

if (( $# < 2 )); then
    echo "Usage:"
    echo "$(basename $0) drive_slots tape_labels"
    echo ""
    echo "drive_slots and tape_labels must be written in nodeset syntax"
    exit 1
fi

input_drive_slot="$1"
input_tape="$2"

# drive_status_per_slot are "Ready" or the PID of the running command
declare -A drive_status_per_slot
tape_array=( $(nodeset -e ${input_tape}) )

# check drive are empty and tape unloaded
initial_mtx_status_file_template="$(basename $0)_initial_mtx_status.XXXXXX"
initial_mtx_status_file="$(mktemp --tmpdir ${initial_status_file_template})"
mtx status > ${initial_mtx_status_file} || { echo "Error $? on initial mtx" \
                                                  "status" &&
                                            exit 1 ; }
for drive_slot in `nodeset -e ${input_drive_slot}`
do
    grep -q "Data Transfer Element ${drive_slot}:Empty" \
        ${initial_mtx_status_file} || { echo "Error: the drive slot" \
                                             "${drive_slot} is not empty" &&
                                        echo "Please, empty it before" \
                                             "executing this test" &&
                                        exit 1 ; }

    drive_status_per_slot[${drive_slot}]="Ready"
done

ALL_DRIVE_READY="True"

for tape_label in ${tape_array[@]}; do
    grep "VolumeTag=${tape_label}" ${initial_mtx_status_file} |
        grep "Storage Element" |
        grep -q "Full" || { echo "Error: the tape ${tape_label} must be" \
                                 "unloaded before executing this test" &&
                            exit 1 ; }
done

if (( ${#tape_array[@]} > 0 )); then
    NO_MORE_TAPE="False"
else
    NO_MORE_TAPE="True"
fi

function mtx_status_load_status_unload {
    local drive_slot="$1"
    local tape_label="$2"
    local status_file="${TMPDIR:-/tmp}/status_drive_slot_${drive_slot}"

    echo "drive slot ${drive_slot} begins to load/unload tape ${tape_label}"

    # status to get tape_slot
    mtx status > "${status_file}" || { echo "Error $? on mtx status to get" \
                                            "tape slot of tape ${tape_label}" \
                                            "before loading it into" \
                                            "drive slot ${drive_slot}" &&
                                       exit 1 ; }
    local tape_slot_line=$(grep "VolumeTag=${tape_label}" "${status_file}" |
                           grep "Full")
    if [ "$?" != "0" ]; then
        echo "Error: tape ${tape_label} should be unloaded"
        exit 1
    fi

    local tape_slot=$(echo ${tape_slot_line} | awk -F'[ :]' '{print $3}')
    if [ "${tape_slot}" == "" ]; then
        echo "Error: tape slot of tape ${tape_label} is empty"
        exit 1
    fi

    # load
    mtx load ${tape_slot} ${drive_slot} > /dev/null ||
        { echo "Error $? when loading tape_slot ${tape_slot} into drive" \
               "${drive_slot}" &&
          exit 1 ; }

    # status to check origin tape_slot
    mtx status > "${status_file}" || { echo "Error $? on mtx status to get" \
                                            "origin slot of tape" \
                                            "${tape_label} before unloading" \
                                            "it from drive slot" \
                                            "${drive_slot}" &&
                                       exit 1 ; }

    local origin_tape_slot_line=$( \
        grep "VolumeTag = ${tape_label}" "${status_file}" |
        grep "Data Transfer Element ${drive_slot}:Full" |
        grep "Storage Element" | grep "Loaded)")
    if [ "$?" != "0" ]; then
        echo "Error: tape ${tape_label} should be loaded in drive ${drive_slot}"
        exit 1
    fi

    local origin_tape_slot=$(echo ${origin_tape_slot_line} | awk '{print $7}')
    if [ "${origin_tape_slot}" == "" ]; then
        echo "Error: origin tape slot of tape ${tape_label} in drive" \
             "${drive_slot} is empty"
        exit 1
    fi

    # check origin_tape_slot == tape_slot
    if [ ${origin_tape_slot} != ${tape_slot} ]; then
        echo "Error when unloading tape ${tape_label}" \
             "from drive ${drive_slot}" \
             "origin tape slot is ${origin_tape_slot} instead of ${tape_slot}"
        exit 1
    fi

    # unload
    mtx unload ${origin_tape_slot} ${drive_slot} > /dev/null ||
        { echo "Error $? when unloading" \
               "drive_slot ${drive_slot} into" \
               "origin_tape_slot ${origin_tape_slot}" &&
          exit 1 ; }

    echo "drive slot ${drive_slot} ends to load/unload tape ${tape_label}"
}

while [[ "${ALL_DRIVE_READY}" != "True" || "${NO_MORE_TAPE}" != "True" ]]; do
    ALL_DRIVE_READY="True"

    for new_drive_slot in ${!drive_status_per_slot[@]}; do
        if [ ${drive_status_per_slot[${new_drive_slot}]} != "Ready" ]; then
            ps ${drive_status_per_slot[${new_drive_slot}]} > /dev/null
            if [ "$?" != "0" ]; then
                wait ${drive_status_per_slot[${new_drive_slot}]}
                if [ "$?" == "0" ]; then
                    drive_status_per_slot[${new_drive_slot}]="Ready"
                else
                    echo "Drive ${new_drive_slot} fails"
                    exit 1
                fi
            else
                ALL_DRIVE_READY="False"
            fi
        fi

        if [ ${drive_status_per_slot[${new_drive_slot}]} == "Ready" ]; then
            if (( ${#tape_array[@]} > 0 )); then
                new_tape="${tape_array[0]}"
                tape_array=( ${tape_array[@]:1} )
                mtx_status_load_status_unload ${new_drive_slot} "${new_tape}" &
                drive_status_per_slot[${new_drive_slot}]="$!"
            else
                NO_MORE_TAPE="True"
            fi
        fi
    done

    if [ ${ALL_DRIVE_READY} == "False" ]; then
        sleep ${WAITING_DRIVE_NB_SECOND}
    fi
done
