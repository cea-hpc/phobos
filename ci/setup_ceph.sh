#!/bin/bash

# This script sets up a simple Ceph cluster for phobos CI tests.
#
# It needs at least 4 VMs which already have Ceph installed. A main VM must be
# able to connect to the other VMs via SSH.
#
# Executing this script will create a Ceph Monitor and a Ceph Manager on the
# main VM and 1 OSD daemon on each of the others VMs.
#
# (This script was tested with Ceph 15.2 rpms for rel7)

set -xe

cluster_name=ceph
ceph_conf=/etc/ceph/ceph.conf
fsid=$(uuidgen)
hostname=$(hostname)
ip_address=$(hostname -i)
disk_size_MiB=150 #150 MiB for each OSD
# Approximate size in MB
disk_size_MB=$(printf "%.0f" \
        $(awk -v x=$disk_size_MiB 'BEGIN { print x*1.05 }'))
# Username to connect to other VMs from the main VM
user_for_ssh=ceph_conf
# Secondary VM hostnames
secondary_vms_hosts=(
vm1
vm2
vm3
)
ceph_storage=/tmp/ceph_storage

# Fill conf file
sudo cat << EOF > ceph.conf
[global]
fsid = $fsid
mon initial members = $hostname
mon host = $ip_address
public network = 10.200.0.0/24
auth cluster required = cephx
auth service required = cephx
auth client required = cephx
osd journal size = 1024
osd pool default size = 3
osd pool default min size = 2
osd pool default pg num = 8
osd pool default pgp num = 8
osd crush chooseleaf type = 1
EOF

sudo mv ceph.conf $ceph_conf

# Create a keyring for your cluster and generate a monitor secret key
sudo ceph-authtool --create-keyring /tmp/ceph.mon.keyring --gen-key -n mon. \
--cap mon 'allow *'

# Generate an administrator keyring, generate a client.admin user and add the
# user to the keyring
sudo ceph-authtool --create-keyring /etc/ceph/ceph.client.admin.keyring \
--gen-key -n client.admin --cap mon 'allow *' --cap osd 'allow *' --cap mds \
'allow *' --cap mgr 'allow *'

# Generate a bootstrap-osd keyring, generate a client.bootstrap-osd user and
# add the user to the keyring.
sudo ceph-authtool --create-keyring /var/lib/ceph/bootstrap-osd/ceph.keyring \
--gen-key -n client.bootstrap-osd --cap mon 'profile bootstrap-osd' --cap mgr \
'allow r'

# Add the generated keys to the ceph.mon.keyring
sudo ceph-authtool /tmp/ceph.mon.keyring --import-keyring \
/etc/ceph/ceph.client.admin.keyring
sudo ceph-authtool /tmp/ceph.mon.keyring --import-keyring \
/var/lib/ceph/bootstrap-osd/ceph.keyring

# Change the owner for ceph.mon.keyring
sudo chown ceph:ceph /tmp/ceph.mon.keyring

# Generate a monitor map using the hostname(s), host IP address(es) and the
# FSID. Save it as /tmp/monmap:
monmaptool --create --add $hostname $ip_address --fsid $fsid /tmp/monmap \
--clobber

# Create a default data directory (or directories) on the monitor host(s)
sudo rm -rf /var/lib/ceph/mon/$cluster_name-$hostname
sudo mkdir /var/lib/ceph/mon/$cluster_name-$hostname
sudo chown ceph:ceph /var/lib/ceph/mon/$cluster_name-$hostname

# Populate the monitor daemon(s) with the monitor map and keyring
sudo -u ceph ceph-mon --mkfs -i $hostname \
--monmap /tmp/monmap --keyring /tmp/ceph.mon.keyring

# Populate the monitor daemon(s) with the monitor map and keyring
sudo -u ceph ceph-mon --mkfs -i $hostname \
        --monmap /tmp/monmap --keyring /tmp/ceph.mon.keyring

# Start the monitor service with systemd
sudo systemctl stop ceph-mon@$hostname
sudo systemctl start ceph-mon@$hostname

# Enable msgr2
sudo ceph mon enable-msgr2

# Add manager keyring
sudo mkdir /var/lib/ceph/mgr/$cluster_name-$hostname

sudo ceph auth get-or-create mgr.$hostname mon 'allow profile mgr' \
        osd 'allow *' mds 'allow *' | sudo tee \
        /var/lib/ceph/mgr/$cluster_name-$hostname/keyring \ >/dev/null

# Start manager
sudo ceph-mgr -i $hostname

# Disable the insecure mode
sudo ceph config set mon auth_allow_insecure_global_id_reclaim false

# Allow pool delete
sudo ceph config set mon mon_allow_pool_delete true

files_to_tranfer=(
$ceph_conf
/var/lib/ceph/bootstrap-osd/ceph.keyring
)

# Add one Bluestore OSD daemon per VM
for vm in "${secondary_vms_hosts[@]}"
do
    # Set up a virtual disk
    # 1- Create image
    # 2- Mount loop device: you should make sure /dev/loop0 is not taken
    # 3- Create Physical Volume
    # 4- Create Volume Group
    # 5- Create Logical Volume
    sudo -u $user_for_ssh ssh -t $vm \
        "sudo dd if=/dev/zero of=$ceph_storage bs=1M count=$disk_size_MB ; " \
        "sudo losetup /dev/loop0 $ceph_storage ; " \
        "sudo pvcreate /dev/loop0 ; " \
        "sudo vgcreate vg_osd /dev/loop0 ; " \
        "sudo lvcreate -L $disk_size_MiB -n lv_osd vg_osd ; "

    # Copy Ceph files
    for p in "${files_to_tranfer[@]}"; do
    sudo cp $p /tmp/tmpfile
    sudo -u $user_for_ssh scp /tmp/tmpfile $vm:/tmp/tmpfile
    sudo -u $user_for_ssh ssh -t $vm "sudo cp /tmp/tmpfile $p"
    done

    # Create OSD
    sudo -u $user_for_ssh ssh -t $vm "sudo ceph-volume lvm create " \
                                     "--data /dev/vg_osd/lv_osd"
done

# Check Ceph status with a 60s timeout
start=$SECONDS
duration=$(( SECONDS - start ))
while [ $duration -le 60 ]
do
    output=$(sudo ceph -s)
    if [[ "$output" == *"active+clean"* && "$output" == *"osd: 3 osds: 3 up"* \
        && "$output" == *"3 in"* && "$output" == *"HEALTH_OK"* ]]
    then
        echo "$output"
        break
    fi
    duration=$(( SECONDS - start ))
done
