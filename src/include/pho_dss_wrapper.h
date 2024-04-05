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
 * \brief  Phobos Distributed State Service API.
 */
#ifndef _PHO_DSS_WRAPPER_H
#define _PHO_DSS_WRAPPER_H

#include "pho_types.h"
#include "pho_cfg.h"
#include "pho_common.h"

#include <stdint.h>
#include <stdlib.h>

/**
 * Retrieve usable devices information from DSS, meaning devices that are
 * unlocked.
 *
 * @param[in]  hdl      valid connection handle
 * @param[in]  family   family of the devices to retrieve
 * @param[in]  host     host of the devices to retrieve, if NULL, will retrieve
 *                      usable devices of every host
 * @param[out] dev_ls   list of retrieved items to be freed w/ dss_res_free()
 * @param[out] dev_cnt  number of items retrieved in the list
 *
 * @return 0 on success, negated errno on failure
 */
int dss_get_usable_devices(struct dss_handle *hdl, const enum rsc_family family,
                           const char *host, struct dev_info **dev_ls,
                           int *dev_cnt);

/**
 * Return the health of a device
 *
 * The health is computed as the number of errors encountered by the device
 * minus the number of successes since the first error.
 *
 * The health is always >= 0. A health of 0 implies that the device is failed.
 * It also has a maximum of \p max_health.
 *
 * @param[in]  dss         Valid DSS handle
 * @param[in]  device_id   Device to query
 * @param[in]  max_health  Maximum health that the device can have
 * @param[out] health      The returned health of the device
 *
 * @return     0 on success, negative POSIX error code on error
 */
int dss_device_health(struct dss_handle *dss, const struct pho_id *device_id,
                      size_t max_health, size_t *health);

/**
 * Get only one medium info from DSS
 *
 * @param[in]   dss         DSS handle
 * @param[in]   medium_id   medium id to get from DSS
 * @param[out]  medium_info medium info retrieved from DSS or NULL if failure
 *                          (output medium_info must be cleaned by calling
 *                           dss_res_free(medium_info, 1))
 *
 * @return 0 on success, negated errno on failure
 */
int dss_one_medium_get_from_id(struct dss_handle *dss,
                               const struct pho_id *medium_id,
                               struct media_info **medium_info);

/**
 * Locate a medium
 *
 * If the medium is locked by a server, its hostname is copied from the lock
 * and returned.
 *
 * If the medium of the family dir is not locked by anyone, -ENODEV is returned
 * because it means that the corresponding LRS is not started or that the medium
 * is failed.
 * If the medium is not locked by anyone and its family is not dir, NULL is
 * returned as hostname.
 *
 * @param[in]   dss         DSS to request
 * @param[in]   medium_id   Medium to locate
 * @param[out]  hostname    Allocated and returned hostname or NULL if the
 *                          medium is not locked by anyone
 * @param[out]  medium_info Allocated and returned additionnal information about
 *                          the medium or NULL if the medium couldn't be
 *                          retrieved
 *
 * @return 0 if success, -errno if an error occurs and hostname is irrelevant
 *         -ENOENT if no medium with medium_id exists in media table
 *         -ENODEV if a medium dir is not currently locked
 *         -EACCES if medium is admin locked
 *         -EPERM if medium get operation flag is set to false
 */
int dss_medium_locate(struct dss_handle *dss, const struct pho_id *medium_id,
                      char **hostname, struct media_info **medium_info);

/**
 * Return the health of a medium
 *
 * The health is computed as the number of errors encountered by the medium
 * minus the number of successes since the first error.
 *
 * The health is always >= 0. A health of 0 implies that the medium is failed.
 * It also has a maximum of \p max_health.
 *
 * @param[in]  dss         Valid DSS handle
 * @param[in]  medium_id   Medium to query
 * @param[in]  max_health  Maximum health that the medium can have
 * @param[out] health      The returned health of the medium
 *
 * @return     0 on success, negative POSIX error code on error
 */
mockable
int dss_medium_health(struct dss_handle *dss, const struct pho_id *medium_id,
                      size_t max_health, size_t *health);

/**
 * Find the corresponding object
 *
 * This function is lazy because there is no lock and the existing objects could
 * change any time.
 *
 * At least one of \p oid or \p uuid must not be NULL.
 *
 * If \p version is not provided (zero as input) the latest one is located.
 *
 * If \p uuid is not provided, we first try to find the corresponding \p oid
 * from living objects into the object table. If there is no living \p oid, we
 * check amongst all deprecated objects. If there is only one corresponding
 * \p uuid in the deprecated objects, we take it. If there is more than one
 * \p uuid corresponding to this \p oid, -EINVAL is returned. If there is no
 * existing object corresponding to provided oid/uuid/version, -ENOENT is
 * returned.
 *
 * @param[in]   oid     OID to find or NULL
 * @param[in]   uuid    UUID to find or NULL
 * @param[in]   version Version to find or 0
 * @param[out]  obj     Found and allocated object or NULL if error
 *
 * @return 0 or negative error code
 */
int dss_lazy_find_object(struct dss_handle *hdl, const char *oid,
                         const char *uuid, int version,
                         struct object_info **obj);

/**
 * Move an object from the "object" table to the "deprecated_object" table
 *
 * @param[in] handle    DSS handle
 * @param[in] obj_list  Objects to move, oids must be filled
 * @param[in] obj_count Number of objects
 *
 * @return              0 if success, negated errno code on failure
 */
int dss_move_object_to_deprecated(struct dss_handle *handle,
                                  struct object_info *obj_list,
                                  int obj_cnt);

/**
 * Move an object from the "deprecated_object" table to the "object" table
 *
 * @param[in] handle    DSS handle
 * @param[in] obj_list  Objects to move, uuid and version must be filled
 * @param[in] obj_count Number of objects
 *
 * @return              0 if success, negated errno code on failure
 */
int dss_move_deprecated_to_object(struct dss_handle *handle,
                                  struct object_info *obj_list,
                                  int obj_cnt);

/**
 * Update the layout and extent databases following an extent migrate action:
 * - all \p old_uuid occurences will be replaced by \p new_uuid in layout;
 * - \p old_uuid and \p new_uuid states will be respectively changed to orphan
 *   and sync in extent.
 *
 * @param[in]   handle          DSS handle
 * @param[in]   old_uuid        Old extent UUID
 * @param[in]   new_uuid        New extent UUID
 *
 * @return 0 on success, -errno on failure
 */
int dss_update_extent_migrate(struct dss_handle *handle, const char *old_uuid,
                              const char *new_uuid);

#endif
