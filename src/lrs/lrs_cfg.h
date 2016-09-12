/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 * Copyright 2014-2016 CEA/DAM. All Rights Reserved.
 */
/**
 * \brief  Phobos Local Resource Scheduler configuration utilities.
 */
#ifndef _PHO_LRS_CFG_H
#define _PHO_LRS_CFG_H

#include "pho_cfg.h"

/** List of LRS configuration parameters */
enum pho_cfg_params_lrs {
    PHO_CFG_LRS_FIRST,

    /* lrs parameters */
    PHO_CFG_LRS_mount_prefix = PHO_CFG_LRS_FIRST,
    PHO_CFG_LRS_policy,
    PHO_CFG_LRS_default_family,
    PHO_CFG_LRS_lib_device,

    PHO_CFG_LRS_LAST
};

extern const struct pho_config_item cfg_lrs[];

#endif
