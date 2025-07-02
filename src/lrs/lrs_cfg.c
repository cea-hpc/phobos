/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2025 CEA/DAM.
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
#include "pho_cfg.h"
#include "pho_common.h"

#include <stdlib.h>
#include <string.h>

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
        .value   = "tape,dir,rados_pool"
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
    [PHO_CFG_LRS_sync_time_ms] = {
        .section = "lrs",
        .name    = "sync_time_ms",
        .value   = "tape=10000,dir=10,rados_pool=10"
    },
    [PHO_CFG_LRS_sync_nb_req] = {
        .section = "lrs",
        .name    = "sync_nb_req",
        .value   = "tape=5,dir=5,rados_pool=5"
    },
    [PHO_CFG_LRS_sync_wsize_kb] = {
        .section = "lrs",
        .name    = "sync_wsize_kb",
        .value   = "tape=1048576,dir=1048576,rados_pool=1048576"
    },
    [PHO_CFG_LRS_max_health] = {
        .section = "lrs",
        .name    = "max_health",
        .value   = "1",
    },
    [PHO_CFG_LRS_fifo_max_write_per_grouping] = {
        .section = "lrs",
        .name    = "fifo_max_write_per_grouping",
        .value   = "0",
    },
    [PHO_CFG_LRS_locate_lock_expirancy] = {
        .section = "lrs",
        .name    = "locate_lock_expirancy",
        .value   = "300000",
    },
};

static int _get_unsigned_long_from_string(const char *value,
                                          unsigned long min_limit,
                                          unsigned long max_limit,
                                          unsigned long *ul_value)
{
    char *endptr;

    if (value[0] == '-')
        return -ERANGE;

    *ul_value = strtoul(value, &endptr, 10);
    if (*endptr != '\0' || *endptr == *value)
        return -EINVAL;

    if (*ul_value < min_limit)
        return -ERANGE;

    if (max_limit == ULONG_MAX && *ul_value == ULONG_MAX && errno != 0)
        return -errno;
    else if (*ul_value > max_limit)
        return -ERANGE;

    return 0;
}

int get_cfg_sync_time_ms_value(enum rsc_family family,
                               struct timespec *threshold)
{
    unsigned long num_milliseconds;
    char *value;
    int rc;

    rc = PHO_CFG_GET_SUBSTRING_VALUE(cfg_lrs, PHO_CFG_LRS, sync_time_ms,
                                     family, &value);
    if (rc)
        return rc;

    rc = _get_unsigned_long_from_string(value, 0, ULONG_MAX, &num_milliseconds);
    free(value);
    if (rc)
        return rc;

    threshold->tv_sec = num_milliseconds / 1000;
    threshold->tv_nsec = (num_milliseconds % 1000) * 1000000;

    return 0;
}

int get_cfg_sync_nb_req_value(enum rsc_family family, unsigned int *threshold)
{
    unsigned long ul_value;
    char *value;
    int rc;

    rc = PHO_CFG_GET_SUBSTRING_VALUE(cfg_lrs, PHO_CFG_LRS, sync_nb_req,
                                     family, &value);
    if (rc)
        return rc;

    rc = _get_unsigned_long_from_string(value, 1, UINT_MAX, &ul_value);
    free(value);
    if (rc)
        return rc;

    *threshold = ul_value;

    return 0;
}

int get_cfg_sync_wsize_value(enum rsc_family family, unsigned long *threshold)
{
    char *value;
    int rc;

    rc = PHO_CFG_GET_SUBSTRING_VALUE(cfg_lrs, PHO_CFG_LRS, sync_wsize_kb,
                                     family, &value);
    if (rc)
        return rc;

    rc = _get_unsigned_long_from_string(value, 1, ULONG_MAX / 1024, threshold);
    free(value);
    if (rc)
        return rc;

    *threshold *= 1024; // converting from KiB to bytes

    return 0;
}
