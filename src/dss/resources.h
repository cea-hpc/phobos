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
 * \brief  Phobos Distributed State Service API.
 */
#ifndef _PHO_DSS_RESOURCES_H
#define _PHO_DSS_RESOURCES_H

#include <gmodule.h>
#include <libpq-fe.h>
#include <stddef.h>

#include "pho_dss.h"

struct dss_resource_ops {
    int (*insert_query)(PGconn *conn, void *void_resource, int item_count,
                        GString *request);
    int (*update_query)(void *void_resource, int item_count,
                        int64_t fields, GString *request);
    int (*select_query)(GString *conditions, GString *request);
    int (*delete_query)(void *void_resource, int item_count, GString *request);
    int (*create)(struct dss_handle *handle, void *void_resource,
                  PGresult *res, int row_num);
    void (*free)(void *void_resource);

    size_t size;
};

#endif
