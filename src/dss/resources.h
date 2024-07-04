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
 * \brief  Resource header of Phobos's Distributed State Service API.
 */

#ifndef _PHO_DSS_RESOURCES_H
#define _PHO_DSS_RESOURCES_H

#include <gmodule.h>
#include <libpq-fe.h>
#include <stddef.h>

#include "pho_dss.h"

/**
 * This structure defines the basic functions a resource should provide to be
 * used by the DSS. It also contains the size of the resource's structure.
 *
 * Some calls are strictly mandatory, and assertion failures may happen if they
 * are found to be missing. Others aren't for which the wrappers will return
 * -ENOTSUP if they are not implemented in an IOA.
 *
 * Refer to the documentation of wrappers for what exactly is expected, done and
 * returned by the functions.
 */
struct dss_resource_ops {
    int (*insert_query)(PGconn *conn, void *void_resource, int item_count,
                        int64_t fields, GString *request);
    int (*update_query)(PGconn *conn, void *void_resource, int item_count,
                        int64_t fields, GString *request);
    int (*select_query)(GString **conditions, int n_conditions,
                        GString *request, struct dss_sort *sort);
    int (*delete_query)(void *void_resource, int item_count, GString *request);
    int (*create)(struct dss_handle *handle, void *void_resource,
                  PGresult *res, int row_num);
    void (*free)(void *void_resource);

    size_t size;
};

/**
 * Get the insert query of a resource into \p request.
 *
 * \param[in]  type           The resource type whose insert_query function
 *                            should be called
 * \param[in]  conn           The database connection, mainly used for string
 *                            escaping
 * \param[in]  void_resource  The resources to create the insert query with
 * \param[in]  item_count     The number of resources to create the insert query
 *                            with
 * \param[in]  fields         Additionnal fields used to specify the type of
 *                            insert
 * \param[out] request        The request in which to put the insert query
 *
 * \return 0 on success, -ENOTSUP if the resource does not permit inserting
 *                                values
 *                       negative error code otherwise
 */
int get_insert_query(enum dss_type type, PGconn *conn, void *void_resource,
                     int item_count, int64_t fields, GString *request);

/**
 * Get the update query of a resource into \p request.
 *
 * \param[in]  type           The resource type whose update_query function
 *                            should be called
 * \param[in]  conn           The database connection, mainly used for string
 *                            escaping
 * \param[in]  void_resource  The resources to create the update query with
 * \param[in]  item_count     The number of resources to create the update query
 *                            with
 * \param[in]  fields         Additionnal fields used to specify the type of
 *                            update
 * \param[out] request        The request in which to put the update query
 *
 * \return 0 on success, -ENOTSUP if the resource does not permit updating
 *                                values
 *                       negative error code otherwise
 */
int get_update_query(enum dss_type type, PGconn *conn, void *void_resource,
                     int item_count, int64_t fields, GString *request);

/**
 * Get the select query of a resource into \p request.
 *
 * \param[in]  type           The resource type whose select_query function
 *                            should be called
 * \param[in]  conditions     The conditions of the resources to select
 * \param[in]  n_conditions   The number of condition
 * \param[out] request        The request in which to put the select query
 *
 * \return 0 on success, -ENOTSUP if the resource does not permit select
 *                                values
 *                       negative error code otherwise
 */
int get_select_query(enum dss_type type, GString **conditions, int n_conditions,
                     GString *request, struct dss_sort *sort);

/**
 * Get the delete query of a resource into \p request.
 *
 * \param[in]  type           The resource type whose delete_query function
 *                            should be called
 * \param[in]  void_resource  The resources to create the delete query with
 * \param[in]  item_count     The number of resources to create the delete query
 *                            with
 * \param[out] request        The request in which to put the delete query
 *
 * \return 0 on success, -ENOTSUP if the resource does not permit deleting
 *                                values
 *                       negative error code otherwise
 */
int get_delete_query(enum dss_type type, void *void_resource, int item_count,
                     GString *request);

/**
 * Create a resource from the result of a database query.
 *
 * \param[in]  type           The resource type whose create_resource function
 *                            should be called
 * \param[in]  handle         The handle to the DSS, may be needed for
 *                            sub-queries
 * \param[out] void_resource  The resource to create
 * \param[in]  res            The result of the database query
 * \param[in]  row_num        The row number to create the resource from
 *
 * \return 0 on success, -ENOTSUP if the resource does not permit creating
 *                                resources
 *                       negative error code otherwise
 */
int create_resource(enum dss_type type, struct dss_handle *handle,
                    void *void_resource, PGresult *res, int row_num);

/**
 * Free a resource
 *
 * \param[in]  type           The resource type whose free_resource function
 *                            should be called
 * \param[out] void_resource  The resource to free
 */
void free_resource(enum dss_type type, void *void_resource);

/**
 * Get the resource's size.
 *
 * \param[in]  type           The resource type whose size should be returned
 */
size_t get_resource_size(enum dss_type type);

#endif
