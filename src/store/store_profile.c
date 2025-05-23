/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2024 CEA/DAM.
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
 * \brief  profile specific implementation of Phobos store
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "store_profile.h"

#include "phobos_store.h"
#include "pho_cfg.h"
#include "pho_common.h"
#include "pho_type_utils.h"

#include <stdlib.h>

#define PROFILE_SECTION_CFG "profile \"%s\""
#define PROFILE_FAMILY_CFG_PARAM "family"
#define PROFILE_LAYOUT_CFG_PARAM "layout"
#define PROFILE_LYT_PARAMS_CFG_PARAM "lyt-params"
#define PROFILE_TAGS_CFG_PARAM "tags"
#define PROFILE_LIBRARY_CFG_PARAM "library"

#define COPY_SECTION_CFG "copy \"%s\""
#define COPY_PROFILE_CFG_PARAM "profile"

/**
 * List of configuration parameters for profile store
 */
enum pho_cfg_params_store_profile {
    PHO_CFG_STORE_FIRST,

    /* store parameters */
    PHO_CFG_STORE_default_layout = PHO_CFG_STORE_FIRST,
    PHO_CFG_STORE_default_profile,
    PHO_CFG_STORE_default_family,

    PHO_CFG_STORE_LAST
};

const struct pho_config_item cfg_store_profile[] = {
    [PHO_CFG_STORE_default_layout] = {
        .section = "store",
        .name    = "default_layout",
        .value   = "raid1"
    },
    [PHO_CFG_STORE_default_profile] = {
        .section = "store",
        .name    = "default_profile",
        .value   = NULL
    },
    [PHO_CFG_STORE_default_family] = {
        .section = "store",
        .name    = "default_family",
        .value   = "tape"
    },
};

static int apply_defaults_to_put_params(struct pho_xfer_put_params *put_params,
                                        int lvl)
{
    const char *cfg_val;
    int rc = 0;

    if (put_params->layout_name == NULL) {
        rc = pho_cfg_get_val_from_level(
            cfg_store_profile[PHO_CFG_STORE_default_layout].section,
            cfg_store_profile[PHO_CFG_STORE_default_layout].name, lvl,
            &cfg_val);
        if (rc == 0)
            put_params->layout_name = cfg_val;
        else if (rc != -ENODATA)
            return rc;
    }

    if (put_params->family == PHO_RSC_INVAL) {
        rc = pho_cfg_get_val_from_level(
            cfg_store_profile[PHO_CFG_STORE_default_family].section,
            cfg_store_profile[PHO_CFG_STORE_default_family].name, lvl,
            &cfg_val);
        if (rc == 0)
            put_params->family = str2rsc_family(cfg_val);
        else if (rc != -ENODATA)
            goto out;
    }

    if (put_params->profile == NULL) {
        rc = pho_cfg_get_val_from_level(
            cfg_store_profile[PHO_CFG_STORE_default_profile].section,
            cfg_store_profile[PHO_CFG_STORE_default_profile].name, lvl,
            &cfg_val);
        if (rc == 0)
            put_params->profile = cfg_val;
        else if (rc != -ENODATA)
            return rc;
    }

out:
    return rc;
}

static int set_lyt_params(char *section_name, struct pho_attrs *attrs)
{
    const char *separators = " =,";
    const char *cfg_val;
    char *str_cpy;
    char *saveptr;
    char *value;
    char *key;
    int rc;

    rc = pho_cfg_get_val_from_level(section_name, PROFILE_LYT_PARAMS_CFG_PARAM,
                                    PHO_CFG_LEVEL_LOCAL, &cfg_val);
    if (!rc && cfg_val != NULL) {
        str_cpy = xstrdup(cfg_val);

        for (key = strtok_r(str_cpy, separators, &saveptr);
             key != NULL;
             key = strtok_r(NULL, separators, &saveptr)) {
            value = strtok_r(NULL, separators, &saveptr);
            if (value == NULL)
                LOG_GOTO(out, rc = -EINVAL, "Missing value for key '%s' "
                                            "in %s", key, cfg_val);

            pho_attr_set(attrs, key, value);
        }
out:
        free(str_cpy);
    }

    return rc;
}

/**
 * Extract the values of the specified profile from the config and set the
 * parameters of xfer.
 * Family and layout are only applied if not formerly set, tags are joined
 *
 * @param[in] put_params the phobos xfer put parameters read out and apply the
 *                       profile
 *
 * @return 0 on success, negative errro in case of failure
 */
static int apply_profile_to_put_params(struct pho_xfer_put_params *put_params)
{
    const char *cfg_val;
    char *section_name;
    int rc;

    rc = asprintf(&section_name, PROFILE_SECTION_CFG,
                  put_params->profile);
    if (rc < 0)
        return -ENOMEM;

    // family
    if (put_params->family == PHO_RSC_INVAL) {
        rc = pho_cfg_get_val(section_name, PROFILE_FAMILY_CFG_PARAM, &cfg_val);
        if (!rc)
            put_params->family = str2rsc_family(cfg_val);
        else if (rc != -ENODATA)
            goto out;
    }

    // layout
    if (put_params->layout_name == NULL) {
        rc = pho_cfg_get_val(section_name, PROFILE_LAYOUT_CFG_PARAM, &cfg_val);
        if (!rc)
            put_params->layout_name = cfg_val;
        else if (rc != -ENODATA)
            goto out;

        if (pho_attrs_is_empty(&put_params->lyt_params)) {
            rc = set_lyt_params(section_name, &put_params->lyt_params);
            if (rc != 0 && rc != -ENODATA)
                goto out;
        }
    }

    // tags
    rc = pho_cfg_get_val(section_name, PROFILE_TAGS_CFG_PARAM, &cfg_val);
    if (rc == 0)
        str2string_array(cfg_val, &put_params->tags);
    else if (rc != -ENODATA)
        goto out;

    // library
    if (put_params->library == NULL) {
        rc = pho_cfg_get_val(section_name, PROFILE_LIBRARY_CFG_PARAM, &cfg_val);
        if (!rc)
            put_params->library = cfg_val;
        else if (rc != -ENODATA)
            goto out;
    }

    free(section_name);
    return 0;

out:
    free(section_name);
    return rc;
}

/**
 * Extract the values of the specified profile for a copy name from the config
 * and set the parameters of xfer.
 *
 * @param[in] put_params the phobos xfer put parameters
 *
 * @return 0 on success, negative errro in case of failure
 */
static int apply_copy_to_put_params(struct pho_xfer_put_params *put_params)
{
    const char *cfg_val;
    char *section_name;
    int rc = 0;

    rc = asprintf(&section_name, COPY_SECTION_CFG, put_params->copy_name);
    if (rc < 0)
        return -ENOMEM;

    if (put_params->profile == NULL) {
        rc = pho_cfg_get_val(section_name, COPY_PROFILE_CFG_PARAM, &cfg_val);
        if (!rc)
            put_params->profile = cfg_val;
        else if (rc != -ENODATA)
            goto out;
    }

    free(section_name);
    return 0;

out:
    free(section_name);
    return rc;
}

/** Return the (configured) default resource family. */
static enum rsc_family default_family_from_cfg(void)
{
    const char *fam_str;

    fam_str = PHO_CFG_GET(cfg_store_profile, PHO_CFG_STORE, default_family);
    if (fam_str == NULL)
        return PHO_RSC_INVAL;

    return str2rsc_family(fam_str);
}

int fill_put_params(struct pho_xfer_desc *xfer)
{
    struct pho_xfer_put_params *put_params;
    enum pho_cfg_level lvl;
    int rc = 0;

    if (xfer->xd_op == PHO_XFER_OP_COPY)
        put_params = &xfer->xd_params.copy.put;
    else
        put_params = &xfer->xd_params.put;

    for (lvl = PHO_CFG_LEVEL_PROCESS; lvl < PHO_CFG_LEVEL_LAST; ++lvl) {
        if (put_params->copy_name != NULL) {
            rc = apply_copy_to_put_params(put_params);
            if (rc != 0)
                return rc;
        }

        rc = apply_defaults_to_put_params(put_params, lvl);
        if (rc != 0 && rc != -ENODATA)
            return rc;

        /* get information of profile from cfg if specified */
        if (put_params->profile != NULL) {
            rc = apply_profile_to_put_params(put_params);
            if (rc != 0)
                return rc;
        }
    }

    /* Fill profile or default values into put params if not set prelimilarly */
    if (put_params->family == PHO_RSC_INVAL)
        put_params->family = default_family_from_cfg();

    if (put_params->layout_name == NULL)
        put_params->layout_name =
            cfg_store_profile[PHO_CFG_STORE_default_layout].value;

    return 0;
}
