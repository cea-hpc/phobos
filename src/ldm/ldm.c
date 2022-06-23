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
 * \brief  Phobos Local Device Manager.
 *
 * This modules implements low level device control on local host.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "pho_ldm.h"
#include "pho_cfg.h"
#include "pho_type_utils.h"
#include "pho_common.h"
#include "ldm_dev_adapters.h"

#include <dlfcn.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdio.h>

static pthread_rwlock_t lib_adapters_rwlock;
static GHashTable *lib_adapters;

typedef int (*lib_adapter_init_func_t)(struct lib_adapter_module *);

/**
 * Initializes lib_adapters_rwlock and the lib_adapters hash table.
 */
__attribute__((constructor)) static void adapters_init(void)
{
    int rc;

    rc = pthread_rwlock_init(&lib_adapters_rwlock, NULL);
    if (rc) {
        fprintf(stderr,
                "Unexpected error: cannot initialize lib adapters rwlock: %s\n",
                strerror(rc));
        exit(EXIT_FAILURE);
    }

    /* Note: lib adapters are not meant to be unloaded for now */
    lib_adapters = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
                                         NULL);
    if (lib_adapters == NULL) {
        fprintf(stderr, "Could not create lib adapters hash table\n");
        exit(EXIT_FAILURE);
    }
}

/**
 * Convert lib adapter name into the corresponding module library name.
 * Actual path resolution will be handled by regular LD mechanism.
 *
 * eg. "dummy" -> "libpho_lib_adapter_dummy.so"
 *
 * \return 0 on success, negated errno on error.
 */
static int build_lib_adapter_instance_path(const char *mod_name, char *path,
                                           size_t len)
{
    int rc;

    rc = snprintf(path, len, "libpho_lib_adapter_%s.so", mod_name);
    if (rc < 0 || rc >= len) {
        path[0] = '\0';
        return -EINVAL;
    }

    return 0;
}

/**
 * Load a lib adapter module by \a mod_name into \a mod.
 *
 * @param[in]   mod_name    Name of the module to load.
 * @param[out]  mod         Module descriptor to fill out.
 *
 * @return 0 on success, -errno on error.
 */
static int lib_adapter_load(const char *mod_name,
                            struct lib_adapter_module **mod)
{
    lib_adapter_init_func_t op_init;
    char libpath[NAME_MAX];
    void *hdl;
    int rc;

    ENTRY;

    pho_debug("Loading lib adapter '%s'", mod_name);

    rc = build_lib_adapter_instance_path(mod_name, libpath, sizeof(libpath));
    if (rc)
        LOG_RETURN(rc, "Invalid lib adapter name '%s'", mod_name);

    hdl = dlopen(libpath, RTLD_NOW);
    if (hdl == NULL)
        LOG_RETURN(-EINVAL, "Cannot load module '%s': %s", mod_name, dlerror());

    *mod = calloc(1, sizeof(**mod));
    if (*mod == NULL)
        GOTO(out_err, rc = -ENOMEM);

    op_init = dlsym(hdl, PLAM_OP_INIT);
    if (!op_init)
        LOG_GOTO(out_err, rc = -ENOSYS,
                 "Operation '%s' is missing", PLAM_OP_INIT);

    rc = op_init(*mod);
    if (rc)
        LOG_GOTO(out_err, rc, "Could not initialize module '%s'", mod_name);

    pho_debug("Plugin %s-%d.%d successfully loaded",
              (*mod)->desc.mod_name,
              (*mod)->desc.mod_major,
              (*mod)->desc.mod_minor);

out_err:
    if (rc) {
        free(*mod);
        dlclose(hdl);
    }

    return rc;
}

/**
 * Load a lib adapter module if it has not already been. Relies on the global
 * lib_adapters modules hash map and its associated rwlock.
 *
 * @param[in]   mod_name    Name of the module to load.
 * @param[out]  mod         Module descriptor to fill out.
 *
 * @return 0 on success, -errno on error.
 */
static int lib_adapter_mod_lazy_load(const char *mod_name,
                                     struct lib_adapter_module **module)
{
    int rc2;
    int rc;

    rc = pthread_rwlock_rdlock(&lib_adapters_rwlock);
    if (rc)
        LOG_RETURN(rc, "Cannot read lock lib adapters table");

    /* Check if module loaded */
    *module = g_hash_table_lookup(lib_adapters, mod_name);

    /* If not loaded, unlock, write lock, check if it is loaded and load it */
    if (*module == NULL) {
        /* Release the read lock */
        rc = pthread_rwlock_unlock(&lib_adapters_rwlock);
        if (rc)
            LOG_RETURN(rc, "Cannot unlock lib adapters table");

        /* Re-acquire a write lock */
        rc = pthread_rwlock_wrlock(&lib_adapters_rwlock);
        if (rc)
            LOG_RETURN(rc, "Cannot write lock lib adapters table");

        /*
         * Re-check if module loaded (this could have changed between the read
         * lock release and the write lock acquisition)
         */
        *module = g_hash_table_lookup(lib_adapters, mod_name);
        if (*module != NULL)
            goto out_unlock;

        rc = lib_adapter_load(mod_name, module);
        if (rc)
            LOG_GOTO(out_unlock, rc, "Error while loading lib adapter %s",
                     mod_name);

        /* Insert the module into the module table */
        g_hash_table_insert(lib_adapters, strdup(mod_name), *module);

        /* Write lock is released in function final cleanup */
    }

out_unlock:
    rc2 = pthread_rwlock_unlock(&lib_adapters_rwlock);
    if (rc2)
        /* This unlock shouldn't fail as the lock was taken in this function.
         * In case it does, one may not assume thread-safety, so we report this
         * error instead of previous ones.
         */
        LOG_RETURN(rc2, "Cannot unlock lib adapters table");

    return rc;
}

int get_lib_adapter(enum lib_type lib_type, struct lib_adapter *lib)
{
    struct lib_adapter_module *module;
    int rc = 0;

    switch (lib_type) {
    case PHO_LIB_DUMMY:
        rc = lib_adapter_mod_lazy_load("dummy", &module);
        break;
    case PHO_LIB_SCSI:
        rc = lib_adapter_mod_lazy_load("scsi", &module);
        break;
    default:
        return -ENOTSUP;
    }

    if (rc)
        return rc;

    *lib = *module->ops;

    return rc;
}

int get_dev_adapter(enum rsc_family dev_family, struct dev_adapter *dev)
{
    switch (dev_family) {
    case PHO_RSC_DIR:
        *dev = dev_adapter_dir;
        break;
    case PHO_RSC_TAPE:
        *dev = dev_adapter_scsi_tape;
        break;
    case PHO_RSC_RADOS_POOL:
#ifdef RADOS_ENABLED
        *dev = dev_adapter_rados_pool;
#else
        LOG_RETURN(-ENOTSUP, "Phobos has been built without the necessary "
                             "RADOS modules");
#endif
        break;
    default:
        return -ENOTSUP;
    }
    return 0;
}

void ldm_dev_state_fini(struct ldm_dev_state *lds)
{
    free(lds->lds_model);
    free(lds->lds_serial);
    lds->lds_model = NULL;
    lds->lds_serial = NULL;
}

extern const struct fs_adapter fs_adapter_posix;
extern const struct fs_adapter fs_adapter_ltfs;

int get_fs_adapter(enum fs_type fs_type, struct fs_adapter *fsa)
{
    switch (fs_type) {
    case PHO_FS_POSIX:
        *fsa = fs_adapter_posix;
        break;
    case PHO_FS_LTFS:
        *fsa = fs_adapter_ltfs;
        break;
    default:
        return -ENOTSUP;
    }
    return 0;
}

int ldm_dev_query(const struct dev_adapter *dev, const char *dev_path,
                  struct ldm_dev_state *lds)
{
    assert(dev != NULL);
    assert(dev->dev_query != NULL);
    return dev->dev_query(dev_path, lds);
}
