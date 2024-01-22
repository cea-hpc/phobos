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
 * \brief  Common functions for LDM adapters.
 */
#ifndef _LDM_COMMON_H
#define _LDM_COMMON_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "pho_ldm.h"

#include <mntent.h>
#include <jansson.h>

#define PHO_LDM_HELPER "/usr/sbin/pho_ldm_helper"

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
int simple_statfs(const char *path, struct ldm_fs_space *fs_spc);

/*
 * Same as simple_statfs, but will log an error to \p message if the statfs
 * fails
 */
int logged_statfs(const char *path, struct ldm_fs_space *fs_spc,
                  json_t **message);

/**
 * Build a command to mount a LTFS filesystem at a given path.
 * The result must be released by the caller using free(3).
 */
char *ltfs_mount_cmd(const char *device, const char *path);

/**
 * Build a command to unmount a LTFS filesystem at a given path.
 * The result must be released by the caller using free(3).
 */
char *ltfs_umount_cmd(const char *device, const char *path);

/**
 * Build a command to format a LTFS filesystem with the given label.
 * The result must be released by the caller using free(3).
 */
char *ltfs_format_cmd(const char *device, const char *label);

#endif
