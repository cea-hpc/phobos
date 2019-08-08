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
    /* put: ignored
     * get: retrieve object metadata only (no data movement) */
    PHO_XFER_OBJ_GETATTR    = (1 << 1),
};

/**
 * Multiop completion notification callback.
 * Invoked with:
 *  - user-data pointer
 *  - the operation descriptor
 *  - the return code for this operation: 0 on success, neg. errno on failure
 */
typedef void (*pho_completion_cb_t)(void *u, const struct pho_xfer_desc *, int);

enum pho_xfer_op {
    PHO_XFER_OP_PUT, /**< Put operation */
    PHO_XFER_OP_GET, /**< Get operation */
    /* TODO: GETATTR operation */
};

/**
 * GET / PUT parameter.
 * The source/destination semantics of the fields vary
 * depending on the nature of the operation.
 * See below:
 *  - phobos_get()
 *  - phobos_put()
 */
struct pho_xfer_desc {
    char                *xd_objid;   /**< Object id to read or write */
    enum pho_xfer_op     xd_op;      /**< Operation to perform (PUT or GET) */
    bool                 xd_close_fd; /**< True if xd_fd should be closed by
                                        *  phobos.
                                        */
    const char          *xd_fpath;   /**< Path to read or write */
    int                  xd_fd;      /**< positive fd if xd_id_open */
    ssize_t              xd_size;    /**< Amount of data to write (for the GET
                                       * operation, the size read is equal to
                                       * the size of the retrieved object)
                                       */
    const char          *xd_layout_name; /**< Name of the layout module to use
                                           * (for put).
                                           */
    struct pho_attrs     xd_attrs;   /**< User defined attribute to get / put */
    enum pho_xfer_flags  xd_flags;   /**< See enum pho_xfer_flags doc */
    struct tags          xd_tags;    /**< Tags to select a media to write */
    int                  xd_rc;      /**< Outcome of this xfer */
};


/**
 * Put N files to the object store with minimal overhead.
 * Each desc entry contains:
 * - objid: the target object identifier
 * - fpath: the source file
 * - fd: (optional) if >= 0, an opened fd to read from (replaces fpath)
 * - size: (optional) if >= 0, amount of data to read from fd
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
 * - fpath: target file to write the object data to
 * - fd: (optional) an opened fd to write to (replaces fpath)
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

/** query metadata of the object store */
/* TODO int phobos_query(criteria, &obj_list); */
/* TODO int phobos_del(); */

/**
 * Initializes a xfer to originate be read/written from/to a path.  After this
 * call, other fields can be set by the caller (xd_layout_name, xd_attrs,
 * xd_tags, xd_flags).
 */
void pho_xfer_desc_init_from_path(struct pho_xfer_desc *xfer, const char *path);

/**
 * Free resources associated with this xfer, except for xd_fpath and xd_objid
 * which are for the caller to manage.
 *
 * @return 0 on success, -errno on close error.
 */
int pho_xfer_desc_destroy(struct pho_xfer_desc *xfer);

#endif
