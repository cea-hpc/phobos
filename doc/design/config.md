# Levels of configuration
We distinguish multiple levels of configuration:

## Hard coded
A hard-coded parameter, used only in case that parameter was not set beforehand
in another level.

## Global
A globally specified parameter, with value shared amongst all
nodes. Admins should prioritize this method to make sure the changes affect
every node.

These parameters are stored in the DSS.

## Local
A locally specified parameter, with value shared amongst all
processes of the node. Prioritize this method to modify specific parameters
of single nodes.

These parameters are stored directly on nodes, in ".ini" files (e.g.
.git/config syntax), which will be parsed using the C library "libini_config"
and the Python library "python-iniparse", available in every CentOS
distribution.

Here is an example of a ".ini" file:

```INI
[dss]
	server_url = phobos-dss

[media "lto5"]
	compat_rules = lto3:r,lto4:rw,lto5:rw
	default_format = ltfs
	default_mapping = hash1
```

## Process
A parameter specified for a single process. One notable use of
this method is for non-regression testing.

These parameters are given using environnement variables.

Here is an example of environment variables:

```Shell
export PHOBOS_DSS_connect_string="dbname=dummy host=dummy "
                                 "user=dummy password=dummy"
export PHOBOS_STORE_default_layout="simple"
```

## CLI
A parameter defined for a single execution, given through the command-line
interface.

# Levels of configuration and priority
We define a certain order of priority between the different levels, based on
their locality, which corresponds to the following:

```
CLI > Process > Local > Global > Hard-coded
```

# API calls
Each of these parameters can be acessed and set using these functions:

## Get
This function will try and get the parameter from the level of configuration
specified by "section".

```C
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
```

## Set (not implemented yet)
This function will set the parameter in the section specified by the flags.

```C
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
```

The flags are the following:

```C
enum pho_cfg_flags {
    PHO_CFG_SCOPE_PROCESS = (1 << 0), /**< set the parameter only for current
                                           process */
    PHO_CFG_SCOPE_LOCAL   = (1 << 1), /**< set the parameter for local host */
    PHO_CFG_SCOPE_GLOBAL  = (1 << 2), /**< set the parameter for all phobos
                                           hosts and instances */
};
```

## Match (not implemented yet)
This function will try to fetch the value of multiple parameters in different
sections.

```C
/**
 * This function gets the value of multiple configuration items
 * that match the given section_pattern and/or name_pattern.
 * String matching is shell-like (fnmatch(3)).
 *
 * @param(in) section_pattern   Pattern of the sections to look for name
 *                              patterns.
 *                              If NULL, search name_pattern is all sections.
 * @param(in) name_pattern      Pattern of parameter name to loook for.
 *                              If NULL, return all names in matching
 *                              sections.
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
```
