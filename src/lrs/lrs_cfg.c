/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2017 CEA/DAM.
 *
 *  This file is part of Phobos.
 *
 *  Phobos is free software: you can redistribute it and/or modify it under
 *  the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation, either version 2.1 of the License, or
 *  (at your option) any later version.
 *
 *  Phobos is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with Phobos. If not, see <http://www.gnu.org/licenses/>.
 */
/**
 * \brief  Phobos Local Resource Scheduler configuration utilities.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "lrs_cfg.h"

const struct pho_config_item cfg_lrs[] = {
    [PHO_CFG_LRS_mount_prefix] = {
        .section = "lrs",
        .name    = "mount_prefix",
        .value   = "/mnt/phobos-"
    },
    [PHO_CFG_LRS_policy] = {
        .section = "lrs",
        .name    = "policy",
        .value   = "best_fit"
    },
    [PHO_CFG_LRS_families] = {
        .section = "lrs",
        .name    = "families",
        .value   = "tape,dir"
    },
    [PHO_CFG_LRS_lib_device] = {
        .section = "lrs",
        .name    = "lib_device",
        .value   = "/dev/changer"
    },
    [PHO_CFG_LRS_server_socket] = {
        .section = "lrs",
        .name    = "server_socket",
        .value   = "/run/phobosd/lrs"
    },
    [PHO_CFG_LRS_lock_file] = {
        .section = "lrs",
        .name    = "lock_file",
        .value   = "/run/phobosd/phobosd.lock"
    },
};
