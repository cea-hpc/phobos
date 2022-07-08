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
 * \brief  Phobos Module Loader Manager.
 *
 * This module implements the loading at runtime of other modules like the
 * layouts or the lib/dev/fs/io adapters.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "pho_common.h"
#include "pho_module_loader.h"
#include "pho_type_utils.h"

#include <dlfcn.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdio.h>

static pthread_rwlock_t modules_rwlock;
static GHashTable *modules;

typedef int (*module_init_func_t)(void *);

/**
 * Initializes modules_rwlock and the modules hash table.
 */
__attribute__((constructor)) static void modules_init(void)
{
    int rc;

    rc = pthread_rwlock_init(&modules_rwlock, NULL);
    if (rc) {
        fprintf(stderr,
                "Unexpected error: cannot initialize modules rwlock: %s\n",
                strerror(rc));
        exit(EXIT_FAILURE);
    }

    modules = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    if (modules == NULL) {
        fprintf(stderr, "Could not create modules hash table\n");
        exit(EXIT_FAILURE);
    }
}

/**
 * Destroys modules_rwlock and the modules hash table.
 */
__attribute__((destructor)) static void modules_destroy(void)
{
    int rc;

    rc = pthread_rwlock_destroy(&modules_rwlock);
    if (rc) {
        fprintf(stderr,
                "Unexpected error: cannot destroy modules rwlock: %s\n",
                strerror(rc));
    }

    g_hash_table_destroy(modules);
}

/**
 * Convert module name into the corresponding module library name.
 * Actual path resolution will be handled by regular LD mechanism.
 *
 * eg. "lib_adapter_dummy" -> "libpho_lib_adapter_dummy.so"
 *
 * \return 0 on success, negated errno on error.
 */
static int build_module_instance_path(const char *mod_name, char *path,
                                      size_t len)
{
    int rc;

    rc = snprintf(path, len, "libpho_%s.so", mod_name);
    if (rc < 0 || rc >= len) {
        path[0] = '\0';
        return -EINVAL;
    }

    return 0;
}

/**
 * Load a module by \a mod_name into \a mod.
 *
 * @param[in]   mod_name    Name of the module to load.
 * @param[in]   mod_size    Size of the structure corresponding to the module.
 * @param[out]  mod         Module descriptor to fill out.
 *
 * @return 0 on success, -errno on error.
 */
static int module_open(const char *mod_name, const ssize_t mod_size,
                       void **mod)
{
    module_init_func_t op_init;
    char modpath[NAME_MAX];
    void *hdl;
    int rc;

    ENTRY;

    pho_debug("Loading module '%s'", mod_name);

    rc = build_module_instance_path(mod_name, modpath, sizeof(modpath));
    if (rc)
        LOG_RETURN(rc, "Invalid module name '%s'", mod_name);

    hdl = dlopen(modpath, RTLD_NOW);
    if (hdl == NULL)
        LOG_RETURN(-EINVAL, "Cannot load module '%s': %s", mod_name, dlerror());

    *mod = calloc(1, mod_size);
    if (*mod == NULL)
        GOTO(out_err, rc = -ENOMEM);

    op_init = dlsym(hdl, PM_OP_INIT);
    if (!op_init)
        LOG_GOTO(out_err, rc = -ENOSYS,
                 "Operation '%s' is missing", PM_OP_INIT);

    rc = op_init(*mod);
    if (rc)
        LOG_GOTO(out_err, rc, "Could not initialize module '%s'", mod_name);

    pho_debug("Module '%s' loaded", mod_name);

out_err:
    if (rc) {
        free(*mod);
        dlclose(hdl);
    }

    return rc;
}

/**
 * Load a module if it has not already been. Relies on the global modules
 * hash map and its associated rwlock.
 *
 * @param[in]   mod_name    Name of the module to load.
 * @param[in]   mod_size    Size of the structure corresponding to the module.
 * @param[out]  mod         Module descriptor to fill out.
 *
 * @return 0 on success, -errno on error.
 */
static int mod_lazy_load(const char *mod_name, const ssize_t mod_size,
                         void **module)
{
    int rc2;
    int rc;

    rc = pthread_rwlock_rdlock(&modules_rwlock);
    if (rc)
        LOG_RETURN(rc, "Cannot read lock module table");

    /* Check if module loaded */
    *module = g_hash_table_lookup(modules, mod_name);

    /* If not loaded, unlock, write lock, check if it is loaded and load it */
    if (*module == NULL) {
        /* Release the read lock */
        rc = pthread_rwlock_unlock(&modules_rwlock);
        if (rc)
            LOG_RETURN(rc, "Cannot unlock module table");

        /* Re-acquire a write lock */
        rc = pthread_rwlock_wrlock(&modules_rwlock);
        if (rc)
            LOG_RETURN(rc, "Cannot write lock module table");

        /*
         * Re-check if module loaded (this could have changed between the read
         * lock release and the write lock acquisition)
         */
        *module = g_hash_table_lookup(modules, mod_name);
        if (*module != NULL)
            goto out_unlock;

        rc = module_open(mod_name, mod_size, module);
        if (rc)
            LOG_GOTO(out_unlock, rc, "Error while loading module %s",
                     mod_name);

        /* Insert the module into the module table */
        g_hash_table_insert(modules, strdup(mod_name), *module);

        /* Write lock is released in function final cleanup */
    }

out_unlock:
    rc2 = pthread_rwlock_unlock(&modules_rwlock);
    if (rc2)
        /* This unlock shouldn't fail as the lock was taken in this function.
         * In case it does, one may not assume thread-safety, so we report this
         * error instead of previous ones.
         */
        LOG_RETURN(rc2, "Cannot unlock module table");

    return rc;
}

int load_module(const char *mod_name, const ssize_t mod_size, void **module)
{
    return mod_lazy_load(mod_name, mod_size, module);
}
