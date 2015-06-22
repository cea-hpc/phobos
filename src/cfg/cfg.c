/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 * Copyright 2014-2015 CEA/DAM. All Rights Reserved.
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
#include <attr/xattr.h>

/** thread-wide handle to DSS */
static __thread void *thr_dss_hdl;

/** pointer to the loaded config file */
static const char *cfg_file;

/** pointer to the loaded configuration structure */
static struct collection_item *cfg_items;

/** load a local config file */
static int pho_cfg_load_file(const char *cfg)
{
    int rc;
    struct collection_item *errors = NULL;

    rc = config_from_file("phobos", cfg, &cfg_items, INI_STOP_ON_ERROR,
                          &errors);
    if (rc != 0) {
        pho_error(rc, "failed to read configuration file '%s'", cfg);
        print_file_parsing_errors(stderr, errors);
        fprintf(stderr, "\n");
        free_ini_config_errors(errors);
        /* libinit returns errno-like error codes > 0 */
        return -rc;
    }

    cfg_file = cfg;
    return 0;
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

    if (cfg_items != NULL)
        return -EALREADY;

    if (cfg == NULL)
        cfg = getenv("PHOBOS_CFG_FILE");

    if (cfg == NULL)
        cfg = PHO_DEFAULT_CFG;

    return pho_cfg_load_file(cfg);
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
 * @return 0 on success, a negative errno value on error.
 */
static int build_env_name(const char *section, const char *name, char **env)
{
    char  *env_var, *curr;
    size_t strsize;

    if (section == NULL || name == NULL)
        return -EINVAL;

    /* sizeof returns length + 1, which makes room for first '_'.
     * Add 2 for 2nd '_' and final '\0'
     */
    strsize = sizeof(PHO_ENV_PREFIX) + strlen(section) + strlen(name) + 2;
    env_var = malloc(strsize);
    if (env_var == NULL)
        return -ENOMEM;

    /* copy prefix (strcpy is safe as the malloc() is properly sized) */
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
    return 0;
}

/**
 * Get process-wide configuration parameter from environment.
 * @retval 0 on success
 * @retval -ENOATTR if the parameter in not defined.
 * @retval other negative error code on failure.
 */
static int pho_cfg_get_env(const char *section, const char *name,
                           const char **value)
{
    int   rc;
    char *env, *val;

    rc = build_env_name(section, name, &env);
    if (rc != 0)
        return rc;

    val = getenv(env);
    pho_debug("environment: %s=%s", env, val ? val : "<NULL>");
    free(env);

    if (val == NULL)
        return -ENOATTR;

    *value = val;
    return 0;
}

/**
 * Get host-wide configuration parameter from config file.
 * @retval 0 on success
 * @retval -ENOATTR if the parameter in not defined.
 * @retval other negative error code on failure.
 */
static int pho_cfg_get_local(const char *section, const char *name,
                             const char **value)
{
    int rc;
    struct collection_item *item;

    rc = get_config_item(section, name, cfg_items, &item);
    if (rc)
        return -rc;

    *value = get_const_string_config_value(item, &rc);
    pho_debug("config file: %s::%s=%s", section, name,
              *value ? *value : "<NULL>");
    /* ini_config sets rc=EINVAL if the parameter is not found. */
    return ((*value == NULL) ? -ENOATTR : 0);
}

/**
 * Get global configuration parameter from DSS.
 * @retval 0 on success
 * @retval -ENOATTR if the parameter in not defined.
 * @retval other negative error code on failure.
 */
static int pho_cfg_get_global(const char *section, const char *name,
                              const char **value)
{
    /** @TODO get value from DSS (not in phobos v0) */
    return -ENOTSUP;
}

int pho_cfg_get(const char *section, const char *name, const char **value)
{
    int   rc;

    /* 1) check process-wide parameter (from environment)*/
    rc = pho_cfg_get_env(section, name, value);
    if (rc != -ENOATTR)
        return rc;

    /* 2) check host-wide parameter (if config file has been loaded) */
    if (cfg_items) {
        rc = pho_cfg_get_local(section, name, value);
        if (rc != -ENOATTR)
            return rc;
    }

    /* 3) check global parameter (if connection is set) */
    if (thr_dss_hdl)
        return pho_cfg_get_global(section, name, value);

    return -ENOATTR;
}

/** @TODO to be implemented
int pho_cfg_match(const char *section_pattern, const char *name_pattern,
                  struct pho_config_item *items, int *count);

int pho_cfg_set(const char *section, const char *name, const char *value,
                enum pho_cfg_flags flags);
*/
