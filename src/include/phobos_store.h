/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 * Copyright 2014-2015 CEA/DAM. All Rights Reserved.
 */
/**
 * \brief  Phobos Object Store interface
 */
#ifndef _PHO_STORE_H
#define _PHO_STORE_H

#include "pho_attrs.h"
#include <stdlib.h>


struct pho_xfer_desc;


/**
 * Transfer (GET / PUT / MPUT) flags.
 * Only OBJ_REPLCE for now, valid for all operations.
 */
enum pho_xfer_flags {
    /* put: replace the object if it already exists
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
};


#ifndef SWIG
/**
 * Put a file to the object store
 * desc contains:
 *  objid: The target object identifier
 *  fpath: The source file
 *  attrs: The metadata (optional)
 *  flags: Behavior flags
 */
int phobos_put(const struct pho_xfer_desc *desc, pho_completion_cb_t cb,
               void *udata);
#endif

/**
 * Put N files to the object store with minimal overhead.
 * See phobos_put() for how to fill the desc items.
 * This function returns the first encountered error, or 0 if all sub-operations
 * have succeeded. Individual notifications are issued via xd_callback.
 */
int phobos_mput(const struct pho_xfer_desc *desc, size_t n,
                pho_completion_cb_t cb, void *udata);

/**
 * Retrieve a file from the object store
 * desc contains:
 *  objid: Unique arbitrary string to identify the object
 *  fpath: Target file to write the object data to
 *  attrs: Unused (can be NULL)
 *  flags: Behavior flags
 */
int phobos_get(const struct pho_xfer_desc *desc, pho_completion_cb_t cb,
               void *udata);

/** query metadata of the object store */
/* TODO int phobos_query(criteria, &obj_list); */
/* TODO int phobos_del(); */

#endif
