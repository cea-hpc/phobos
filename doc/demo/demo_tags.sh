#!/bin/sh

# This script demonstrates phobos tags feature by using various phobos commands.
# It must be executed on a ready to run phobos server with a phobos database
# which is already set.
# (TO BE DONE: delete from phobos all added media, device and objects.)

# WARNING: this script stops the phobs daemon.

echo "# START DEMO"
echo "# Start Phobos dameon"
echo "systemctl start phobosd"
systemctl start phobosd
sleep 5
echo

echo "# Add tape drives"
echo "phobos drive add --unlock /dev/st0 /dev/st4"
phobos drive add --unlock /dev/st0 /dev/st4
sleep 5
echo "phobos drive list --output name,model"
phobos drive list --output name,model
sleep 10
echo

echo "# Add tapes"
echo "phobos tape add --type lto5 P0000[0-1]L5"
phobos tape add --type lto5 P0000[0-1]L5
sleep 5
echo "phobos tape add --type lto6 --tags lto6 Q0000[0-1]L6"
phobos tape add --type lto6 --tags lto6 Q0000[0-1]L6
sleep 10
echo

echo "# Format tapes"
echo "phobos tape format --unlock P0000[0-1]L5 Q0000[0-1]L6"
phobos tape format --unlock P0000[0-1]L5 Q0000[0-1]L6
sleep 10
echo

echo "# Add an object on LTO6 tapes"
echo "phobos put --tags lto6 /etc/hosts obj_id1"
phobos put --tags lto6 /etc/hosts obj_id1
sleep 5
echo "phobos tape list --output name,tags,fs.status,stats.logc_spc_used"
phobos tape list --output name,tags,fs.status,stats.logc_spc_used
sleep 10
echo

echo "# Update tape tags"
echo "phobos tape update --tags archive P00001L5"
phobos tape update --tags archive P00001L5
sleep 5
echo

echo "# Add an object on 'archive' tape"
echo "phobos put --tags archive /etc/fstab obj_id2"
phobos put --tags archive /etc/fstab obj_id2
sleep 5
echo "phobos tape list --output name,tags,fs.status,stats.logc_spc_used"
phobos tape list --output name,tags,fs.status,stats.logc_spc_used
sleep 10
echo

# TO BE DONE: delete from phobos all added media, device and objects.

systemctl stop phobosd
echo "# END DEMO"

