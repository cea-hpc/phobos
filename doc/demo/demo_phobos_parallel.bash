#!/usr/bin/env bash

# This scripts shows how phobos could be used in parallel on two servers by
# executing the following steps:
# - create a file on each server,
# - put the two files into phobos in parallel by asking separately each server
# to put its own file into phobos,
# - getting back the two file in parallel by asking separately each server to
# to get back its own file,
# - focus on the need of a future "relocate" call by asking to server1 to get
# back the file put on server0,
# - deletes files on each server.
# (TO BE DONE: delete example files from phobos)

# This scripts needs a running phobos on two servers sharing the same phobos
# database. The two phobos servers need dir devices ready to store a 1k file.
# This script must be run on a node able to connect by ssh to the both servers.

SERVER0="${SERVER0}"
SERVER1="${SERVER1}"

READER=5

echo "# 2 IO servers in parallel"
echo "# both connected to the same phobos system"
echo "# metadata are shared through a common db"
sleep $READER
sleep $READER
sleep $READER
echo
echo "# First phobos IO server : ${SERVER0}"
sleep $READER
echo
echo "ssh ${SERVER0} sudo head -3 /etc/phobos.conf"
ssh ${SERVER0} sudo head -3 /etc/phobos.conf
sleep $READER
echo
echo "ssh ${SERVER0} sudo phobos dir list"
ssh ${SERVER0} sudo phobos dir list
sleep $READER
echo
echo "# Second phobos IO server : ${SERVER1}"
sleep $READER
echo
echo "ssh ${SERVER1} sudo head -3 /etc/phobos.conf"
ssh ${SERVER1} sudo head -3 /etc/phobos.conf
sleep $READER
echo
echo "ssh ${SERVER1} phobos dir list"
ssh ${SERVER1} sudo phobos dir list
sleep $READER
echo
echo "# Prepare on both an input object"
sleep $READER
sleep $READER
echo
echo "ssh ${SERVER0} sudo dd if=/dev/urandom of=/tmp/${SERVER0}_file bs=1k count=1"
ssh ${SERVER0} sudo dd if=/dev/urandom of=/tmp/${SERVER0}_file bs=1k count=1
echo "ssh ${SERVER1} sudo dd if=/dev/urandom of=/tmp/${SERVER1}_file bs=1k count=1"
ssh ${SERVER1} sudo dd if=/dev/urandom of=/tmp/${SERVER1}_file bs=1k count=1
sleep $READER
echo
echo "# Put simultaneously the two objects"
echo "ssh ${SERVER0} sudo phobos put -f dir /tmp/${SERVER0}_file ${SERVER0}_object &"
echo "ssh ${SERVER1} sudo phobos put -f dir /tmp/${SERVER1}_file ${SERVER1}_object"
ssh ${SERVER0} sudo phobos put -f dir /tmp/${SERVER0}_file ${SERVER0}_object &
ssh ${SERVER1} sudo phobos put -f dir /tmp/${SERVER1}_file ${SERVER1}_object
sleep $READER
sleep $READER
echo
echo "# List object from both IO servers"
echo "ssh ${SERVER0} sudo phobos extent list -o oid,media_name"
ssh ${SERVER0} sudo phobos extent list -o oid,media_name
echo
echo "ssh ${SERVER1} sudo phobos extent list -o oid,media_name"
ssh ${SERVER1} sudo phobos extent list -o oid,media_name
sleep $READER
sleep $READER
echo
echo "# Get back simultaneously the two objects"
echo "ssh ${SERVER0} sudo phobos get ${SERVER0}_object /tmp/${SERVER0}_object_back &"
echo "ssh ${SERVER1} sudo phobos get ${SERVER1}_object /tmp/${SERVER1}_object_back"
ssh ${SERVER0} sudo phobos get ${SERVER0}_object /tmp/${SERVER0}_object_back &
ssh ${SERVER1} sudo phobos get ${SERVER1}_object /tmp/${SERVER1}_object_back
sleep $READER
sleep $READER
echo
echo "# Next phasis : implement relocate call between IO servers"
echo "# (currently : ${SERVER1} has no access to ${SERVER0} data)"
sleep $READER
sleep $READER
echo "ssh ${SERVER1} sudo phobos get ${SERVER0}_object /tmp/${SERVER0}_object_back"
ssh ${SERVER1} sudo phobos get ${SERVER0}_object /tmp/${SERVER0}_object_back
sleep $READER
sleep $READER


echo
echo "# Clean"
set -x
ssh ${SERVER0} sudo rm /tmp/${SERVER0}_file
ssh ${SERVER0} sudo rm /tmp/${SERVER0}_object_back
ssh ${SERVER1} sudo rm /tmp/${SERVER1}_file
ssh ${SERVER1} sudo rm /tmp/${SERVER1}_object_back
# TO BE DONE: delete files from phobos
