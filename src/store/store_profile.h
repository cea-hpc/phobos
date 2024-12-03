/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2024 CEA/DAM.
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
 * \brief  profile specific function header of Phobos store
 */
#ifndef _STORE_PROFILE_H
#define _STORE_PROFILE_H

#include "phobos_store.h"

/**
 * Fill the struct pho_xfer_put_params with data from the cfg.
 * If a profile is given, the corresponding values are loaded and added if
 * nothing else was specified explicitely beforehand.
 *
 * @param[in]   xfer    Phobos pho_xfer_desc to update
 *
 * @return 0 on success, -errno on error.
 */
int fill_put_params(struct pho_xfer_desc *xfer);

#endif
