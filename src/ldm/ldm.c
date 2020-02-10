/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2017 CEA/DAM.
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

#include <sys/stat.h>
#include <sys/types.h>
#include <stdio.h>

/*
 * Use external references for now.
 * They can easily be replaced later by dlopen'ed symbols.
 */
extern const struct lib_adapter lib_adapter_dummy;
extern const struct lib_adapter lib_adapter_scsi;

int get_lib_adapter(enum lib_type lib_type, struct lib_adapter *lib)
{
    switch (lib_type) {
    case PHO_LIB_DUMMY:
        *lib = lib_adapter_dummy;
        break;
    case PHO_LIB_SCSI:
        *lib = lib_adapter_scsi;
        break;
    default:
        return -ENOTSUP;
    }
    return 0;
}

/*
 * Use external references for now.
 * They can easily be replaced later by dlopen'ed symbols.
 */
extern const struct dev_adapter dev_adapter_dir;
extern const struct dev_adapter dev_adapter_scsi_tape;

int get_dev_adapter(enum rsc_family dev_family, struct dev_adapter *dev)
{
    switch (dev_family) {
    case PHO_RSC_DIR:
        *dev = dev_adapter_dir;
        break;
    case PHO_RSC_TAPE:
        *dev = dev_adapter_scsi_tape;
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
