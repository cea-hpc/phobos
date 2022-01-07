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
#ifndef _PHO_LRS_CFG_H
#define _PHO_LRS_CFG_H

#include "pho_cfg.h"
#include "pho_types.h"

/** List of LRS configuration parameters */
enum pho_cfg_params_lrs {
    PHO_CFG_LRS_FIRST,

    /* lrs parameters */
    PHO_CFG_LRS_mount_prefix = PHO_CFG_LRS_FIRST,
    PHO_CFG_LRS_policy,
    PHO_CFG_LRS_families,
    PHO_CFG_LRS_lib_device,
    PHO_CFG_LRS_server_socket,
    PHO_CFG_LRS_lock_file,
    PHO_CFG_LRS_sync_time_threshold,
    PHO_CFG_LRS_sync_nb_req_threshold,

    PHO_CFG_LRS_LAST
};

extern const struct pho_config_item cfg_lrs[];

/**
 * Getter of time threshold value for a given family.
 *
 * @param[in]   family      Targeted family.
 * @param[out]  threshold   Returned threshold value.
 * @return                  0 on success,
 *                         -errno on failure.
 */
int get_cfg_time_threshold_value(enum rsc_family family,
                                 struct timespec *threshold);

/**
 * Getter of number of requests threshold value for a given family.
 *
 * @param[in]   family      Targeted family.
 * @param[out]  threshold   Returned threshold value.
 * @return                  0 on success,
 *                         -errno on failure.
 */
int get_cfg_nb_req_threshold_value(enum rsc_family family,
                                   unsigned int *threshold);

#endif
