/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2023 CEA/DAM.
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
 * \brief  Phobos Tape Library Controler configuration utilities.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "pho_comm.h"
#include "tlc_cfg.h"

const struct pho_config_item cfg_tlc[] = {
    /* listen_hostname/hostname and port are failover settings:
     * if listen_hostname is not set, hostame is used
     */
    [PHO_CFG_TLC_listen_hostname] = {
        .section = "tlc",
        .name = "listen_hostname",
        .value = NULL
    },
    [PHO_CFG_TLC_hostname] = TLC_HOSTNAME_CFG_ITEM,
    [PHO_CFG_TLC_listen_port] = {
        .section = "tlc",
        .name = "listen_port",
        .value = NULL
    },
    [PHO_CFG_TLC_port] = TLC_PORT_CFG_ITEM,
    [PHO_CFG_TLC_lib_device] = {
        .section = "tlc",
        .name    = "lib_device",
        .value   = "/dev/changer"
    },
    [PHO_CFG_TLC_default_library] = {
        .section = "store",
        .name    = "default_tape_library",
        .value   = NULL
    },
};
