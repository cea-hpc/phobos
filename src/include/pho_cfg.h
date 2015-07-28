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
#ifndef _PHO_CFG_H
#define _PHO_CFG_H

#include <sys/types.h>
#include <attr/xattr.h>

/** prefix string for environment variables */
#define PHO_ENV_PREFIX "PHOBOS"

/** default path to local config file */
#define PHO_DEFAULT_CFG "/etc/phobos.conf"

/** List of phobos configuration parameters */
enum pho_cfg_params {
    PHO_CFG_FIRST,

    /* dss parameters */
    PHO_CFG_DSS_connect_string = PHO_CFG_FIRST,

    /* lrs parameters */
    PHO_CFG_LRS_mount_prefix,
    PHO_CFG_LRS_policy,
    PHO_CFG_LRS_default_family,

    /* ldm parameters */
    PHO_CFG_LDM_cmd_drive_query,
    PHO_CFG_LDM_cmd_mount_ltfs,

    PHO_CFG_LAST
};

struct pho_config_item {
    const char *section;
    const char *name;
    const char *value;
};

#define PHO_LDM_HELPER "/usr/sbin/pho_ldm_helper"

/** Name and default of configuration parameters.
 *  Value contains default.
 */
static const struct pho_config_item pho_cfg_descr[] = {
    [PHO_CFG_DSS_connect_string] = {"dss", "connect_string",
                                    "dbname=phobos host=localhost"},
    [PHO_CFG_LRS_mount_prefix]   = {"lrs", "mount_prefix", "/mnt/phobos-"},
    [PHO_CFG_LRS_policy]         = {"lrs", "policy", "best_fit"},
    [PHO_CFG_LRS_default_family] = {"lrs", "default_family", "tape"},

    [PHO_CFG_LDM_cmd_drive_query] = {"ldm", "cmd_drive_query",
                                  PHO_LDM_HELPER" query_drive --json \"%s\""},
    [PHO_CFG_LDM_cmd_mount_ltfs]  = {"ldm", "cmd_mount_ltfs",
                                  PHO_LDM_HELPER" mount_ltfs --device \"%s\""
                                    "--path \"%s\""},
};

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
int pho_cfg_init_local(const char *config_file);

/**
 * Allow access to global config parameters for the current thread.
 * This can only be called after the DSS is initialized.
 */
int pho_cfg_set_thread_conn(void *dss_handle);

/**
 * This function gets the value of the configuration item
 * with the given name in the given section.
 *
 * @param(in) section   Name of the section to look for the parameter.
 * @param(in) name      Name of the parameter to read.
 * @param(out) value    Value of the parameter (const string, must not be
 *                      altered).
 *
 * @retval  0           The parameter is returned successfully.
 * @retval  -ENOATTR    The parameter is not found.
 */
int pho_cfg_get_val(const char *section, const char *name,
                    const char **value);

/** This function gets the value of a configuration item
 *  and return default value if it is not found.
 *  @return 0 on success, or a negative value on error.
 */
static inline int pho_cfg_get(enum pho_cfg_params param,
                              const char **value)
{
    int rc;
    const struct pho_config_item *item;

    if (param >= PHO_CFG_LAST || param < PHO_CFG_FIRST)
        return -EINVAL;

    item = &pho_cfg_descr[param];

    /* sanity check (in case of sparse pho_config_descr array) */
    if (!item->name)
        return -EINVAL;

    rc = pho_cfg_get_val(item->section, item->name, value);
    if (rc == -ENOATTR) {
        *value = item->value;
        rc = 0;
    }
    return rc;
}

/**
 * This function gets the value of multiple configuration items
 * that match the given section_pattern and/or name_pattern.
 * String matching is shell-like (fnmatch(3)).
 *
 * @param(in) section_pattern   Pattern of the sections to look for name
 *                              patterns.
 *                              If NULL, search name_pattern is all sections.
 * @param(in) name_pattern      Pattern of parameter name to loook for.
 *                              If NULL, return all names in matching sections.
 * @param(out) items            List of matching configuration items.
 *                              The list is allocated by the call and must be
 *                              freed by the caller.
 * @param(out) count            Number of elements in items.
 *
 * @return 0 on success. If no matching parameter is found, the function still
           returns 0 and count is set to 0.
 */
int pho_cfg_match(const char *section_pattern, const char *name_pattern,
                  struct pho_config_item *items, int *count);

enum pho_cfg_flags {
    PHO_CFG_SCOPE_PROCESS = (1 << 0), /**< set the parameter only for current
                                           process */
    PHO_CFG_SCOPE_LOCAL   = (1 << 1), /**< set the parameter for local host */
    PHO_CFG_SCOPE_GLOBAL  = (1 << 2), /**< set the parameter for all phobos
                                           hosts and instances */
};

/**
 * Set the value of a parameter.
 * @param(in) section   Name of the section where the parameter is located.
 * @param(in) name      Name of the parameter to set.
 * @param(in) value     Value of the parameter.
 * @param(in) flags     mask of OR'ed 'enum pho_cfg_flags'. In particular,
 *                      flags indicate the scope of the parameter
 *                      (see doc/design/config.txt).
 * @return 0 on success, a negative error code on failure (errno(3)).
 */
int pho_cfg_set(const char *section, const char *name, const char *value,
                enum pho_cfg_flags flags);

#endif
