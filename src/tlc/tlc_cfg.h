/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2026 CEA/DAM.
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
 * \brief  Phobos Tape Library Controler configuration utilities.
 */
#ifndef _PHO_TLC_CFG_H
#define _PHO_TLC_CFG_H

#include "pho_cfg.h"
#include "pho_types.h"

#define DEFAULT_TLC_MAX_DEVICE_RETRY 1

/** List of TLC configuration parameters */
enum pho_cfg_params_tlc {
    PHO_CFG_TLC_FIRST,

    /* tlc parameters */
    PHO_CFG_TLC_lib_device = PHO_CFG_TLC_FIRST,
    PHO_CFG_TLC_default_library,
    PHO_CFG_TLC_retry_count,

    PHO_CFG_TLC_LAST = PHO_CFG_TLC_retry_count,
};

extern const struct pho_config_item cfg_tlc[];

#endif
