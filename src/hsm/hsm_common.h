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
 * \brief Phobos hsm common stuff
 */

#ifndef _HSM_COMMON_H
#define _HSM_COMMON_H

/** List of HSM configuration parameters */
enum pho_cfg_params_hsm {
    /* File path to store the already sync copy ctime */
    PHO_CFG_HSM_synced_ctime_path,
    PHO_CFG_HSM_sync_delay_second,
    PHO_CFG_HSM_release_delay_second,
    PHO_CFG_HSM_dir_release_higher_threshold,
    PHO_CFG_HSM_dir_release_lower_threshold,
    PHO_CFG_HSM_error_log_path,

    /* Delimiters, update when modifying options */
    PHO_CFG_HSM_FIRST = PHO_CFG_HSM_synced_ctime_path,
    PHO_CFG_HSM_LAST  = PHO_CFG_HSM_error_log_path,
};

extern const struct pho_config_item cfg_hsm[];

#define HSM_SECTION_CFG "hsm \"%s\" \"%s\""

#define CTIME_STRING_LENGTH 26

enum hsm_type {
    HSM_INVAL = -1,
    HSM_SYNC = 0,
    HSM_RELEASE = 1,
    HSM_LAST = 2,
};

static const char * const hsm_type_names[] = {
    [HSM_SYNC] = "SYNC",
    [HSM_RELEASE] = "RELEASE",
};

static inline const char *hsm_type2str(enum hsm_type type)
{
    if (type >= HSM_LAST || type < 0)
        return NULL;
    return hsm_type_names[type];
}

struct hsm_params {
    const char *source_copy_name;
    const char *destination_copy_name;
    bool dry_run;
    int log_level;
    bool grouping;
    FILE *error_log_file;
};

#define DEFAULT_HSM_PARAMS {NULL, NULL, false, PHO_LOG_INFO, false, NULL}

/*
 * Open and return the FILE * of the hsm log error file corresponding to
 * the hsm_cfg_section_name.
 */
int open_error_log_file(const char *hsm_cfg_section_name,
                        FILE **error_log_file);

/*
 * Write one error line in the hsm log error file.
 */
void hsm_log_error(enum hsm_type type, int rc,
                   const char *oid, const char *object_uuid, int version,
                   const struct hsm_params *params);
#endif /* _HSM_COMMON_H */
