/*
 *  All rights reserved (c) 2014-2025 CEA/DAM.
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
 * \brief  Phobos admin header for utilities
 */

#ifndef _PHO_ADMIN_LOST_UTILS_H
#define _PHO_ADMIN_LOST_UTILS_H

#include "phobos_admin.h"

#include "pho_types.h"

/**
 * Retrieve the list of extents on a given medium. If \p no_orphan is true, only
 * retrieve non-orphan extents.
 *
 * @param[in]  adm          Admin handle
 * @param[in]  source       The id of the medium to use
 * @param[out] extents      The list of extents on that medium
 * @param[out] count        The size of the \p extents array
 * @param[in]  no_orphan    Whether we should only retrieve non-orphan extents
 *                          or not
 *
 * @return 0 on success, negated errno on failure
 */
int get_extents_from_medium(struct admin_handle *adm,
                            const struct pho_id *source,
                            struct extent **extents, int *count,
                            bool no_orphan);

#endif
