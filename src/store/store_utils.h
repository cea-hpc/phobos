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
 * \brief  Store function prototypes used for unit tests
 */
#ifndef _PHO_STORE_UTILS_H
#define _PHO_STORE_UTILS_H

#include "phobos_store.h"
#include "pho_dss.h"

int object_md_save(struct dss_handle *dss, struct pho_xfer_target *xfer,
                   bool overwrite, const char *grouping);
int object_md_del(struct dss_handle *dss, struct pho_xfer_target *xfer);
int object_md_get(struct dss_handle *dss, struct pho_xfer_target *xfer);

#endif
