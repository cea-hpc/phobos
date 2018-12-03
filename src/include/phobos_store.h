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

/**
 * GET / PUT parameter.
 * The source/destination semantics of the fields vary
 * depending on the nature of the operation.
 * See below:
 *  - phobos_get()
 *  - phobos_put()
 */
struct pho_xfer_desc {
    char                *xd_objid;
    char                *xd_fpath;
    struct pho_attrs    *xd_attrs;
    enum pho_xfer_flags  xd_flags;
    struct tags          xd_tags;    /**< Tags to select a media to write */
};


/**
 * Put N files to the object store with minimal overhead.
 * Each desc entry contains:
 * - objid: The target object identifier
 * - fpath: The source file
 * - attrs: The metadata (optional)
 * - flags: Behavior flags
 *
 * Individual completion notifications are issued via xd_callback.
 * This function returns the first encountered error or 0 if all
 * sub-operations have succeeded.
 */
int phobos_put(const struct pho_xfer_desc *desc, size_t n,
               pho_completion_cb_t cb, void *udata);

/**
 * Retrieve N files from the object store
 * desc contains:
 *  objid: Unique arbitrary string to identify the object
 *  fpath: Target file to write the object data to
 *  attrs: Unused (can be NULL)
 *  flags: Behavior flags
 *
 * Individual completion notifications are issued via xd_callback.
 * This function returns the first encountered error or 0 if all
 * sub-operations have succeeded.
 *
 * XXX This function does not support n > 1 yet.
 */
int phobos_get(const struct pho_xfer_desc *desc, size_t n,
               pho_completion_cb_t cb, void *udata);

/** query metadata of the object store */
/* TODO int phobos_query(criteria, &obj_list); */
/* TODO int phobos_del(); */

#endif
