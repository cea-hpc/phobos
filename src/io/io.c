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

#include "pho_common.h"
#include "pho_io.h"
#include "pho_module_loader.h"

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
