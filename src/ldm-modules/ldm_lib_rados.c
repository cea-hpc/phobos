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
 * \brief Phobos Local Device Manager: RADOS library.
 *
 * Library for RADOS pools.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "pho_cfg.h"
#include "pho_common.h"
#include "pho_ldm.h"
#include "pho_module_loader.h"

#include <fcntl.h>
#include <jansson.h>
#include <rados/librados.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#define PLUGIN_NAME     "rados"
#define PLUGIN_MAJOR    0
#define PLUGIN_MINOR    1

static struct module_desc LIB_ADAPTER_RADOS_MODULE_DESC = {
    .mod_name  = PLUGIN_NAME,
    .mod_major = PLUGIN_MAJOR,
    .mod_minor = PLUGIN_MINOR,
};

/** List of configuration parameters for Ceph RADOS */
enum pho_cfg_params_ceph_rados {
    PHO_CFG_CEPH_RADOS_FIRST,

    /* Ceph RADOS parameters */
    PHO_CFG_CEPH_RADOS_conf_file = PHO_CFG_CEPH_RADOS_FIRST,
    PHO_CFG_CEPH_RADOS_user_id,

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

static int lib_rados_open(struct lib_handle *hdl, const char *dev,
                          json_t *message)
{
    const char *ceph_conf_path;
    const char *userid;
    rados_t cluster_hdl;
    int rc = 0;

    (void) message;

    ENTRY;

    userid = PHO_CFG_GET(cfg_ceph_rados, PHO_CFG_CEPH_RADOS, user_id);
    ceph_conf_path = PHO_CFG_GET(cfg_ceph_rados, PHO_CFG_CEPH_RADOS, conf_file);

    /* Initialize the cluster handle. Default values:  "ceph" cluster name and
     * "client.admin" username
     */
    rc = rados_create(&cluster_hdl, userid);
    if (rc < 0)
        LOG_GOTO(err_out, rc, "Cannot initialize the cluster handle");

    rc = rados_conf_read_file(cluster_hdl, ceph_conf_path);
    if (rc < 0)
        LOG_GOTO(err_out, rc, "Cannot read the Ceph configuration file");

    rc = rados_connect(cluster_hdl);
    if (rc < 0)
        LOG_GOTO(err_out, rc, "Cannot connect to cluster");

    hdl->lh_lib = cluster_hdl;

    return 0;

err_out:
    rados_shutdown(cluster_hdl);
    hdl->lh_lib = NULL;
    return rc;
}

static int lib_rados_close(struct lib_handle *hdl)
{
    ENTRY;

    if (!hdl->lh_lib) /* already closed */
        return -EBADF;

    rados_shutdown(hdl->lh_lib);
    hdl->lh_lib = NULL;
    return 0;
}

static int pho_rados_pool_exists(rados_t cluster_hdl, const char *poolname)
{
    int rc;

    rc = rados_pool_lookup(cluster_hdl, poolname);
    if (rc == -ENOENT)
        pho_error(rc = -ENODEV, "RADOS Pool '%s' does not exist", poolname);
    else if (rc < 0)
        pho_error(rc, "RADOS pool lookup command failed");

    return rc < 0 ? rc : 0;
}

/**
 * Return drive info for an online device.
 */
static int lib_rados_drive_lookup(struct lib_handle *lib_hdl,
                                  const char *drive_serial,
                                  struct lib_drv_info *drv_info,
                                  json_t *message)
{
    const char *sep = strchr(drive_serial, ':');
    int rc;

    ENTRY;

    (void) message;

    if (!lib_hdl->lh_lib || !sep)
        return -EBADF;

    drv_info->ldi_medium_id.family = PHO_RSC_RADOS_POOL;
    drv_info->ldi_addr.lia_addr = 0;

    rc = pho_id_name_set(&drv_info->ldi_medium_id, sep + 1);
    if (rc)
        return rc;

    rc = pho_rados_pool_exists(lib_hdl->lh_lib, drv_info->ldi_medium_id.name);
    if (rc < 0) {
        drv_info->ldi_addr.lia_type = MED_LOC_UNKNOWN;
        drv_info->ldi_full = false;
        return rc;
    }

    drv_info->ldi_addr.lia_type = MED_LOC_DRIVE;
    drv_info->ldi_full = true;
    return 0;
}

/**
 * Extract path from drive identifier which consists of <host>:<path>.
 */
static int lib_rados_media_lookup(struct lib_handle *lib_hdl,
                                  const char *media_label,
                                  struct lib_item_addr *med_addr,
                                  json_t *message)
{
    int rc = 0;

    (void) message;

    ENTRY;

    if (!lib_hdl->lh_lib)
        return -EBADF;

    rc = pho_rados_pool_exists(lib_hdl->lh_lib, media_label);
    med_addr->lia_addr = 0;
    if (rc < 0) {
        med_addr->lia_type = MED_LOC_UNKNOWN;
        return rc;
    }

    med_addr->lia_type = MED_LOC_DRIVE; /* always in drive */
    return 0;
}

/** Exported library adapater */
static struct pho_lib_adapter_module_ops LIB_ADAPTER_RADOS_OPS = {
    .lib_open  = lib_rados_open,
    .lib_close = lib_rados_close,
    .lib_drive_lookup = lib_rados_drive_lookup,
    .lib_media_lookup = lib_rados_media_lookup,
    .lib_media_move = NULL,
};

/** Lib adapter module registration entry point */
int pho_module_register(void *module, void *context)
{
    struct lib_adapter_module *self = (struct lib_adapter_module *) module;

    phobos_module_context_set(context);

    self->desc = LIB_ADAPTER_RADOS_MODULE_DESC;
    self->ops = &LIB_ADAPTER_RADOS_OPS;

    return 0;
}
