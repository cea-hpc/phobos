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
#ifndef _PHO_CFG_H
#define _PHO_CFG_H

#include <sys/types.h>
#include <attr/xattr.h>
#include <stdbool.h>

#include "pho_types.h"

/** prefix string for environment variables */
#define PHO_ENV_PREFIX "PHOBOS"

/** default path to local config file */
#define PHO_DEFAULT_CFG "/etc/phobos.conf"

enum pho_cfg_level {
    PHO_CFG_LEVEL_PROCESS, /**< consider the parameter only for current process
                             */
    PHO_CFG_LEVEL_LOCAL,   /**< consider the parameter for localhost */
    PHO_CFG_LEVEL_LAST,

    /* TODO: Move before LAST when implemented */
    PHO_CFG_LEVEL_GLOBAL,  /**< consider the parameter for all phobos hosts and
                             * instances
                             */
};

struct pho_config_item {
    char *section;
    char *name;
    char *value;
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
 * Release the memory allocated by pho_cfg_init_local. Once called, no pho_cfg_*
 * function can be used.
 */
void pho_cfg_local_fini(void);

/** This function gets the value of a configuration item
 *  and return default value (from module_params) if it is not found.
 *  @return A the value on success and NULL on error.
 */
const char *_pho_cfg_get(int first_index, int last_index, int param_index,
                        const struct pho_config_item *module_params);

#define PHO_CFG_GET(_params_list, _cfg_namespace, _name)                \
        _pho_cfg_get(_cfg_namespace ## _FIRST, _cfg_namespace ## _LAST, \
                _cfg_namespace ## _ ##_name, (_params_list))

/**
 * Allow access to global config parameters for the current thread.
 * This can only be called after the DSS is initialized.
 */
int pho_cfg_set_thread_conn(void *dss_handle);

/**
 * This function gets the value of the configuration item with the given name
 * in the given section but only at a specific level of configuration.
 *
 * @param(in) section   Name of the section to look for the parameter.
 * @param(in) name      Name of the parameter to read.
 * @param(in) lvl       Level of configuration to check.
 * @param(out) value    Value of the parameter (const string, must not be
 *                      altered).
 *
 * @retval  0           The parameter is returned successfully.
 * @retval  -ENODATA    The parameter is not found.
 */
int pho_cfg_get_val_from_level(const char *section, const char *name,
                               enum pho_cfg_level lvl, const char **value);

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
 * @retval  -ENODATA    The parameter is not found.
 */
int pho_cfg_get_val(const char *section, const char *name,
                    const char **value);

/**
 * Set a configuration value local to the process by inserting it to the
 * environment since this is the location with the highest priority.
 *
 * \param[in]  section  Name of the section where to set the parameter.
 * \param[in]  name     Name of the parameter to set.
 * \param[out] value    Value of the parameter.
 *
 * \return  0           The parameter is set successfully.
 *         -EINVAL      The parameters are invalid
 *         -ENOMEM      Not enough memory on the system
 */
int pho_cfg_set_val_local(const char *section, const char *name,
                          const char *value);


/**
 * \p csv_value is parsed as a CSV item (a comma separated list). The items are
 * stored in a list.
 *
 * \param[in]  csv_value    Comma separated list to parse
 * \param[out] value    List of items in the CSV list. The list is allocated and
 *                      must be passed to free() as well as each string of the
 *                      list.
 * \param[out] n        Number of values returned
 */
void get_val_csv(const char *csv_value, char ***value, size_t *n);

/**
 * Helper to get a numeric configuration parameter.
 * @param[in] param       Parameter to be retrieved.
 * @param[in] fail_value  Returned value if parsing fails.
 * @return parameter value, or fail_value on error.
 */
int _pho_cfg_get_int(int first_index, int last_index, int param_index,
                     const struct pho_config_item *module_params,
                     int fail_val);

#define PHO_CFG_GET_INT(_params_list, _cfg_namespace, _name, _fail_val)    \
        _pho_cfg_get_int(_cfg_namespace ## _FIRST, _cfg_namespace ## _LAST, \
                _cfg_namespace ## _ ##_name, (_params_list), (_fail_val))

/**
 * Helper to get a boolean (true/false) configuration parameter.
 *
 * @param[in] param          Parameter to be retrieved.
 * @param[in] default_value  Returned value if parsing fails.
 *
 * @return parameter value, or default_value on error.
 */
bool _pho_cfg_get_bool(int first_index, int last_index, int param_index,
                       const struct pho_config_item *module_params,
                       bool default_val);

#define PHO_CFG_GET_BOOL(_params_list, _cfg_namespace, _name, _default_val)  \
        _pho_cfg_get_bool(_cfg_namespace ## _FIRST, _cfg_namespace ## _LAST, \
                _cfg_namespace ## _ ##_name, (_params_list), (_default_val))


/**
 * Check the compatibility between a given \p tape_model and \p drive_model
 * using the different rules defined in the configuration file.
 *
 * @param[in] tape_model   Tape model used to check compatibility.
 * @param[in] drive_model  Drive model the compatibility should be checked
 *                         against.
 * @param[out] res         True if the tape and drive models are compatible.
 *
 * @return 0 on success, negative error code on failure and res is false
 */
int tape_drive_compat_models(const char *tape_model, const char *drive_model,
                             bool *res);

/**
 * Helper to get a substring value configuration parameter.
 *
 * @param[in]  param      Code-level default values to use if necessary.
 * @param[in]  family     Name of the family to read in the parameter.
 * @param[out] substring  Value of the parameter.
 *
 * @return  0            The parameter is returned successfully.
 * @return  -ENODATA     The parameter is not found.
 * @return  -EINVAL      The parameter or family are invalid.
 */
int _pho_cfg_get_substring_value(int first_index, int last_index,
                                  int param_index,
                                  const struct pho_config_item *module_params,
                                  enum rsc_family family,
                                  char **substring);

#define PHO_CFG_GET_SUBSTRING_VALUE(_params_list, _cfg_namespace, _name, \
                                    _family, _result)  \
        _pho_cfg_get_substring_value(_cfg_namespace ## _FIRST, \
                                     _cfg_namespace ## _LAST, \
                                     _cfg_namespace ## _ ##_name, \
                                     (_params_list), (_family), (_result))

/**
 * Get the default copy name from the conf.
 *
 * @param[out] copy_name    Default copy name in conf.
 *
 * @return 0 on success, negative error code on failure
 */
int get_cfg_default_copy_name(const char **copy_name);

/**
 * Retrieve the get preferred order for the copies.
 *
 * The array of copies must be freed by the caller.
 *
 * @param[out] values    Array of copies to be retrieved in priority order.
 * @param[out] count     Size of values.
 *
 * @return 0 on success, negative error code on failure
 */
int get_cfg_preferred_order(char ***values, size_t *count);

#endif
