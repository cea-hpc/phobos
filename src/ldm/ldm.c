/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 * Copyright 2014-2015 CEA/DAM. All Rights Reserved.
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
extern const struct dev_adapter dev_adapter_lintape;

int get_dev_adapter(enum dev_family dev_type, struct dev_adapter *dev)
{
    switch (dev_type) {
    case PHO_DEV_DIR:
        *dev = dev_adapter_dir;
        break;
    case PHO_DEV_TAPE:
        *dev = dev_adapter_lintape;
        break;
    default:
        return -ENOTSUP;
    }
    return 0;
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
