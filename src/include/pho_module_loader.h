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
#ifndef _PHO_MODULE_LOADER_H
#define _PHO_MODULE_LOADER_H

#include "pho_types.h"
#include <assert.h>
#include <jansson.h>

#define PM_OP_INIT         "pho_module_register"

int pho_module_register(void *module);

typedef int (*module_init_func_t)(void *);

/**
 * Load a module with \a module_name into \a module.
 * The module will be malloc'ed according to \a mod_size.
 *
 * @param[in]   mod_name    Name of the module to load.
 * @param[in]   mod_size    Size of the structure corresponding to the module.
 * @param[out]  mod         Module descriptor to fill out.
 *
 * @return 0 on success, -errno on error.
 */
int load_module(const char *mod_name, const ssize_t mod_size, void **module);

#endif
