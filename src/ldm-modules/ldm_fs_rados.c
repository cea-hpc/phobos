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
 * \brief  Phobos Local Device Manager: FS calls for pools.
 *
 * Implement filesystem primitives for a pool.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "ldm_common.h"
#include "pho_common.h"
#include "pho_ldm.h"
#include "pho_module_loader.h"

#include <fcntl.h>
#include <rados/librados.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#define RADOS_LABEL_PATH ".phobos_rados_pool_label"

#define PLUGIN_NAME     "rados"
#define PLUGIN_MAJOR    0
#define PLUGIN_MINOR    1

static struct module_desc FS_ADAPTER_RADOS_MODULE_DESC = {
    .mod_name  = PLUGIN_NAME,
    .mod_major = PLUGIN_MAJOR,
    .mod_minor = PLUGIN_MINOR,
};

static int pho_rados_pool_disconnect(struct lib_handle *lib_hdl,
                                     rados_ioctx_t *pool_io_ctx)
{
    int rc = 0;

    if (pool_io_ctx) {
        rados_ioctx_destroy(*pool_io_ctx);
        *pool_io_ctx = NULL;
    }

    rc = ldm_lib_close(lib_hdl);
    if (rc)
        pho_error(rc, "Failed to close RADOS library");

    return rc;
}

static int pho_rados_pool_connect(struct lib_handle *lib_hdl,
                                  rados_ioctx_t *pool_io_ctx,
                                  const char *poolname)
{
    rados_t cluster_hdl;
    int rc;

    rc = get_lib_adapter(PHO_LIB_RADOS, &lib_hdl->ld_module);
    if (rc)
        return rc;

    rc = ldm_lib_open(lib_hdl, poolname);
    if (rc)
        LOG_GOTO(out_err, rc, "Could not connect to Ceph cluster");

    if (pool_io_ctx) {
        cluster_hdl = lib_hdl->lh_lib;
        rc = rados_ioctx_create(cluster_hdl, poolname, pool_io_ctx);
        if (rc) {
            LOG_GOTO(out_err, rc, "Could not create I/O context for pool '%s'",
                     poolname);
        }
    }

    return 0;

out_err:
    rc = pho_rados_pool_disconnect(lib_hdl, pool_io_ctx);
    return rc;
}

/* "poolname" corresponds to "mnt_path" in the FS Adapter API
 * llen includes the null byte at the end of the string, therefore the fs_label
 * should only contain llen-1 characters
 */
static int pho_rados_pool_get_label(const char *poolname, char *fs_label,
                                    size_t llen)
{
    char *label_path = RADOS_LABEL_PATH;
    rados_ioctx_t pool_io_ctx;
    struct lib_handle lib_hdl;
    int rc2;
    int rc;

    ENTRY;

    rc = pho_rados_pool_connect(&lib_hdl, &pool_io_ctx, poolname);
    if (rc)
        LOG_GOTO(out, rc, "Could not connect to the pool '%s'", poolname);

    rc = rados_read(pool_io_ctx, label_path, fs_label, llen - 1, 0);
    if (rc < 0)
        LOG_GOTO(out, rc, "Cannot read label: '%s'", label_path);

    fs_label[rc] = '\0';
    rc = 0;

out:
    rc2 = pho_rados_pool_disconnect(&lib_hdl, &pool_io_ctx);
    return rc ? rc : rc2;
}

/**
 * Pseudo mount function. Does not actually mount anything but check the
 * filesystem label, to comply with the behavior of other backends.
 * In RADOS case, "dev_path" and "poolname" (mnt_path in general) are actually
 * the same thing because mounting RADOS pools does not make sense. Therefore,
 * the "poolname" variable is used here.
 */
static int pho_rados_pool_labelled(const char *dev_path, const char *poolname,
                                   const char *fs_label)
{
    char label_on_pool[PHO_LABEL_MAX_LEN + 1];
    (void) dev_path;
    int rc;

    ENTRY;

    rc = pho_rados_pool_get_label(poolname, label_on_pool,
                                  sizeof(label_on_pool));
    if (rc)
        LOG_RETURN(rc, "Cannot retrieve label on '%s'", poolname);

    rc = strcmp(label_on_pool, fs_label);
    if (rc)
        LOG_RETURN(-EINVAL, "Label mismatch on '%s': expected:'%s' found:'%s'",
                   poolname, fs_label, label_on_pool);

    return 0;
}

static int pho_rados_pool_stats(const char *poolname,
                                struct ldm_fs_space *fs_spc)
{
    struct rados_cluster_stat_t cluster_stats;
    struct rados_pool_stat_t pool_stats;
    struct lib_handle lib_hdl;
    rados_ioctx_t pool_io_ctx;
    rados_t cluster_hdl;
    int rc2 = 0;
    int rc = 0;

    ENTRY;

    rc = pho_rados_pool_connect(&lib_hdl, &pool_io_ctx, poolname);
    if (rc)
        LOG_GOTO(out, rc, "Could not connect to the pool");

    cluster_hdl = lib_hdl.lh_lib;

    rc = rados_cluster_stat(cluster_hdl, &cluster_stats);
    if (rc)
        LOG_GOTO(out, rc, "Could not get the Ceph cluster's stats");

    rc = rados_ioctx_pool_stat(pool_io_ctx, &pool_stats);
    if (rc < 0)
        LOG_GOTO(out, rc, "Could not get the pool's stats");

    fs_spc->spc_used = pool_stats.num_bytes;
    fs_spc->spc_avail = cluster_stats.kb_avail * 1000;
    fs_spc->spc_flags = 0;

out:
    rc = pho_rados_pool_disconnect(&lib_hdl, &pool_io_ctx);
    return rc < 0 ? rc : rc2;
}

static int pho_rados_pool_format(const char *poolname, const char *label,
                                 struct ldm_fs_space *fs_spc)
{
    char *label_path = RADOS_LABEL_PATH;
    struct lib_handle lib_hdl;
    rados_ioctx_t pool_io_ctx;
    char label_buffer[10];
    int rc2, rc;

    ENTRY;

    rc = pho_rados_pool_connect(&lib_hdl, &pool_io_ctx, poolname);
    if (rc)
        LOG_GOTO(out, rc, "Could not connect to the pool %s", poolname);

    rc = rados_read(pool_io_ctx, label_path, label_buffer, 10, 0);
    if (rc > 0)
        LOG_GOTO(out, rc = -EEXIST, "Rados pool %s already formatted",
                 poolname);

    if (rc != -ENOENT)
        LOG_GOTO(out, rc, "Found unexpected label object '%s' in pool '%s' but "
                          "failed to read from it",
                 label_path, poolname);

    rc = rados_write(pool_io_ctx, label_path, label, strlen(label), 0);
    if (rc < 0)
        LOG_GOTO(out, rc = -errno, "Cannot set label '%s' on pool '%s'",
                 label_path, poolname);

    if (fs_spc) {
        memset(fs_spc, 0, sizeof(*fs_spc));
        rc = pho_rados_pool_stats(poolname, fs_spc);
    }

out:
    rc2 = pho_rados_pool_disconnect(&lib_hdl, &pool_io_ctx);
    return rc < 0 ? rc : rc2;
}

/* This function checks if a pool with the given poolname exists in the Ceph
 * cluster and fill mnt_path with the poolname to comply with the expected
 * behavior of FS Adapters.
 */
static int pho_rados_pool_exists(const char *poolname, char *mnt_path,
                                 size_t mnt_path_size)
{
    char mounted_label[PHO_LABEL_MAX_LEN + 1];
    struct lib_handle lib_hdl;
    rados_t cluster_hdl;
    int rc2 = 0;
    int rc = 0;

    rc = pho_rados_pool_connect(&lib_hdl, NULL, poolname);
    if (rc)
        LOG_GOTO(out, rc, "Could not connect to Ceph cluster");

    cluster_hdl = lib_hdl.lh_lib;
    rc = rados_pool_lookup(cluster_hdl, poolname);
    if (rc < 0)
        LOG_GOTO(out, rc, "Could not find a pool named %s", poolname);

    rc = pho_rados_pool_get_label(poolname, mounted_label,
                                  sizeof(mounted_label));
    if (rc) {
        pho_info("The pool %s is present but we can't get any label", poolname);
        return -ENOENT;
    }

    memset(mnt_path, 0, mnt_path_size);
    strncpy(mnt_path, poolname, mnt_path_size);
    /* make sure mnt_path is null terminated */
    mnt_path[mnt_path_size-1] = '\0';

out:
    rc2 = pho_rados_pool_disconnect(&lib_hdl, NULL);
    /* If rc is >= 0, the pool exists and rc is the pool's id */
    return rc < 0 ? rc : rc2;
}

/** Exported fs adapter */
struct pho_fs_adapter_module_ops FS_ADAPTER_RADOS_OPS = {
    .fs_mount     = pho_rados_pool_labelled,
    .fs_umount    = NULL,
    .fs_format    = pho_rados_pool_format,
    .fs_mounted   = pho_rados_pool_exists,
    .fs_df        = pho_rados_pool_stats,
    .fs_get_label = pho_rados_pool_get_label,
};

/** FS adapter module registration entry point */
int pho_module_register(void *module, void *context)
{
    struct fs_adapter_module *self = (struct fs_adapter_module *) module;

    phobos_module_context_set(context);

    self->desc = FS_ADAPTER_RADOS_MODULE_DESC;
    self->ops = &FS_ADAPTER_RADOS_OPS;

    return 0;
}
