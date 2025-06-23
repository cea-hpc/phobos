/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2022 CEA/DAM.
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
 * \brief  Phobos configuration management.
 *
 * For more details see doc/design/config.txt.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "pho_cfg.h"
#include "pho_common.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <ini_config.h>

/** XXX if this is used one day, it must be in a global context, not a global
 * variable as it needs to be shared between modules
 * (cf. struct phobos_global_context).
 *
 * Note that a new per-thread context will have to be created for this.
 */
/** thread-wide handle to DSS */
static __thread void *thr_dss_hdl;

static inline bool config_is_loaded(void)
{
    return phobos_context()->config.cfg_items != NULL;
}

/** load a local config file */
static int pho_cfg_load_file(const char *cfg)
{
    struct collection_item *errors = NULL;
    struct config *config;
    int rc;

    MUTEX_LOCK(&phobos_context()->config.lock);

    config = &phobos_context()->config;

    /* Make sure that the config was not loaded by another thread */
    if (config->cfg_items != NULL)
        GOTO(unlock, rc = 0);

    rc = config_from_file("phobos", cfg, &config->cfg_items,
                          INI_STOP_ON_ERROR, &errors);

    if (rc == 0) {
        config->cfg_file = cfg;
    } else {
        /* libini returns positive errno-like error codes */
        if (rc == ENOENT && strcmp(cfg, PHO_DEFAULT_CFG) == 0) {
            pho_warn("no configuration file at default location: %s", cfg);
            rc = 0;
        } else {
            pho_error(rc, "failed to read configuration file '%s'", cfg);
            print_file_parsing_errors(stderr, errors);
            fprintf(stderr, "\n");
            free_ini_config(config->cfg_items);
        }
    }

    /* The error collection always has to be freed, even when empty */
    free_ini_config_errors(errors);
unlock:
    MUTEX_UNLOCK(&config->lock);

    return -rc;
}

/**
 * Initialize access to local config parameters (process-wide and host-wide).
 * This is basically called before the DSS is initialized.
 * This is NOT thread safe and must be called before any call to other
 * pho_cfg_*() * functions.
 * @param[in] cfg_file force path to configuration file.
 *                     If cfg_file is NULL, get env(PHOBOS_CFG_FILE).
 *                     If this last is NULL, use the default path
 *                     ('/etc/phobos.conf').
 */
int pho_cfg_init_local(const char *config_file)
{
    const char *cfg = config_file;

    if (config_is_loaded())
        return -EALREADY;

    if (cfg == NULL)
        cfg = getenv("PHOBOS_CFG_FILE");

    if (cfg == NULL)
        cfg = PHO_DEFAULT_CFG;

    pho_verb("Loading config %s", cfg);

    return pho_cfg_load_file(cfg);
}

void pho_cfg_local_fini(void)
{
    if (!config_is_loaded())
        return;

    MUTEX_LOCK(&phobos_context()->config.lock);
    free_ini_config(phobos_context()->config.cfg_items);
    phobos_context()->config.cfg_items = NULL;
    MUTEX_UNLOCK(&phobos_context()->config.lock);
}

/**
 * Allow access to global config parameters for the current thread.
 * This can only be called after the DSS is initialized.
 */
int pho_cfg_set_thread_conn(void *dss_handle)
{
    if (dss_handle == NULL)
        return -EINVAL;

    thr_dss_hdl = dss_handle;
    return 0;
}

/** Build environment variable name for a given section and parameter name:
 * PHOBOS_<section(upper case)>_<param_name(lower case)>.
 * @param[in]  section   section name of the configuration item.
 * @param[in]  name      name of the configuration parameter.
 * @param[out] env       string allocated by the function that contains the
 *                       environement variable name. It must be free()'d by the
 *                       caller.
 */
static void build_env_name(const char *section, const char *name, char **env)
{
    char  *env_var, *curr;
    size_t strsize;

    /* sizeof returns length + 1, which makes room for first '_'.
     * Add 2 for 2nd '_' and final '\0'
     */
    strsize = sizeof(PHO_ENV_PREFIX) + strlen(section) + strlen(name) + 2;
    env_var = xmalloc(strsize);

    /* copy prefix (strcpy is safe as the xmalloc() is properly sized) */
    strcpy(env_var, PHO_ENV_PREFIX"_");
    curr = end_of_string(env_var);

    /* copy and upper case section */
    strcpy(curr, section);
    upperstr(curr);
    curr = end_of_string(curr);
    *curr = '_';
    curr++;

    /* copy and lower case parameter */
    strcpy(curr, name);
    lowerstr(curr);

    *env = env_var;
}

/**
 * Get process-wide configuration parameter from environment.
 * @retval 0 on success
 * @retval -ENODATA if the parameter in not defined.
 * @retval other negative error code on failure.
 */
static int pho_cfg_get_env(const char *section, const char *name,
                           const char **value)
{
    char *env, *val;

    build_env_name(section, name, &env);

    val = getenv(env);
    pho_debug("environment: %s=%s", env, val ? val : "<NULL>");
    free(env);

    if (val == NULL)
        return -ENODATA;

    *value = val;
    return 0;
}

int pho_cfg_set_val_local(const char *section, const char *name,
                          const char *value)
{
    char *env;
    int rc;

    build_env_name(section, name, &env);

    rc = setenv(env, value, 1); /* 1 for overwrite */
    free(env);
    if (rc)
        return -errno;

    return 0;
}

/**
 * Get host-wide configuration parameter from config file.
 * @retval 0 on success
 * @retval -ENODATA if the parameter in not defined.
 * @retval other negative error code on failure.
 */
static int pho_cfg_get_local(const char *section, const char *name,
                             const char **value)
{
    struct collection_item *item;
    int rc;

    MUTEX_LOCK(&phobos_context()->config.lock);
    rc = get_config_item(section, name,
                         phobos_context()->config.cfg_items, &item);
    MUTEX_UNLOCK(&phobos_context()->config.lock);
    if (rc)
        return -rc;

    *value = get_const_string_config_value(item, &rc);
    pho_debug("config file: %s::%s=%s", section, name,
              *value ? *value : "<NULL>");
    /* ini_config sets rc=EINVAL if the parameter is not found. */
    return ((*value == NULL) ? -ENODATA : 0);
}

/**
 * Get global configuration parameter from DSS.
 * @retval 0 on success
 * @retval -ENODATA if the parameter in not defined.
 * @retval other negative error code on failure.
 */
static int pho_cfg_get_global(const char *section, const char *name,
                              const char **value)
{
    /** @TODO get value from DSS (not in phobos v0) */
    return -ENOTSUP;
}

int pho_cfg_get_val_from_level(const char *section, const char *name,
                               enum pho_cfg_level lvl, const char **value)
{
    switch (lvl) {
    case PHO_CFG_LEVEL_PROCESS:
        /* from environment */
        return pho_cfg_get_env(section, name, value);

    case PHO_CFG_LEVEL_LOCAL:
        /* if config file has not been loaded */
        if (!config_is_loaded())
            return -ENODATA;

        return pho_cfg_get_local(section, name, value);

    case PHO_CFG_LEVEL_GLOBAL:
        /* if connection is not set */
        if (thr_dss_hdl == NULL)
            return -ENODATA;

        return pho_cfg_get_global(section, name, value);

    default:
        return -EINVAL;
    }
}

static size_t count_char(const char *s, char c)
{
    size_t n = 0;

    for (; *s != '\0'; s++)
        if (*s == c)
            n++;

    return n;
}

void get_val_csv(const char *csv_value, char ***values, size_t *n)
{
    char *csv_value_dup;
    size_t nb_values;
    char *saveptr;
    char **array;
    char **iter;
    char *token;

    *n = 0;
    *values = NULL;

    csv_value_dup = xstrdup(csv_value);

    nb_values = count_char(csv_value, ',');
    array = xmalloc(sizeof(*array) * (nb_values + 1));

    iter = array;
    for (token = strtok_r(csv_value_dup, ",", &saveptr);
         token != NULL;
         token = strtok_r(NULL, ",", &saveptr)) {

        *iter = xstrdup(token);

        iter++;
        (*n)++;
    }

    *values = array;

    free(csv_value_dup);
}

int pho_cfg_get_val(const char *section, const char *name, const char **value)
{
    int rc;

    /* 1) check process-wide parameter */
    rc = pho_cfg_get_val_from_level(section, name, PHO_CFG_LEVEL_PROCESS,
                                    value);
    if (rc != -ENODATA)
        return rc;

    /* 2) check host-wide parameter */
    rc = pho_cfg_get_val_from_level(section, name, PHO_CFG_LEVEL_LOCAL, value);
    if (rc != -ENODATA)
        return rc;

    /* 3) check global parameter */
    rc = pho_cfg_get_val_from_level(section, name, PHO_CFG_LEVEL_GLOBAL, value);
    if (rc != -ENODATA)
        return rc;

    return -ENODATA;
}

const char *_pho_cfg_get(int first_index, int last_index, int param_index,
                         const struct pho_config_item *module_params)
{
    const struct pho_config_item    *item;
    const char                      *res;
    int                              rc;

    if (param_index > last_index || param_index < first_index)
        return NULL;

    item = &module_params[param_index];

    /* sanity check (in case of sparse pho_config_descr array) */
    if (!item->name)
        return NULL;

    rc = pho_cfg_get_val(item->section, item->name, &res);
    if (rc == -ENODATA)
        res = item->value;

    return res;
}

int _pho_cfg_get_int(int first_index, int last_index, int param_index,
                     const struct pho_config_item *module_params,
                     int fail_val)
{
    const char *opt;
    int64_t     val;

    opt = _pho_cfg_get(first_index, last_index, param_index, module_params);
    if (opt == NULL) {
        pho_debug("Failed to retrieve config parameter #%d", param_index);
        return fail_val;
    }

    val = str2int64(opt);
    if (val == LLONG_MIN || val < INT_MIN || val > INT_MAX) {
        pho_warn("Invalid value for parameter #%d: '%s' (integer expected)",
                 param_index, opt);
        return fail_val;
    }

    return val;
}

bool _pho_cfg_get_bool(int first_index, int last_index, int param_index,
                       const struct pho_config_item *module_params,
                       bool default_val)
{
    const char *value;

    value = _pho_cfg_get(first_index, last_index, param_index, module_params);
    if (!value) {
        pho_debug("Failed to retrieve config parameter #%d", param_index);
        return default_val;
    }

    if (!strcmp(value, "true"))
        return true;
    else if (!strcmp(value, "false"))
        return false;
    else
        return default_val;
}

int _pho_cfg_get_substring_value(int first_index, int last_index,
                                 int param_index,
                                 const struct pho_config_item *module_params,
                                 enum rsc_family family, char **substring)
{
    const char *cfg_val;
    char *token_dup;
    char *save_ptr;
    char *key;
    int rc;

    cfg_val = _pho_cfg_get(first_index, last_index, param_index, module_params);
    if (!cfg_val) {
        pho_debug("Failed to retrieve config parameter #%d", param_index);
        return -ENODATA;
    }

    token_dup = xstrdup(cfg_val);

    key = strtok_r(token_dup, ",", &save_ptr);
    if (key == NULL)
        key = token_dup;

    rc = -EINVAL;

    do {
        char *value = strchr(key, '=');

        if (value == NULL)
            continue;

        *value++ = '\0';
        if (strcmp(key, rsc_family_names[family]) != 0)
            continue;

        rc = 0;

        *substring = xstrdup(value);

        break;
    } while ((key = strtok_r(NULL, ",", &save_ptr)) != NULL);

    free(token_dup);

    return rc;
}

/** @TODO to be implemented
int pho_cfg_match(const char *section_pattern, const char *name_pattern,
                  struct pho_config_item *items, int *count);

int pho_cfg_set(const char *section, const char *name, const char *value,
                enum pho_cfg_level lvl);
*/
