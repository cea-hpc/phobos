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
 * \brief  Resource file of Phobos's Distributed State Service.
 */

#include <assert.h>

#include "pho_common.h"

#include "deprecated.h"
#include "device.h"
#include "extent.h"
#include "full_layout.h"
#include "layout.h"
#include "logs.h"
#include "media.h"
#include "object.h"
#include "resources.h"

static const struct dss_resource_ops *get_resource_ops(enum dss_type type)
{
    switch (type) {
    case DSS_DEPREC:
        return &deprecated_ops;
    case DSS_DEVICE:
        return &device_ops;
    case DSS_EXTENT:
        return &extent_ops;
    case DSS_FULL_LAYOUT:
        return &full_layout_ops;
    case DSS_LAYOUT:
        return &layout_ops;
    case DSS_LOGS:
        return &logs_ops;
    case DSS_MEDIA:
        return &media_ops;
    case DSS_OBJECT:
        return &object_ops;
    default:
        return NULL;
    }

    UNREACHED();
}

int get_insert_query(enum dss_type type, PGconn *conn, void *void_resource,
                     int item_count, int64_t fields, GString *request)
{
    const struct dss_resource_ops *resource_ops = get_resource_ops(type);

    if (resource_ops == NULL)
        return -ENOTSUP;

    assert(resource_ops->insert_query != NULL);

    return resource_ops->insert_query(conn, void_resource, item_count, fields,
                                      request);
}

int get_update_query(enum dss_type type, PGconn *conn, void *void_resource,
                     int item_count, int64_t fields, GString *request)
{
    const struct dss_resource_ops *resource_ops = get_resource_ops(type);

    if (resource_ops == NULL)
        return -ENOTSUP;

    assert(resource_ops->update_query != NULL);

    return resource_ops->update_query(conn, void_resource, item_count, fields,
                                      request);
}

int get_select_query(enum dss_type type, GString **conditions, int n_conditions,
                     GString *request)
{
    const struct dss_resource_ops *resource_ops = get_resource_ops(type);

    if (resource_ops == NULL)
        return -ENOTSUP;

    return resource_ops->select_query(conditions, n_conditions, request);
}

int get_delete_query(enum dss_type type, void *void_resource, int item_count,
                     GString *request)
{
    const struct dss_resource_ops *resource_ops = get_resource_ops(type);

    if (resource_ops == NULL)
        return -ENOTSUP;

    assert(resource_ops->delete_query != NULL);

    return resource_ops->delete_query(void_resource, item_count, request);
}

int create_resource(enum dss_type type, struct dss_handle *handle,
                    void *void_resource, PGresult *res, int row_num)
{
    const struct dss_resource_ops *resource_ops = get_resource_ops(type);

    if (resource_ops == NULL)
        return -ENOTSUP;

    return resource_ops->create(handle, void_resource, res, row_num);
}

void free_resource(enum dss_type type, void *void_resource)
{
    const struct dss_resource_ops *resource_ops = get_resource_ops(type);

    if (resource_ops == NULL)
        return;

    resource_ops->free(void_resource);
}

size_t get_resource_size(enum dss_type type)
{
    const struct dss_resource_ops *resource_ops = get_resource_ops(type);

    if (resource_ops == NULL)
        return -ENOTSUP;

    return resource_ops->size;
}
