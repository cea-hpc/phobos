/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2017 CEA/DAM.
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
 * \brief  Phobos Object Store interface
 */
#ifndef _PHO_STORE_H
#define _PHO_STORE_H

#include "pho_attrs.h"
#include "pho_types.h"
#include <stdlib.h>

struct pho_xfer_desc;

/**
 * Transfer (GET / PUT / MPUT) flags.
 * Exact semantic depends on the operation it is applied on.
 */
enum pho_xfer_flags {
    /* put: replace the object if it already exists (_not supported_)
     * get: replace the target file if it already exists */
    PHO_XFER_OBJ_REPLACE    = (1 << 0),
};

/**
 * Multiop completion notification callback.
 * Invoked with:
 *  - user-data pointer
 *  - the operation descriptor
 *  - the return code for this operation: 0 on success, neg. errno on failure
 */
typedef void (*pho_completion_cb_t)(void *u, const struct pho_xfer_desc *, int);

/**
 * Phobos XFer operations.
 */
enum pho_xfer_op {
    PHO_XFER_OP_PUT,   /**< PUT operation. */
    PHO_XFER_OP_GET,   /**< GET operation. */
    PHO_XFER_OP_GETMD  /**< GET metadata operation. */
};

/**
 * PUT parameters.
 */
struct pho_xfer_put_params {
    ssize_t          size;        /**< Amount of data to write. */
    enum rsc_family  family;      /**< Targeted resource family. */
    const char      *layout_name; /**< Name of the layout module to use. */
    const char      *alias;       /**< Identifier for family, layout, tag combination */
    struct tags      tags;        /**< Tags to select a media to write. */
};

/**
 * Operation parameters.
 */
union pho_xfer_params {
    struct pho_xfer_put_params put; /**< PUT parameters. */
};

/**
 * Xfer descriptor.
 * The source/destination semantics of the fields vary
 * depending on the nature of the operation.
 * See below:
 *  - pĥobos_getmd()
 *  - phobos_get()
 *  - phobos_put()
 */
struct pho_xfer_desc {
    char                   *xd_objid;  /**< Object ID to read or write. */
    enum pho_xfer_op        xd_op;     /**< Operation to perform. */
    int                     xd_fd;     /**< FD of the source/destination. */
    struct pho_attrs        xd_attrs;  /**< User defined attributes. */
    union pho_xfer_params   xd_params; /**< Operation parameters. */
    enum pho_xfer_flags     xd_flags;  /**< See enum pho_xfer_flags doc. */
    int                     xd_rc;     /**< Outcome of this xfer. */
};

/**
 * Put N files to the object store with minimal overhead.
 * Each desc entry contains:
 * - objid: the target object identifier
 * - fd: an opened fd to read from
 * - size: amount of data to read from fd
 * - layout_name: (optional) name of the layout module to use
 * - attrs: the metadata (optional)
 * - flags: behavior flags
 * - tags: tags defining constraints on which media can be selected to put the
 *   data
 * Other fields are not used.
 *
 * Individual completion notifications are issued via xd_callback.
 * This function returns the first encountered error or 0 if all
 * sub-operations have succeeded.
 */
int phobos_put(struct pho_xfer_desc *xfers, size_t n,
               pho_completion_cb_t cb, void *udata);

/**
 * Retrieve N files from the object store
 * desc contains:
 * - objid: identifier of the object to retrieve
 * - fd: an opened fd to write to
 * - attrs: unused (can be NULL)
 * - flags: behavior flags
 * Other fields are not used.
 *
 * Individual completion notifications are issued via xd_callback.
 * This function returns the first encountered error or 0 if all
 * sub-operations have succeeded.
 */
int phobos_get(struct pho_xfer_desc *xfers, size_t n,
               pho_completion_cb_t cb, void *udata);

/**
 * Retrieve N file metadata from the object store
 * desc contains:
 * - objid: identifier of the object to retrieve
 * - attrs: unused (can be NULL)
 * - flags: behavior flags
 * Other fields are not used.
 *
 * Individual completion notifications are issued via xd_callback.
 * This function returns the first encountered error of 0 if all
 * sub-operations have succeeded.
 */
int phobos_getmd(struct pho_xfer_desc *xfers, size_t n,
                 pho_completion_cb_t cb, void *udata);

/** query metadata of the object store */
/* TODO int phobos_query(criteria, &obj_list); */
/* TODO int phobos_del(); */

/**
 * Free tags and attributes resources associated with this xfer,
 * as they are allocated in phobos.
 */
void pho_xfer_desc_destroy(struct pho_xfer_desc *xfer);

/**
 * Retrieve the objects that match the given pattern and metadata.
 *
 * The caller must release the list calling phobos_store_object_list_free().
 *
 * \param[in]       pattern         Shell regexp pattern.
 * \param[in]       metadata        Metadata filter.
 * \param[in]       n_metadata      Number of requested metadata.
 * \param[out]      objs            Retrieved objects.
 * \param[out]      n_objs          Number of retrieved items.
 *
 * \return                          0     on success,
 *                                 -errno on failure.
 */
int phobos_store_object_list(const char *pattern, const char **metadata,
                             int n_metadata, struct object_info **objs,
                             int *n_objs);

/**
 * Release the list retrieved using phobos_store_object_list().
 *
 * \param[in]       objs            Objects to release.
 * \param[in]       n_objs          Number of objects to release.
 */
void phobos_store_object_list_free(struct object_info *objs, int n_objs);

#endif
