/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2026 CEA/DAM.
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
 * \brief Implementation of hsm_common.h
 */

#define _GNU_SOURCE /*asprintf*/
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "pho_cfg.h"
#include "pho_common.h"

#include "hsm_common.h"

/** Definition and default values of HSM configuration parameters */
const struct pho_config_item cfg_hsm[] = {
    [PHO_CFG_HSM_synced_ctime_path] = {
        .section = "hsm",
        .name    = "synced_ctime_path",
        .value   = "/var/lib/phobos/hsm_synced_ctime_source_destination",
    },
    [PHO_CFG_HSM_sync_delay_second] = {
        .section = "hsm",
        .name    = "sync_delay_second",
        .value   = "0",
    },
    [PHO_CFG_HSM_release_delay_second] = {
        .section = "hsm",
        .name    = "release_delay_second",
        .value   = "0",
    },
    [PHO_CFG_HSM_dir_release_higher_threshold] = {
        .section = "hsm",
        .name    = "dir_release_higher_threshold",
        .value   = "95",
    },
    [PHO_CFG_HSM_dir_release_lower_threshold] = {
        .section = "hsm",
        .name    = "dir_release_lower_threshold",
        .value   = "80",
    },
    [PHO_CFG_HSM_error_log_path] = {
        .section = "hsm",
        .name    = "error_log_path",
        .value   = "/var/lib/phobos/hsm_error",
    },
};

int open_error_log_file(const char *hsm_cfg_section_name, FILE **error_log_file)
{
    const char *error_log_path = NULL;
    int rc;

    rc = pho_cfg_get_val(hsm_cfg_section_name,
                         cfg_hsm[PHO_CFG_HSM_error_log_path].name,
                         &error_log_path);
    if (rc == -ENODATA) {
        pho_warn("No error_log_path value in config section '%s', we get "
                 "default value '%s'", hsm_cfg_section_name,
                 cfg_hsm[PHO_CFG_HSM_error_log_path].value);
        error_log_path = cfg_hsm[PHO_CFG_HSM_error_log_path].value;
    } else if (rc) {
        LOG_RETURN(-EINVAL,
                   "Unable to get error_log_path for config section '%s'",
                   hsm_cfg_section_name);
    }

    *error_log_file = fopen(error_log_path, "a");
    if (!*error_log_file)
        LOG_RETURN(rc = -errno,
                   "Error when opening error log file %s", error_log_path);

    return 0;
}

void hsm_log_error(enum hsm_type type, int rc, const struct object_info *obj,
                   const struct hsm_params *params)
{
    char timestamp[20];
    int local_rc;
    time_t now;

    if (!params->error_log_file) {
        pho_warn("HSM %s ERROR %d (%s) (oid: '%s', uuid: '%s', version: '%d') "
                 "(source: '%s', destination: '%s')\n",
                 hsm_type2str(type), -rc, strerror(-rc),
                 obj->oid, obj->uuid, obj->version,
                 params->source_copy_name, params->destination_copy_name);

        return;
    }

    now = time(NULL);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S",
             localtime(&now));
    local_rc = fprintf(params->error_log_file,
                       "[%s] %s ERROR %d (%s) "
                       "(oid: '%s', uuid: '%s', version: '%d') "
                       "(source: '%s', destination: '%s')\n",
                       timestamp, hsm_type2str(type), -rc, strerror(-rc),
                       obj->oid, obj->uuid, obj->version,
                       params->source_copy_name, params->destination_copy_name);

    if (local_rc)
        pho_warn("Unable to log HSM %s ERROR %d (%s)"
                 "(oid: '%s', uuid: '%s', version: '%d') "
                 "(source: '%s', destination: '%s') : %d (%s)",
                 hsm_type2str(type), -rc, strerror(-rc),
                 obj->oid, obj->uuid, obj->version,
                 params->source_copy_name, params->destination_copy_name,
                 local_rc, strerror(local_rc));
}

int hsm_write_candidate(enum hsm_type type, const struct object_info *object,
                         const struct hsm_params *params)
{
    if (printf("%s \"%s\" \"%s\" \"%d\" \"%s\"\n",
               type == HSM_SYNC ? "CREATE" : "DELETE",
               object->oid, object->uuid, object->version,
               type == HSM_SYNC ? params->destination_copy_name :
                                  params->source_copy_name) < 0)
        LOG_RETURN(-EIO, "Unable to write hsm : %s \"%s\" \"%s\" \"%d\" \"%s\"",
                   type == HSM_SYNC ? "CREATE" : "DELETE",
                   object->oid, object->uuid, object->version,
                   type == HSM_SYNC ? params->destination_copy_name :
                                      params->source_copy_name);
    else
        return 0;
}
