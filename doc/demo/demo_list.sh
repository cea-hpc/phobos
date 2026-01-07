#!/usr/bin/env bash

# This script must be run on a phobos admin node.
# This script shows some of the list features offered by phobos by executing
# the following steps:
# - starts the phobos daemon,
# - creates and add 3 directories to phobos,
# - executes various list commands,
# - stops the phobos daemon and deletes the 3 directories.
# (TO BE DONE: delete the 3 added directories from phobos)

echo "# START DEMO"
echo "# Start Phobos daemon"
echo "systemctl start phobosd"
systemctl start phobosd
sleep 5
echo

echo "# Add directories, format some of them, and unlock one"
mkdir /tmp/aries /tmp/taurus /tmp/gemini
echo "phobos dir add /tmp/aries /tmp/taurus /tmp/gemini"
phobos dir add /tmp/aries /tmp/taurus /tmp/gemini
sleep 5
echo "phobos dir format --fs posix /tmp/aries /tmp/gemini"
phobos dir format --fs posix /tmp/aries /tmp/gemini
sleep 5
echo "phobos dir unlock /tmp/aries"
phobos dir unlock /tmp/aries
sleep 10
echo

echo "# List directories (default behavior)"
echo "phobos dir list"
phobos dir list
sleep 10
echo

echo "# Detailed list"
echo "phobos dir list --output all"
phobos dir list --output all
sleep 10
echo

echo "# Customized list"
echo "phobos dir list --output name,path,adm_status,fs.status"
phobos dir list --output name,adm_status,fs.status
sleep 10
echo

echo "# Other list formats"
echo "phobos dir list --output name,path,adm_status,fs.status --format csv"
phobos dir list --output name,adm_status,fs.status --format csv
sleep 5
echo "phobos dir list --output name,path,adm_status,fs.status --format json"
phobos dir list --output name,adm_status,fs.status --format json
sleep 5
echo "phobos dir list --output name,path,adm_status,fs.status --format yaml"
phobos dir list --output name,adm_status,fs.status --format yaml
sleep 5
echo "phobos dir list --output name,path,adm_status,fs.status --format xml"
phobos dir list --output name,adm_status,fs.status --format xml
sleep 10
echo

# TO BE DONE: delete 3 added directories from phobos.

systemctl stop phobosd
rm -r /tmp/aries /tmp/taurus /tmp/gemini
echo "# END DEMO"
