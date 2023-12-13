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
 * \brief  Phobos I/O adapters.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "pho_cfg.h"
#include "pho_common.h"
#include "pho_io.h"
#include "pho_module_loader.h"

#define IO_BLOCK_SIZE_ATTR_KEY "io_block_size"

/**
 * List of configuration parameters for this module
 */
enum pho_cfg_params_io {
    /* Actual parameters */
    PHO_CFG_IO_io_block_size,

    /* Delimiters, update when modifying options */
    PHO_CFG_IO_FIRST = PHO_CFG_IO_io_block_size,
    PHO_CFG_IO_LAST  = PHO_CFG_IO_io_block_size,
};

const struct pho_config_item cfg_io[] = {
    [PHO_CFG_IO_io_block_size] = {
        .section = "io",
        .name    = IO_BLOCK_SIZE_ATTR_KEY,
        .value   = "0" /** default value = not set */
    },
};

int get_io_block_size(size_t *size)
{
    const char *string_io_block_size;
    int64_t sz;

    string_io_block_size = PHO_CFG_GET(cfg_io, PHO_CFG_IO, io_block_size);
    if (!string_io_block_size) {
        /* If not forced by configuration, the io adapter will retrieve it
         * from the backend storage system.
         */
        *size = 0;
        return 0;
    }

    sz = str2int64(string_io_block_size);
    if (sz < 0) {
        *size = 0;
        LOG_RETURN(-EINVAL, "Invalid value '%s' for parameter '%s'",
                   string_io_block_size, IO_BLOCK_SIZE_ATTR_KEY);
    }

    *size = sz;
    return 0;
}

void get_preferred_io_block_size(size_t *io_size,
                                 const struct io_adapter_module *ioa,
                                 struct pho_io_descr *iod)
{
    ssize_t sz;

    get_io_block_size(io_size);
    if (*io_size != 0)
        return;

    sz = ioa_preferred_io_size(ioa, iod);
    if (sz > 0) {
        *io_size = sz;
        return;
    }

    /* fallback: get the system page size */
    *io_size = sysconf(_SC_PAGESIZE);
}

/** retrieve IO functions for the given filesystem and addressing type */
int get_io_adapter(enum fs_type fstype, struct io_adapter_module **ioa)
{
    int rc = 0;

    switch (fstype) {
    case PHO_FS_POSIX:
        rc = load_module("io_adapter_posix", sizeof(**ioa), phobos_context(),
                         (void **)ioa);
        break;
    case PHO_FS_LTFS:
        rc = load_module("io_adapter_ltfs", sizeof(**ioa), phobos_context(),
                         (void **)ioa);
        break;
    case PHO_FS_RADOS:
        rc = load_module("io_adapter_rados", sizeof(**ioa), phobos_context(),
                         (void **)ioa);
        break;
    default:
        pho_error(-EINVAL, "Invalid FS type %#x", fstype);
        return -EINVAL;
    }

    return rc;
}
