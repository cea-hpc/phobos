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
 * \brief  Phobos admin header for removal of lost media
 */

#ifndef _PHO_ADMIN_LOST_H
#define _PHO_ADMIN_LOST_H

#include "phobos_admin.h"

#include "pho_types.h"

/**
 * Delete a list of media from the database, all the extents associated with it,
 * and update the objects and copies to see if they are still readable or not.
 *
 * @param[in]  adm          Admin handle
 * @param[in]  media_list   array of entries to delete
 * @param[in]  media_count  number of items in the list
 *
 * @return 0 on success, negated errno on failure
 */
int delete_media_and_extents(struct admin_handle *adm,
                             struct media_info *media_list,
                             int media_count);

#endif
