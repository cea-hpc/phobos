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
 * \brief  Phobos Local Device Manager: dummy library.
 *
 * Dummy library for devices that are always online.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "pho_common.h"
#include "pho_ldm.h"
#include "pho_module_loader.h"

#define PLUGIN_NAME     "dummy"
#define PLUGIN_MAJOR    0
#define PLUGIN_MINOR    1

static struct module_desc LIB_ADAPTER_DUMMY_MODULE_DESC = {
    .mod_name  = PLUGIN_NAME,
    .mod_major = PLUGIN_MAJOR,
    .mod_minor = PLUGIN_MINOR,
};

/**
 * Return drive info for an online device.
 */
static int dummy_drive_lookup(struct lib_handle *lib, const char *drive_serial,
                              struct lib_drv_info *drv_info)
{
    const char  *sep = strchr(drive_serial, ':');

    ENTRY;

    if (sep == NULL || (strlen(sep + 1) + 1) > PHO_URI_MAX)
        return -EINVAL;

    drv_info->ldi_addr.lia_type = MED_LOC_DRIVE;
    drv_info->ldi_addr.lia_addr = 0;
    drv_info->ldi_full = true;

    drv_info->ldi_medium_id.family = PHO_RSC_DIR;

    pho_id_name_set(&drv_info->ldi_medium_id, sep + 1);

    return 0;
}

/** Exported library adapater */
static struct pho_lib_adapter_module_ops LIB_ADAPTER_DUMMY_OPS = {
    .lib_open         = NULL,
    .lib_close        = NULL,
    .lib_drive_lookup = dummy_drive_lookup,
    .lib_scan         = NULL,
    .lib_load         = NULL,
    .lib_unload       = NULL,
};

/** Lib adapter module registration entry point */
int pho_module_register(void *module, void *context)
{
    struct lib_adapter_module *self = (struct lib_adapter_module *) module;

    phobos_module_context_set(context);

    self->desc = LIB_ADAPTER_DUMMY_MODULE_DESC;
    self->ops = &LIB_ADAPTER_DUMMY_OPS;

    return 0;
}
