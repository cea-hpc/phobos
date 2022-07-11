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
 * \brief  Phobos Local Device Manager: device calls for inplace directories.
 *
 * Implement device primitives for a directory.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "pho_common.h"
#include "pho_ldm.h"
#include "pho_module_loader.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#define PLUGIN_NAME     "dir"
#define PLUGIN_MAJOR    0
#define PLUGIN_MINOR    1

static struct module_desc DEV_ADAPTER_DIR_MODULE_DESC = {
    .mod_name  = PLUGIN_NAME,
    .mod_major = PLUGIN_MAJOR,
    .mod_minor = PLUGIN_MINOR,
};

static int dir_lookup(const char *dev_id, char *dev_path, size_t path_size)
{
    /* identifier for directories consists of <host>:<path> */
    const char *sep = strchr(dev_id, ':');
    ENTRY;

    if (!sep)
        return -EINVAL;

    strncpy(dev_path, sep + 1, path_size);
    return 0;
}

static int dir_query(const char *dev_path, struct ldm_dev_state *lds)
{
    char     hostname[HOST_NAME_MAX];
    char    *dot;
    char    *id = NULL;
    char    *real;
    int      rc = 0;
    ENTRY;

    lds->lds_family = PHO_RSC_DIR;
    lds->lds_model = NULL;

    real = realpath(dev_path, NULL);
    if (!real)
        LOG_RETURN(-errno, "Could not resolve path '%s'", dev_path);

    if (gethostname(hostname, HOST_NAME_MAX))
        LOG_GOTO(out_free, rc = -EADDRNOTAVAIL, "Failed to get host name");

    /* truncate to short host name */
    dot = strchr(hostname, '.');
    if (dot)
        *dot = '\0';

    /* dir id is set to <host>:<real-path> */
    if (asprintf(&id, "%s:%s", hostname, real) == -1 || id == NULL)
        LOG_GOTO(out_free, rc = -ENOMEM, "String allocation failed");

    lds->lds_serial = id;

out_free:
    free(real);
    return rc;
}

/** Exported dev adapter */
struct pho_dev_adapter_module_ops DEV_ADAPTER_DIR_OPS = {
    .dev_lookup = dir_lookup,
    .dev_query  = dir_query,
    .dev_load   = NULL,
    .dev_eject  = NULL,
};

/** Dev adapter module registration entry point */
int pho_module_register(void *module)
{
    struct dev_adapter_module *self = (struct dev_adapter_module *) module;

    self->desc = DEV_ADAPTER_DIR_MODULE_DESC;
    self->ops = &DEV_ADAPTER_DIR_OPS;

    return 0;
}
