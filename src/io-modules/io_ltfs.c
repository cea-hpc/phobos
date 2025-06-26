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
 * \brief  Phobos I/O LTFS adapter.
 */
#include "io_posix_common.h"
#include "pho_common.h"
#include "pho_module_loader.h"

#include <attr/xattr.h>
#include <sys/types.h>

#define PLUGIN_NAME     "ltfs"
#define PLUGIN_MAJOR    0
#define PLUGIN_MINOR    1

static struct module_desc IO_ADAPTER_LTFS_MODULE_DESC = {
    .mod_name  = PLUGIN_NAME,
    .mod_major = PLUGIN_MAJOR,
    .mod_minor = PLUGIN_MINOR,
};

#define LTFS_SYNC_ATTR_NAME "user.ltfs.sync"

static int pho_ltfs_sync(const char *root_path, json_t **message)
{
    struct phobos_global_context *context = phobos_context();
    int one = 1;

    ENTRY;

    if (message)
        *message = NULL;

    if (context->mocks.mock_ltfs.mock_setxattr == NULL)
        context->mocks.mock_ltfs.mock_setxattr = setxattr;

    /* flush the LTFS partition to tape */
    if (context->mocks.mock_ltfs.mock_setxattr(root_path, LTFS_SYNC_ATTR_NAME,
                                               (void *)&one, sizeof(one),
                                               0) != 0) {
        if (message)
            *message = json_pack(
                "{s:s}", "sync",
                "Failed to set LTFS special xattr " LTFS_SYNC_ATTR_NAME);
        LOG_RETURN(-errno,
                   "failed to set LTFS special xattr " LTFS_SYNC_ATTR_NAME);
    }

    return 0;
}

/** LTFS adapter */
static const struct pho_io_adapter_module_ops IO_ADAPTER_LTFS_OPS = {
    .ioa_get               = pho_posix_get,
    .ioa_del               = pho_posix_del,
    .ioa_open              = pho_posix_open,
    .ioa_write             = pho_posix_write,
    .ioa_read              = pho_posix_read,
    .ioa_close             = pho_posix_close,
    .ioa_medium_sync       = pho_ltfs_sync,
    .ioa_preferred_io_size = pho_posix_preferred_io_size,
    .ioa_set_md            = pho_posix_set_md,
    .ioa_get_common_xattrs_from_extent  = pho_get_common_xattrs_from_extent,
    .ioa_size              = pho_posix_size,
};

/** IO adapter module registration entry point */
int pho_module_register(void *module, void *context)
{
    struct io_adapter_module *self = (struct io_adapter_module *) module;

    phobos_module_context_set(context);

    self->desc = IO_ADAPTER_LTFS_MODULE_DESC;
    self->ops = &IO_ADAPTER_LTFS_OPS;

    return 0;
}
