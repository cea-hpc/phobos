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

#include "pho_cfg.h"
#include "pho_common.h"
#include "pho_ldm.h"
#include "pho_module_loader.h"
#include "pho_type_utils.h"

#include <dlfcn.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdio.h>

int get_lib_adapter(enum lib_type lib_type, struct lib_adapter_module **lib)
{
    int rc = 0;

    switch (lib_type) {
    case PHO_LIB_DUMMY:
        rc = load_module("lib_adapter_dummy", sizeof(**lib), (void **)lib);
        break;
    case PHO_LIB_SCSI:
        rc = load_module("lib_adapter_scsi", sizeof(**lib), (void **)lib);
        break;
    default:
        return -ENOTSUP;
    }

    return rc;
}

int get_dev_adapter(enum rsc_family dev_family, struct dev_adapter *dev)
{
    struct dev_adapter_module *dev_mod;
    int rc = 0;

    switch (dev_family) {
    case PHO_RSC_DIR:
        rc = load_module("dev_adapter_dir", sizeof(*dev_mod),
                         (void **)&dev_mod);
        *dev = *dev_mod->ops;
        break;
    case PHO_RSC_TAPE:
        rc = load_module("dev_adapter_scsi_tape", sizeof(*dev_mod),
                         (void **)&dev_mod);
        *dev = *dev_mod->ops;
        break;
    case PHO_RSC_RADOS_POOL:
#ifdef RADOS_ENABLED
        rc = load_module("dev_adapter_rados_pool", sizeof(*dev_mod),
                         (void **)&dev_mod);
        *dev = *dev_mod->ops;
#else
        LOG_RETURN(-ENOTSUP, "Phobos has been built without the necessary "
                             "RADOS modules");
#endif
        break;
    default:
        return -ENOTSUP;
    }

    return rc;
}

void ldm_dev_state_fini(struct ldm_dev_state *lds)
{
    free(lds->lds_model);
    free(lds->lds_serial);
    lds->lds_model = NULL;
    lds->lds_serial = NULL;
}

int get_fs_adapter(enum fs_type fs_type, struct fs_adapter *fsa)
{
    struct fs_adapter_module *fsa_mod;
    int rc = 0;

    switch (fs_type) {
    case PHO_FS_POSIX:
        rc = load_module("fs_adapter_posix", sizeof(*fsa_mod),
                         (void **)&fsa_mod);
        *fsa = *fsa_mod->ops;
        break;
    case PHO_FS_LTFS:
        rc = load_module("fs_adapter_ltfs", sizeof(*fsa_mod),
                         (void **)&fsa_mod);
        *fsa = *fsa_mod->ops;
        break;
    default:
        return -ENOTSUP;
    }

    return rc;
}

int ldm_dev_query(const struct dev_adapter *dev, const char *dev_path,
                  struct ldm_dev_state *lds)
{
    assert(dev != NULL);
    assert(dev->dev_query != NULL);
    return dev->dev_query(dev_path, lds);
}
