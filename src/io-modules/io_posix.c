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
 * \brief  Phobos I/O POSIX adapter.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "io_posix_common.h"
#include "pho_common.h"
#include "pho_module_loader.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define PLUGIN_NAME     "posix"
#define PLUGIN_MAJOR    0
#define PLUGIN_MINOR    1

static struct module_desc IO_ADAPTER_POSIX_MODULE_DESC = {
    .mod_name  = PLUGIN_NAME,
    .mod_major = PLUGIN_MAJOR,
    .mod_minor = PLUGIN_MINOR,
};

static int pho_posix_medium_sync(const char *root_path)
{
    int rc = 0;
    int fd;

    ENTRY;

    fd = open(root_path, O_RDONLY);
    if (fd == -1)
        return -errno;

    if (syncfs(fd))
        rc = -errno;

    if (close(fd) && !rc)
        return -errno;

    return rc;
}

/** POSIX adapter */
static const struct pho_io_adapter_module_ops IO_ADAPTER_POSIX_OPS = {
    .ioa_get               = pho_posix_get,
    .ioa_del               = pho_posix_del,
    .ioa_open              = pho_posix_open,
    .ioa_write             = pho_posix_write,
    .ioa_close             = pho_posix_close,
    .ioa_medium_sync       = pho_posix_medium_sync,
    .ioa_preferred_io_size = pho_posix_preferred_io_size,
    .ioa_set_md            = pho_posix_set_md,
};

/** IO adapter module registration entry point */
int pho_module_register(void *module, void *context)
{
    struct io_adapter_module *self = (struct io_adapter_module *) module;

    phobos_module_context_set(context);

    self->desc = IO_ADAPTER_POSIX_MODULE_DESC;
    self->ops = &IO_ADAPTER_POSIX_OPS;

    return 0;
}
