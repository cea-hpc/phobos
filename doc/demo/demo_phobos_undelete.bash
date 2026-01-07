#!/usr/bin/env bash

# This script shows the undelete feature offered by phobos.
# It must be run on a node ready to execute a phobos server.
# WARNING : the postgres phobos database is dropped.

READER=5

echo "################################"
echo "# Phobos undelete quick demo"
echo "################################"
sleep $READER
echo
echo "# Clean DB"
sleep $READER
echo "sudo -u postgres phobos_db drop_db"
sudo -u postgres phobos_db drop_db
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
echo "# Add a directory as backend to phobos"
echo "media_dir=\$(mktemp -d /tmp/test.pho.XXXX)"
media_dir=$(mktemp -d /tmp/test.pho.XXXX)
echo "phobos dir add \$media_dir"
phobos dir add $media_dir
echo "phobos dir format --fs posix --unlock \$media_dir"
phobos dir format --fs posix --unlock $media_dir
sleep $READER
echo
echo "**** TEST UNDELETE BY UUID ****"
echo "# Put an object as oid1"
echo "phobos put --family dir /etc/hosts oid1"
phobos put --family dir /etc/hosts oid1
echo "phobos object list --output oid"
phobos object list --output oid
echo "phobos object list --deprecated --output oid,uuid,version"
phobos object list --deprecated --output oid,uuid,version
echo
sleep $READER
echo "# Delete this object"
echo "phobos delete oid1"
phobos delete oid1
echo "phobos object list --output oid"
phobos object list --output oid
echo "phobos object list --deprecated --output oid,uuid,version"
phobos object list --deprecated --output oid,uuid,version
echo
sleep $READER
echo "# can not get the object by oid"
echo "phobos get oid1 test_tmp"
phobos get oid1 test_tmp
echo
sleep $READER
echo "# get the uuid of the deleted object"
echo "uuid=\$(phobos object list --deprecated --output uuid oid1)"
uuid=$(phobos object list --deprecated --output uuid oid1)
echo
sleep $READER
echo "# undelete by uuid"
echo "phobos undelete uuid \$uuid"
phobos undelete uuid $uuid
echo "phobos object list --output oid"
phobos object list --output oid
echo "phobos object list --deprecated --output oid,uuid,version"
phobos object list --deprecated --output oid,uuid,version
echo
sleep $READER
echo "# get the undeleted object by oid"
echo "phobos get oid1 test_tmp"
phobos get oid1 test_tmp
echo
sleep $READER
echo "# clean test_tmp"
echo "rm test_tmp"
rm test_tmp
echo
echo "**** TEST UNDELETE BY OID ****"
echo
sleep $READER
echo "# Put an object as oid2"
echo "phobos put --family dir /etc/hosts oid2"
phobos put --family dir /etc/hosts oid2
echo "# Delete oid2"
echo "phobos delete oid2"
phobos delete oid2
echo
sleep $READER
echo "# can not get the object by oid"
echo "phobos get oid2 test_tmp"
phobos get oid2 test_tmp
echo
sleep $READER
echo "# undelete oid2 by oid"
echo "phobos undelete oid oid2"
phobos undelete oid oid2
echo
sleep $READER
echo "# get undelete oid2 object"
echo "phobos get oid2 test_tmp"
phobos get oid2 test_tmp
echo
sleep $READER
echo "# clean test_tmp"
echo "rm test_tmp"
rm test_tmp
echo
echo "**** TEST UNDELETE ERROR IF THE OID ALREADY EXISTS ****"
echo
sleep $READER
echo "# Put an object as oid3"
echo "phobos put --family dir /etc/hosts oid3"
phobos put --family dir /etc/hosts oid3
echo "# Delete oid3"
echo "phobos delete oid3"
phobos delete oid3
echo "# Put a new object as oid3"
echo "phobos put --family dir /etc/hosts oid3"
phobos put --family dir /etc/hosts oid3
echo
sleep $READER
echo "phobos object list --output oid"
phobos object list --output oid
echo "phobos object list --deprecated --output oid,uuid,version"
phobos object list --deprecated --output oid,uuid,version
echo
sleep $READER
echo "# get the uuid of the deleted object"
echo "uuid=\$(phobos object list --deprecated --output uuid oid3)"
uuid=$(phobos object list --deprecated --output uuid oid1)
echo
sleep $READER
echo "# try to undelete oid3 by oid: but this OID is already existing"
echo "phobos undelete oid oid3"
phobos undelete oid oid3
echo
sleep $READER
echo "# delete the new oid3"
echo "phobos delete oid3"
phobos delete oid3
echo "phobos object list --output oid"
phobos object list --output oid
echo "phobos object list --deprecated --output oid,uuid,version"
phobos object list --deprecated --output oid,uuid,version
echo
sleep $READER
#echo "**** TEST UNDELETE ERROR BY OID IF THERE IS SEVERAL DEPRECATED UUID ****"
echo "# try to undelete oid3 by oid : but this OID has two existing uuids"
echo "phobos undelete oid oid3"
phobos undelete oid oid3
echo "# There is no error, but the object is not undeleted and it is still deprecated"
echo "# We need to add an error message to warn the user."
echo
sleep $READER
echo "phobos object list --output oid"
phobos object list --output oid
echo "phobos object list --deprecated --output oid,uuid,version"
phobos object list --deprecated --output oid,uuid,version
echo
sleep $READER
echo "**** TEST UNDELETE SUCCESS BY UUID IF THERE IS SEVERAL DEPRECATED UUID ****"
echo "# undelete oid3 by uuid"
echo "phobos undelete uuid \$uuid"
phobos undelete uuid $uuid
echo "phobos object list --output oid"
phobos object list --output oid
echo "phobos object list --deprecated --output oid,uuid,version"
phobos object list --deprecated --output oid,uuid,version
echo
sleep $READER
echo
echo
echo "# End test cleaning"
set -x
rm -rf $media_dir
systemctl stop phobosd
sudo -u postgres phobos_db drop_db || true
