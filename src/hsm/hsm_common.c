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
#include <glib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "pho_attrs.h"
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


static char *str2base64(const char *str)
{
    const char *base64_chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const char *current_char = str;
    size_t nb_char = strlen(str);
    char *current_char_base64;
    char *str_base64;

    str_base64 = xcalloc((nb_char + 2) / 3 * 4 + 1, 1);
    current_char_base64 = str_base64;
    while (nb_char > 0) {
        /*
         *current_char :         [xxxxxx  xx] [xxxx  xxxx] [xx  xxxxxx]
         *current_char_base_64 : [xxxxxx][xx   xxxx][xxxx   xx][xxxxxx]
         */
        current_char_base64[0] = base64_chars[current_char[0] >> 2];

        if (nb_char > 1)
            current_char_base64[1] =
                base64_chars[((current_char[0] & 0x03) << 4) +
                             (current_char[1] >> 4)];
        else
            current_char_base64[1] =
                base64_chars[(current_char[0] & 0x03) << 4];

        if (nb_char > 2)
            current_char_base64[2] =
                base64_chars[((current_char[1] & 0x0f) << 2) +
                             (current_char[2] >> 6)];
        else if (nb_char > 1)
            current_char_base64[2] =
                base64_chars[(current_char[1] & 0x0f) << 2];
        else
            current_char_base64[2] = '=';

        if (nb_char > 2)
            current_char_base64[3] = base64_chars[current_char[2] & 0x3f];
        else
            current_char_base64[3] = '=';

        if (nb_char < 3)
            nb_char = 0;
        else
            nb_char -= 3;

        current_char += 3;
        current_char_base64 += 4;
    }

    return str_base64;
}

static void add_metadata(GString *metadata_str, const struct object_info *obj,
                         const struct hsm_params *params)
{
    struct pho_attrs obj_md = {0};
    int rc, i;

    rc = pho_json_to_attrs(&obj_md, obj->user_md);
    if (rc) {
        pho_warn("Error when parsing metadata '%s' of object "
                 "(oid: '%s', uuid: '%s', version: '%d') : %d (%s)",
                 obj->user_md, obj->oid, obj->uuid, obj->version,
                 -rc, strerror(-rc));
        return;
    }

    for (i = 0; i < params->wanted_keys.count; i++) {
        const char *value = pho_attr_get(&obj_md,
                                         params->wanted_keys.strings[i]);

        if (value) {
            if (params->base64)
                value = str2base64(value);

            g_string_append_printf(metadata_str, " \"%s\"=\"%s\"",
                                   params->wanted_keys.strings[i], value);
            if (params->base64)
                free((void *)value);
        }
    }

    pho_attrs_free(&obj_md);
}

int hsm_write_candidate(enum hsm_type type, const struct object_info *object,
                         const struct hsm_params *params)
{
    GString *metadata_str = g_string_new("");
    int rc = 0;

    if (params->grouping && object->grouping)
        g_string_append_printf(metadata_str, " \"grouping\"=\"%s\"",
                               object->grouping);

    if (params->wanted_keys.count > 0 && object->user_md)
        add_metadata(metadata_str, object, params);

    if (printf("%s \"%s\" \"%s\" \"%d\" \"%s\"%s\n",
               type == HSM_SYNC ? "CREATE" : "DELETE",
               object->oid, object->uuid, object->version,
               type == HSM_SYNC ? params->destination_copy_name :
                                  params->source_copy_name,
               metadata_str->str) < 0)
        LOG_GOTO(clean, rc = -EIO,
                 "Unable to write hsm : %s \"%s\" \"%s\" \"%d\" \"%s\"",
                 type == HSM_SYNC ? "CREATE" : "DELETE",
                 object->oid, object->uuid, object->version,
                 type == HSM_SYNC ? params->destination_copy_name :
                                    params->source_copy_name);

clean:
    g_string_free(metadata_str, true);
    return rc;
}
