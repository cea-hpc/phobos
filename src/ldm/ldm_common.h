/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 * Copyright 2015-2016 CEA/DAM. All Rights Reserved.
 */
/**
 * \brief  Common functions for LDM adapters.
 */
#ifndef _LDM_COMMON_H
#define _LDM_COMMON_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "pho_ldm.h"

#include <mntent.h>

/**
 * Callback function for mntent iterator.
 * @param mntent  Current mount entry.
 * @param cb_data Callback specific data.
 * @retval 0 to continue iterating on next mnt ent entries.
 * @retval != 0 to stop iterating and return this value to caller.
 */
typedef int (*mntent_cb_t)(const struct mntent *mntent, void *cb_data);

/**
 * Iterate on mounted filesystems.
 * @param cb_func  Callback function to be called for each mounted filesystem.
 * @param cb_data  Pointer to be passed to callback function.
 *
 * @retval 0 if all cb_func returned 0 (iterated over all mntent entries).
 * @retval != 0 if any cb_func return != 0 (iteration was stopped).
 */
int mnttab_foreach(mntent_cb_t cb_func, void *cb_data);

/**
 * Standard implementation of 'df' using statfs().
 */
int common_statfs(const char *path, struct ldm_fs_space *fs_spc);

#endif
