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
 * \brief  Phobos admin import header
 */

#ifndef _PHO_ADMIN_IMPORT_H
#define _PHO_ADMIN_IMPORT_H

#include "phobos_admin.h"

#include "pho_srl_lrs.h"

/**
 * Import the content of the medium
 *
 * @param[in] adm           Admin module handler.
 * @param[in] medium        Medium to import the content of.
 * @param[in] check_hash    Option to know if a recalculation of hashs has to
 *                          be remade.
 *
 * @return 0 on success, -errno on failure.
 */
int import_medium(struct admin_handle *adm, struct media_info *medium,
                  bool check_hash);

/**
 * Reconstructs a copy, which means updating its copy_status
 * to either "incomplete", "readable" or "complete".
 *
 * @param[in]   adm         Admin handle,
 * @param[in]   copy        copy to reconstruct,
 *
 * @return 0 on success, -errno on failure.
 */
int reconstruct_copy(struct admin_handle *adm, struct copy_info *copy);

#endif
