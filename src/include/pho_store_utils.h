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
 * \brief   Store related internal helpers.
 */
#ifndef _PHO_STORE_UTILS_H
#define _PHO_STORE_UTILS_H

#include <stddef.h>

#include "phobos_store.h"

/**
 * Retrieve the xd_fd field of the \a xfer, opening xd_fpath with NOATIME if the
 * \a xfer has not been previously opened.
 *
 * @return the fd or -errno on error. If xd_fd was not positioned and xd_fpath
 * is NULL, returns -EINVAL.
 */
int pho_xfer_desc_get_fd(struct pho_xfer_desc *xfer);

/**
 * Retrieve the xd_size field of the \a xfer, opening xd_fd (with
 * pho_xfer_desc_get_fd) and stating it if xd_size was not already set to a
 * positive number.
 *
 * @return the updated xd_size field or -errno on error.
 */
ssize_t pho_xfer_desc_get_size(struct pho_xfer_desc *xfer);

#endif /* _PHO_STORE_UTILS_H */
