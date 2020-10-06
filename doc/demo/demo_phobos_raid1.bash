#!/bin/bash

# This script shows raid1 layout features offered by phobos as robustness facing
# media failures and flexibility facing nearly full media.
# It must be run on a node ready to execute a phobos server.
# WARNING : the postgres phobos database is dropped.

READER=5

echo "################################"
echo "# Phobos RAID1 layout quick demo"
echo "################################"
sleep $READER
echo
echo "# Clean DB"
sleep $READER
echo "sudo -u postgres phobos_db drop_db || true"
sudo -u postgres phobos_db drop_db || true
sleep $READER
echo "sudo -u postgres phobos_db setup_db -s -p phobos"
sudo -u postgres phobos_db setup_db -s -p phobos
sleep $READER
echo
echo "# Start phobos daemon"
echo "systemctl start phobosd"
systemctl start phobosd
sleep $READER
echo
echo "################################"
echo "# First test : raid1 resilience"
echo "# - store 2 copies of an object on two different media"
echo "# - retrieve this object even if one medium is unavailable"
echo "################################"
sleep $READER
echo
echo "# Create a file as input object"
echo "echo \"Input object example\" > /tmp/input_object"
echo "Input object example" > /tmp/input_object
sleep $READER
echo
echo "# Add a first directory to phobos ressource"
echo "mkdir /tmp/dir1"
mkdir /tmp/dir1
sleep $READER
echo "phobos dir add /tmp/dir1"
phobos dir add /tmp/dir1
sleep $READER
echo "phobos dir format --fs posix --unlock /tmp/dir1"
phobos dir format --fs posix --unlock /tmp/dir1
sleep $READER
echo "phobos dir list -o name,adm_status"
phobos dir list -o name,adm_status
sleep $READER
echo
echo "# Store input_object using simple layout"
echo "phobos put -f dir -l simple /tmp/input_object simple_layout_object"
phobos put -f dir -l simple /tmp/input_object simple_layout_object
sleep $READER
echo
echo "# Add a second directory to phobos ressource"
echo "mkdir /tmp/dir2"
mkdir /tmp/dir2
sleep $READER
echo "phobos dir add /tmp/dir2"
phobos dir add /tmp/dir2
sleep $READER
echo "phobos dir format --fs posix --unlock /tmp/dir2"
phobos dir format --fs posix --unlock /tmp/dir2
sleep $READER
echo "phobos dir list -o name,adm_status"
phobos dir list -o name,adm_status
sleep $READER
echo
echo "# Set raid1 layout replicat count to 2"
echo "export PHOBOS_LAYOUT_RAID1_repl_count=2"
export PHOBOS_LAYOUT_RAID1_repl_count=2
sleep $READER
echo
echo "# Store input_object using raid1 layout"
echo "phobos put -f dir -l raid1 /tmp/input_object raid1_2replica_object"
phobos put -f dir -l raid1 /tmp/input_object raid1_2replica_object
sleep $READER
echo
echo "# List new objects extents"
echo "phobos extent list -o oid,media_name,layout"
phobos extent list -o oid,media_name,layout
sleep $READER
echo
echo "# Get back simple and raid1 object"
echo "phobos get simple_layout_object /tmp/simple_back"
phobos get simple_layout_object /tmp/simple_object_back
echo "phobos get raid1_2replica_object /tmp/raid1_back"
phobos get raid1_2replica_object /tmp/raid1_back
sleep $READER
echo
echo "# Simulate a failure on first medium"
echo "phobos dir lock /tmp/dir1"
phobos dir lock /tmp/dir1
echo "phobos dir list -o name,adm_status"
phobos dir list -o name,adm_status
sleep $READER
echo
echo "# Due to first medium failure, simple layout object is unavailable."
echo "phobos get simple_layout_object /tmp/simple_back_with_failure"
phobos get simple_layout_object /tmp/simple_object_back_with_failure
sleep $READER
echo
echo "# Get raid1 layout object even if first medium is failed."
echo "phobos get raid1_2replica_object /tmp/raid1_back_failure"
phobos get raid1_2replica_object /tmp/raid1_back_failure
sleep $READER
echo
echo "# Lock media"
echo
echo "phobos dir lock /tmp/dir1 /tmp/dir2"
phobos dir lock /tmp/dir1 /tmp/dir2
sleep $READER


echo
echo "################################"
echo "# Second test : raid1 management of media allocation"
echo "# - store 2 copies of an object on three different media"
echo "# - first medium is wide enough to store one copy of the object"
echo "# - second and third media are smaller and can't store the object"
echo "# - the second copy is splitted between second and third media"
echo "# - retrieve this object even if one medium is unavailable"
sleep $READER
sleep $READER
sleep $READER
echo
echo "# Create first medium of 128M"
sleep $READER
echo
echo "dd if=/dev/zero of=/tmp/128M_file bs=1M count=128"
dd if=/dev/zero of=/tmp/128M_device_file bs=1M count=128
echo
echo "losetup -f /tmp/128M_device_file"
losetup -f /tmp/128M_device_file
echo
echo "mkfs.ext4 /dev/loop0"
mkfs.ext4 /dev/loop0
echo
echo "mkdir /tmp/128M_dir"
mkdir /tmp/128M_dir
echo
echo "mount /dev/loop0 /tmp/128M_dir"
mount /dev/loop0 /tmp/128M_dir
sleep $READER
echo
echo "phobos dir add /tmp/128M_dir"
phobos dir add /tmp/128M_dir
echo
echo "phobos dir format --fs posix --unlock /tmp/128M_dir"
phobos dir format --fs posix --unlock /tmp/128M_dir
sleep $READER
echo
echo "# Create second and third medium of 64M each"
sleep $READER
echo
echo "dd if=/dev/zero of=/tmp/64M_file_1 bs=1M count=64"
dd if=/dev/zero of=/tmp/64M_file_1 bs=1M count=64
echo "dd if=/dev/zero of=/tmp/64M_file_2 bs=1M count=64"
dd if=/dev/zero of=/tmp/64M_file_2 bs=1M count=64
echo
echo "losetup -f /tmp/64M_file_1"
losetup -f /tmp/64M_file_1
echo "losetup -f /tmp/64M_file_2"
losetup -f /tmp/64M_file_2
echo
echo "mkfs.ext4 /dev/loop1"
mkfs.ext4 /dev/loop1
echo "mkfs.ext4 /dev/loop2"
mkfs.ext4 /dev/loop2
echo
echo "mkdir /tmp/64M_dir_1"
mkdir /tmp/64M_dir_1
echo "mkdir /tmp/64M_dir_2"
mkdir /tmp/64M_dir_2
echo
echo "mount /dev/loop1 /tmp/64M_dir_1"
mount /dev/loop1 /tmp/64M_dir_1
echo "mount /dev/loop2 /tmp/64M_dir_2"
mount /dev/loop2 /tmp/64M_dir_2
sleep $READER
echo
echo "phobos dir add /tmp/64M_dir_1 /tmp/64M_dir_2"
phobos dir add /tmp/64M_dir_1 /tmp/64M_dir_2
echo
echo "phobos dir format --fs posix --unlock /tmp/64M_dir_1 /tmp/64M_dir_2"
phobos dir format --fs posix --unlock /tmp/64M_dir_1 /tmp/64M_dir_2
echo
echo "phobos dir list -o name,adm_status,stats.phys_spc_free"
phobos dir list -o name,adm_status,stats.phys_spc_free
sleep $READER
echo
echo "# Create a 100M file as input object"
sleep $READER
echo
echo "dd if=/dev/urandom of=/tmp/100M_input_object bs=1M count=100"
dd if=/dev/urandom of=/tmp/100M_input_object bs=1M count=100
sleep $READER
echo
echo "# Store the 100M file using raid1 layout"
echo
echo "phobos put -f dir -l raid1 /tmp/100M_input_object 100M_raid1_object"
phobos put -f dir -l raid1 /tmp/100M_input_object 100M_raid1_object
sleep $READER
echo
echo "phobos object list -o all"
phobos object list -o all
echo
echo "phobos extent list -o oid,layout,media_name,size"
phobos extent list -o oid,layout,media_name,size
sleep $READER
echo
echo "# Get back object"
echo "phobos get 100M_raid1_object /tmp/100M_raid1_back"
phobos get 100M_raid1_object /tmp/100M_raid1_back
sleep $READER
echo
echo "# Simulate a failure of 128M medium"
echo "phobos dir lock /tmp/128M_dir"
phobos dir lock /tmp/128M_dir
echo "phobos dir list -o name,adm_status,stats.phys_spc_free"
phobos dir list -o name,adm_status,stats.phys_spc_free
sleep $READER
echo
echo "# Get back object under failure"
echo "phobos get 100M_raid1_object /tmp/100M_raid1_back_failure"
phobos get 100M_raid1_object /tmp/100M_raid1_back_failure
sleep $READER
echo
echo "# One more failure"
echo "phobos dir lock /tmp/64M_dir_1"
phobos dir lock /tmp/64M_dir_1
echo "phobos dir list -o name,adm_status,stats.phys_spc_free"
phobos dir list -o name,adm_status,stats.phys_spc_free
sleep $READER
echo
echo "# No miracle"
echo
echo "phobos get 100M_raid1_object /tmp/100M_raid1_back_miracle"
phobos get 100M_raid1_object /tmp/100M_raid1_back_miracle
sleep $READER

echo
echo "# End test cleaning"
set -x
systemctl stop phobosd
rm -rf /tmp/dir1 /tmp/dir2
rm /tmp/input_object
rm /tmp/simple_object_back /tmp/raid1_back
rm /tmp/raid1_back_failure
umount /tmp/128M_dir
umount /tmp/64M_dir_1
umount /tmp/64M_dir_2
rm -rf /tmp/128M_dir /tmp/64M_dir_1 /tmp/64M_dir_2
losetup -D
rm /tmp/100M_input_object
rm /tmp/100M_raid1_back
rm /tmp/100M_raid1_back_failure
sudo -u postgres phobos_db drop_db || true
