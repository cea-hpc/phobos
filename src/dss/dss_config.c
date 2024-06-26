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
 * \brief  Configuration file of Phobos's Distributed State Service.
 */

#include <assert.h>
#include <gmodule.h>
#include <stdlib.h>
#include <strings.h>

#include "pho_cfg.h"
#include "pho_common.h"
#include "pho_dss.h"

#include "dss_config.h"

/** List of configuration parameters for DSS */
enum pho_cfg_params_dss {
    /* DSS parameters */
    PHO_CFG_DSS_connect_string,

    /* Delimiters, update when modifying options */
    PHO_CFG_DSS_FIRST = PHO_CFG_DSS_connect_string,
    PHO_CFG_DSS_LAST  = PHO_CFG_DSS_connect_string,
};

const struct pho_config_item cfg_dss[] = {
    [PHO_CFG_DSS_connect_string] = {
        .section = "dss",
        .name    = "connect_string",
        .value   = "dbname=phobos host=localhost"
    },
};

/* This config item is mutualized with lrs_device.c */
const struct pho_config_item cfg_tape_model[] = {
    [PHO_CFG_TAPE_MODEL_supported_list] = {
        .section = "tape_model",
        .name    = "supported_list",
        .value   = "LTO5,LTO6,LTO7,LTO8,LTO9,T10KB,T10KC,T10KD"
    },
};

/* init by parse_supported_tape_models function called at config init */
static GPtrArray *supported_tape_models;

__attribute__((destructor))
static void _destroy_supported_tape_models(void)
{
    if (supported_tape_models)
        g_ptr_array_free(supported_tape_models, TRUE);
}

int parse_supported_tape_models(void)
{
    const char *config_list;
    char *parsed_config_list;
    char *saved_ptr;
    char *conf_model;
    GPtrArray *built_supported_tape_models;

    if (supported_tape_models)
        return -EALREADY;

    /* get tape supported model from conf */
    config_list = PHO_CFG_GET(cfg_tape_model, PHO_CFG_TAPE_MODEL,
                              supported_list);
    if (!config_list)
        LOG_RETURN(-EINVAL, "no supported_list tape model found in config");

    /* duplicate supported model to parse it */
    parsed_config_list = xstrdup(config_list);

    /* allocate built_supported_tape_models */
    built_supported_tape_models = g_ptr_array_new_with_free_func(free);
    if (!built_supported_tape_models) {
        free(parsed_config_list);
        LOG_RETURN(-ENOMEM, "Error on allocating built_supported_tape_models");
    }

    /* parse model list */
    for (conf_model = strtok_r(parsed_config_list, ",", &saved_ptr);
         conf_model;
         conf_model = strtok_r(NULL, ",", &saved_ptr)) {
        char *new_model;

        /* dup tape model */
        new_model = xstrdup(conf_model);

        /* store tape model */
        g_ptr_array_add(built_supported_tape_models, new_model);
    }

    free(parsed_config_list);
    supported_tape_models = built_supported_tape_models;
    return 0;
}

bool dss_tape_model_check(const char *model)
{
    int index;

    assert(model);
    assert(supported_tape_models);

    /* if found return true as success value */
    for (index = 0; index < supported_tape_models->len; index++)
        if (strcasecmp(g_ptr_array_index(supported_tape_models, index),
                       model) == 0)
            return true;

    /* not found : return false */
    return false;
}

const char *get_connection_string(void)
{
    return PHO_CFG_GET(cfg_dss, PHO_CFG_DSS, connect_string);
}
