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
 * \brief  Extent resource header of Phobos's Distributed State Service.
 */

#ifndef _PHO_DSS_EXTENT_H
#define _PHO_DSS_EXTENT_H

#include "resources.h"

/**
 * The "extent" operations structure.
 * Implements every function of the structure except "delete_query".
 */
extern const struct dss_resource_ops extent_ops;

/**
 * Decode the extent hash from a given json and store the result in \p extent.
 *
 * \param[out] extent      The extent in which to store the MD5 and XXH128 hash
 * \param[in]  hash_field  The json field in which to search the hashes
 *
 * \return 0 on success, -EINVAL if \p hash_field is not a json object
 *                       negative error code otherwise
 */
int dss_extent_hash_decode(struct extent *extent, json_t *hash_field);

#endif
