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

#include <rados/librados.h>
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

/** List of configuration parameters for Ceph RADOS */
enum pho_cfg_params_ceph_rados {
    PHO_CFG_CEPH_RADOS_FIRST,

    /* Ceph RADOS parameters */
    PHO_CFG_CEPH_RADOS_user_id = PHO_CFG_CEPH_RADOS_FIRST,
    PHO_CFG_CEPH_RADOS_conf_file,

    PHO_CFG_CEPH_RADOS_LAST
};

const struct pho_config_item cfg_ceph_rados[] = {
    [PHO_CFG_CEPH_RADOS_user_id] = {
        .section = "rados",
        .name    = "user_id",
        .value   = "admin"
    },
    [PHO_CFG_CEPH_RADOS_conf_file] = {
        .section = "rados",
        .name    = "ceph_conf_file",
        .value   = "/etc/ceph/ceph.conf"
    },
};

static int pho_rados_cluster_handle_init(rados_t *cluster)
{
    const char *ceph_conf_path;
    const char *userid;
    int rc = 0;

    ENTRY;

    userid = PHO_CFG_GET(cfg_ceph_rados, PHO_CFG_CEPH_RADOS, user_id);
    ceph_conf_path = PHO_CFG_GET(cfg_ceph_rados, PHO_CFG_CEPH_RADOS, conf_file);

    /* Initialize the cluster handle. Default values:  "ceph" cluster name and
     * "client.admin" username
     */
    rc = rados_create(cluster, userid);
    if (rc < 0)
        LOG_RETURN(rc, "Cannot initialize the cluster handle");

    rc = rados_conf_read_file(*cluster, ceph_conf_path);
    if (rc < 0)
        LOG_RETURN(rc, "Cannot read the Ceph configuration file");

    rc = rados_connect(*cluster);
    if (rc < 0)
        LOG_RETURN(rc, "Cannot connect to cluster");

    return rc;
}

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

static int pho_rados_pool_exists(const char *dev_path)
{
    rados_t cluster_hdl;
    int rc = 0;

    ENTRY;

    rc = pho_rados_cluster_handle_init(&cluster_hdl);
    if (rc)
        LOG_GOTO(out, rc, "Could not connect to Ceph cluster");

    rc = rados_pool_lookup(cluster_hdl, dev_path);
    if (rc == -ENOENT)
        pho_error(rc = -ENODEV, "RADOS Poll '%s' does not exist", dev_path);
    else if (rc < 0)
        pho_error(rc, "RADOS pool lookup command failed");

out:
        rados_shutdown(cluster_hdl);
        /* If rc is >= 0, the pool exists and rc is the pool's id */
        return rc < 0 ? rc : 0;
}

static int pho_rados_pool_query(const char *dev_path, struct ldm_dev_state *lds)
{
    char hostname[HOST_NAME_MAX];
    char *id = NULL;
    char *dot;
    int rc;

    ENTRY;

    rc = pho_rados_pool_exists(dev_path);
    if (rc < 0)
        return rc;

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

    lds->lds_serial = id;

    return 0;
}

/** Exported dev adapater */
struct dev_adapter DEV_ADAPTER_RADOS_POOL_OPS = {
    .dev_lookup = pho_rados_pool_lookup,
    .dev_query  = pho_rados_pool_query,
    .dev_load   = NULL,
    .dev_eject  = NULL,
};

/** Dev adapter module registration entry point */
int pho_module_register(void *module)
{
    struct dev_adapter_module *self = (struct dev_adapter_module *) module;

    self->desc = DEV_ADAPTER_RADOS_POOL_MODULE_DESC;
    self->ops = &DEV_ADAPTER_RADOS_POOL_OPS;

    return 0;
}
