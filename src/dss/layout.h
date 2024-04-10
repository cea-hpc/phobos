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
 * \brief  Layout resource header of Phobos's Distributed State Service.
 */

#ifndef _PHO_DSS_LAYOUT_H
#define _PHO_DSS_LAYOUT_H

#include "resources.h"

/**
 * The "layout" operations structure.
 * Implements every function of the structure except "update_query" and
 * "delete_query".
 */
extern const struct dss_resource_ops layout_ops;

/**
 * Decode the layout description from a given \p json.
 *
 * The description encompass the layout name, major and minor version numbers,
 * and optionnally, any additional attributes.
 *
 * \param[out] desc        The layout module description to fill
 * \param[in]  hash_field  The json field in which to search the fields listed
 *                         above
 *
 * \return 0 on success, -EINVAL if \p json is not a json object
 *                       negative error code otherwise
 */
int layout_desc_decode(struct module_desc *desc, const char *json);

#endif
