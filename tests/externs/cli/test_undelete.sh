#!/bin/bash

#
#  All rights reserved (c) 2014-2022 CEA/DAM.
#
#  This file is part of Phobos.
#
#  Phobos is free software: you can redistribute it and/or modify it under
#  the terms of the GNU Lesser General Public License as published by
#  the Free Software Foundation, either version 2.1 of the License, or
#  (at your option) any later version.
#
#  Phobos is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU Lesser General Public License for more details.
#
#  You should have received a copy of the GNU Lesser General Public License
#  along with Phobos. If not, see <http://www.gnu.org/licenses/>.
#

#
# Integration test for undeletion feature
#

test_dir=$(dirname $(readlink -e $0))
. $test_dir/../../test_env.sh
. $test_dir/../../setup_db.sh
. $test_dir/../../test_launch_daemon.sh

set -xe

function dir_setup
{
    export dirs="$(mktemp -d /tmp/test.pho.XXXX)"
    echo "adding directories $dirs"
    $phobos dir add $dirs
    $phobos dir format --fs posix --unlock $dirs
}

function setup
{
    setup_tables
    invoke_daemon
    dir_setup
}

function cleanup
{
    waive_daemon
    rm -rf $dirs
    drop_tables
}

function test_undelete
{
    echo "**** TEST UNDELETE BY UUID ****"
    $phobos put --family dir /etc/hosts oid1 || error "Object should be put"
    $phobos delete oid1 || error "Object should be deleted"

    uuid=$($phobos object list --deprecated --output uuid oid1)
    $valg_phobos undelete uuid $uuid ||
        error "Object should be undeleted without any error"
    $phobos get oid1 test_tmp || error "Object should be got after undeletion"
    rm test_tmp

    [ -z $($phobos object list oid1) ] &&
        error "Object should be listed, because it is undeleted"

    # TODO : return an error to user when no action is done
    #$phobos undelete uuid "unexisting-fantasy-uuid" &&
    #    error "Undeleting an unexisting uuid should failed"

    echo "**** TEST UNDELETE BY OID ****"
    $phobos put --family dir /etc/hosts oid2 || error "Object should be put"
    $phobos delete oid2 || error "Object should be deleted"

    $valg_phobos undelete oid oid2 ||
        error "Object should be undeleted without any error"
    $phobos get oid2 test_tmp || error "Object should be got after undeletion"
    rm test_tmp

    [ -z $($phobos object list oid2) ] &&
        error "Object should be listed, because it is undeleted"

    # TODO : return an error to user when no action is done
    #$phobos undelete oid "unexisting-fantasy-oid" &&
    #    error "Undeleting an unexisting oid should failed"
    #
    #$phobos put --family dir /etc/hosts oid3 ||
    #    error "Object should be put"
    #$phobos delete oid3 ||
    #    error "Object should be deleted"
    #$phobos put --family dir /etc/hosts oid3 ||
    #    error "Object should be put"
    #$phobos delete oid3 ||
    #    error "Object should be deleted"
    #$phobos undelete oid oid3 &&
    #    error "Undeleting an oid with two deprecated uuid should failed"

    return 0
}

trap cleanup EXIT
setup

test_undelete
