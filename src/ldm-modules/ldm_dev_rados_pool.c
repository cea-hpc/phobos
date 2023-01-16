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
 * \brief  Phobos Local Device Manager: device calls for RADOS pools.
 *
 * Implement device primitives for a RADOS pool.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "pho_ldm.h"
#include "pho_cfg.h"
#include "pho_common.h"
#include "pho_module_loader.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#define PLUGIN_NAME     "rados_pool"
#define PLUGIN_MAJOR    0
#define PLUGIN_MINOR    1

static struct module_desc DEV_ADAPTER_RADOS_POOL_MODULE_DESC = {
    .mod_name  = PLUGIN_NAME,
    .mod_major = PLUGIN_MAJOR,
    .mod_minor = PLUGIN_MINOR,
};

static int pho_rados_pool_lookup(const char *dev_id, char *dev_path,
                                 size_t path_size)
{
    /* identifier for RADOS pools consists of <host>:<path>
     * path is the RADOS pool's name
     */
    const char *sep = strchr(dev_id, ':');

    ENTRY;

    if (!sep)
        return -EINVAL;

    if (strlen(sep) + 1 > path_size)
        return -ERANGE;

    strncpy(dev_path, sep + 1, path_size);
    return 0;
}

static int pho_rados_pool_exists(const char *dev_id)
{
    struct lib_drv_info drv_info;
    struct lib_handle lib_hdl;
    int rc2 = 0;
    int rc = 0;

    ENTRY;

    rc = get_lib_adapter(PHO_LIB_RADOS, &lib_hdl.ld_module);
    if (rc)
        return rc;

    rc = ldm_lib_open(&lib_hdl, dev_id);
    if (rc)
        LOG_GOTO(out, rc, "Could not connect to Ceph cluster");

    rc = ldm_lib_drive_lookup(&lib_hdl, dev_id, &drv_info);

out:
    rc2 = ldm_lib_close(&lib_hdl);
    if (rc2)
        pho_error(rc2, "Closing RADOS library failed");
    /* If rc is >= 0, the pool exists and rc is the pool's id */
    return rc < 0 ? rc : rc2;
}

static int pho_rados_pool_query(const char *dev_path, struct ldm_dev_state *lds)
{
    char hostname[HOST_NAME_MAX];
    char *id = NULL;
    char *dot;
    int rc;

    ENTRY;

    lds->lds_family = PHO_RSC_RADOS_POOL;
    lds->lds_model = NULL;
    if (gethostname(hostname, HOST_NAME_MAX))
        LOG_RETURN(-EADDRNOTAVAIL, "Failed to get host name");

    /* truncate to short host name */
    dot = strchr(hostname, '.');
    if (dot)
        *dot = '\0';

    /* RADOS pool id is set to <host>:<path> */
    if (asprintf(&id, "%s:%s", hostname, dev_path) == -1 || id == NULL)
        LOG_RETURN(-ENOMEM, "String allocation failed");

    rc = pho_rados_pool_exists(id);

    if (rc)
        free(id);
    else
        lds->lds_serial = id;

    return rc;
}

/** Exported dev adapter */
struct pho_dev_adapter_module_ops DEV_ADAPTER_RADOS_POOL_OPS = {
    .dev_lookup = pho_rados_pool_lookup,
    .dev_query  = pho_rados_pool_query,
    .dev_load   = NULL,
    .dev_eject  = NULL,
};

/** Dev adapter module registration entry point */
int pho_module_register(void *module, void *context)
{
    struct dev_adapter_module *self = (struct dev_adapter_module *) module;

    phobos_module_context_set(context);

    self->desc = DEV_ADAPTER_RADOS_POOL_MODULE_DESC;
    self->ops = &DEV_ADAPTER_RADOS_POOL_OPS;

    return 0;
}
