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
 * \brief  RAID1 layout pieces of code shared for testing purpose
 */
#ifndef _PHO_RAID1_H
#define _PHO_RAID1_H

#include "pho_types.h" /* struct layout_info */

/**
 * Replica count parameter comes from configuration.
 * It is saved in layout REPL_COUNT_ATTR_KEY attr in a char * value and in the
 * private raid1 encoder unsigned int repl_count value.
 */
#define REPL_COUNT_ATTR_KEY "repl_count"
#define REPL_COUNT_ATTR_VALUE_BASE 10

/**
 * Computing the XXH128 of each extent is disabled by the configuration if
 * EXTENT_XXH128_ATTR_KEY is set to anything other than "yes"
 */
#define EXTENT_XXH128_ATTR_KEY "extent_xxh128"

/**
 * Computing the MD5 of each extent is disabled by the configuration if
 * EXTENT_MD5_ATTR_KEY is set to anything other than "yes"
 */
#define EXTENT_MD5_ATTR_KEY "extent_md5"

/**
 * Set unsigned int replica count value from char * layout attr
 *
 * 0 is not a valid replica count, -EINVAL will be returned.
 *
 * @param[in]  layout     layout with a REPL_COUNT_ATTR_KEY
 * @param[out] repl_count replica count value to set
 *
 * @return 0 if success,
 *         -error_code if failure and \p repl_count value is irrelevant
 */
int layout_repl_count(struct layout_info *layout, unsigned int *repl_count);

/**
 * Retrieve one node name from which an object can be accessed
 *
 * Implement layout_locate layout module methods.
 *
 * @param[in]   layout      Layout of the object to locate
 * @param[in]   focus_host  Hostname on which the caller would like to access
 *                          the object if there is no node more convenient (if
 *                          NULL, focus_host is set to local hostname)
 * @param[out]  hostname    Allocated and returned hostname of the node that
 *                          gives access to the object (NULL is returned on
 *                          error)
 * @param[out]  nb_new_lock Number of new locks on media added for the returned
 *                          hostname
 *
 * @return                  0 on success or -errno on failure,
 *                          -ENODEV if there is no existing medium to retrieve
 *                          a split
 *                          -EINVAL on invalid replica count
 *                          -EAGAIN if there is not any convenient node to
 *                          currently retrieve this object
 *                          -EADDRNOTAVAIL if we cannot get self hostname
 */
int layout_raid1_locate(struct dss_handle *dss, struct layout_info *layout,
                        const char *focus_host, char **hostname,
                        int *nb_new_lock);

#endif
