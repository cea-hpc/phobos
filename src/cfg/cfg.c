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

/** thread-wide handle to DSS */
static __thread void *thr_dss_hdl;

/** pointer to the loaded config file */
static const char *cfg_file;

/** pointer to the loaded configuration structure */
static struct collection_item *cfg_items;


const struct pho_config_item pho_cfg_descr[] = {
    [PHO_CFG_DSS_connect_string] = {
        .section = "dss",
        .name    = "connect_string",
        .value   = "dbname=phobos host=localhost"
    },
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
    [PHO_CFG_LRS_default_family] = {
        .section = "lrs",
        .name    = "default_family",
        .value   = "tape"
    },
    [PHO_CFG_LDM_cmd_drive_query] = {
        .section = "ldm",
        .name    = "cmd_drive_query",
        .value   = PHO_LDM_HELPER" query_drive \"%s\""
    },
    [PHO_CFG_LDM_cmd_drive_load] = {
        .section = "ldm",
        .name    = "cmd_drive_load",
        .value   = PHO_LDM_HELPER" load_drive \"%s\" \"%s\""
    },
    [PHO_CFG_LDM_cmd_drive_unload] = {
        .section = "ldm",
        .name    = "cmd_drive_unload",
        .value   = PHO_LDM_HELPER" unload_drive \"%s\" \"%s\""
    },
    [PHO_CFG_LDM_cmd_mount_ltfs] = {
        .section = "ldm",
        .name    = "cmd_mount_ltfs",
        .value   = PHO_LDM_HELPER" mount_ltfs \"%s\" \"%s\""
    },
    [PHO_CFG_LDM_cmd_umount_ltfs] = {
        .section = "ldm",
        .name    = "cmd_umount_ltfs",
        .value   = PHO_LDM_HELPER" umount_ltfs \"%s\" \"%s\""
    },
    [PHO_CFG_LDM_cmd_format_ltfs] = {
        .section = "ldm",
        .name    = "cmd_format_ltfs",
        .value   = PHO_LDM_HELPER" format_ltfs \"%s\" \"%s\""
    },
};


/** load a local config file */
static int pho_cfg_load_file(const char *cfg)
{
    struct collection_item  *errors = NULL;
    int                      rc;

    rc = config_from_file("phobos", cfg, &cfg_items, INI_STOP_ON_ERROR,
                          &errors);

    /* libinit returns positive errno-like error codes */
    if (rc != 0) {
        if (rc == ENOENT && strcmp(cfg, PHO_DEFAULT_CFG) == 0) {
            pho_warn("no configuration file at default location: %s", cfg);
            rc = 0;
        } else {
            pho_error(rc, "failed to read configuration file '%s'", cfg);
            print_file_parsing_errors(stderr, errors);
            fprintf(stderr, "\n");
        }
        free_ini_config_errors(errors);
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

int pho_cfg_get_val(const char *section, const char *name, const char **value)
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

const char *pho_cfg_get(enum pho_cfg_params param)
{
    const struct pho_config_item    *item;
    const char                      *res;
    int                              rc;

    if (param >= PHO_CFG_LAST || param < PHO_CFG_FIRST)
        return NULL;

    item = &pho_cfg_descr[param];

    /* sanity check (in case of sparse pho_config_descr array) */
    if (!item->name)
        return NULL;

    rc = pho_cfg_get_val(item->section, item->name, &res);
    if (rc == -ENOATTR)
        res = item->value;

    return res;
}

/** @TODO to be implemented
int pho_cfg_match(const char *section_pattern, const char *name_pattern,
                  struct pho_config_item *items, int *count);

int pho_cfg_set(const char *section, const char *name, const char *value,
                enum pho_cfg_flags flags);
*/
