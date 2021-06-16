#!/bin/sh

function noisy_cmd
{
    echo "$*" >&2
    sh -c "$*"
}

echo "# Starting the daemon"
noisy_cmd systemctl start phobosd

phobos_db drop_tables
echo "# Setup database"
noisy_cmd phobos_db setup_tables
echo

rm -rf /tmp/test_get
echo "# Create testing directory"
noisy_cmd mkdir -p /tmp/test_get
echo

echo "# Add directory to phobos"
noisy_cmd phobos dir add /tmp/test_get
noisy_cmd phobos dir format --unlock --fs posix /tmp/test_get
echo

echo "# Create an object to get"
noisy_cmd phobos put --family dir --metadata "a=b" /etc/hosts oid1
echo

echo "# Get the uuid of the current generation"
uuid1=$(noisy_cmd phobos object list -o uuid oid1)
echo "-> $uuid1"
echo

echo "# Get an alive object"
noisy_cmd phobos get oid1 /tmp/out && rm /tmp/out
noisy_cmd phobos get --version 1 oid1 /tmp/out && rm /tmp/out
noisy_cmd phobos get --uuid "$uuid1" oid1 /tmp/out && rm /tmp/out
echo

echo "# Overwrite the current object to create a new version"
noisy_cmd phobos put --family dir --overwrite --metadata "b=c" /etc/hosts oid1
echo

echo "# Get the alive version"
noisy_cmd phobos get oid1 /tmp/out && rm /tmp/out
echo

echo "# Get the deprecated version"
noisy_cmd phobos get --version 1 oid1 /tmp/out && rm /tmp/out
echo

echo "# Get the most recent version"
noisy_cmd phobos get --uuid $uuid1 oid1 /tmp/out && rm /tmp/out
echo

echo "# Delete the object"
noisy_cmd phobos delete oid1
echo

echo "# The get with just oid fails as it only targets alive objects"
noisy_cmd phobos get oid1 /tmp/out && rm /tmp/out
echo

echo "# But the deprecated objects can still be accessed"
noisy_cmd phobos get --version 1 oid1 /tmp/out && rm /tmp/out
noisy_cmd phobos get --uuid $uuid1 oid1 /tmp/out && rm /tmp/out
echo

echo "# Create a new generation by reusing the last oid"
noisy_cmd phobos put --family dir --metadata "c=d" /etc/hosts oid1
echo

echo "# Get the uuid of the current generation"
uuid2=$(noisy_cmd phobos object list -o uuid oid1)
echo "-> $uuid2"
echo

echo "# Now we can target the old generation with its uuid"
noisy_cmd phobos get --uuid $uuid1 oid1 /tmp/out && rm /tmp/out
echo

echo "# Create a new version of the current object"
noisy_cmd phobos put --overwrite --family dir /etc/hosts oid1
echo

echo "# Target the current generation's old version"
noisy_cmd phobos get --version 1 oid1 /tmp/out && rm /tmp/out
echo

echo "# Target the old generation"
noisy_cmd phobos get --uuid $uuid1 oid1 /tmp/out && rm /tmp/out
echo
