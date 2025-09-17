/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2025 CEA/DAM.
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

#define __PHOBOS_MAJOR__ PHOBOS_MAJOR
#define __PHOBOS_MINOR__ PHOBOS_MINOR
#define __PHOBOS_PATCH__ PHOBOS_PATCH

#define __PHOBOS_PREREQ(maj, min) \
    (__PHOBOS_MAJOR__ > (maj) || \
    (__PHOBOS_MAJOR__ == (maj) && __PHOBOS_MINOR__ >= (min)))

#define __PHOBOS_PREREQ_PATCH(maj, min, patch) \
    (__PHOBOS_PREREQ((maj), (min)) || \
    (__PHOBOS_MAJOR__ == (maj) && __PHOBOS_MINOR__ == (min) && \
        __PHOBOS_PATCH__ >= (patch)))

#include "pho_attrs.h"
#include "pho_types.h"
#include "pho_dss.h"
#include "pho_dss_wrapper.h"
#include <stdlib.h>

struct pho_xfer_desc;

/**
 * Transfer (GET / PUT / MPUT) flags.
 * Exact semantic depends on the operation it is applied on.
 */
enum pho_xfer_flags {
    /**
     * put: replace the object if it already exists (_not supported_)
     * get: replace the target file if it already exists
     */
    PHO_XFER_OBJ_REPLACE    = (1 << 0),
    /* get: check the object's location before getting it */
    PHO_XFER_OBJ_BEST_HOST  = (1 << 1),
    /* del: hard remove the object */
    PHO_XFER_OBJ_HARD_DEL   = (1 << 2),
    /* del: hard remove the copy */
    PHO_XFER_COPY_HARD_DEL  = (1 << 3),
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
    PHO_XFER_OP_GETMD, /**< GET metadata operation. */
    PHO_XFER_OP_DEL,   /**< DEL operation. */
    PHO_XFER_OP_UNDEL, /**< UNDEL operation. */
    PHO_XFER_OP_COPY,  /**< COPY operation. */
    PHO_XFER_OP_LAST
};

static const char * const xfer_op_names[] = {
    [PHO_XFER_OP_PUT]   = "PUT",
    [PHO_XFER_OP_GET]   = "GET",
    [PHO_XFER_OP_GETMD] = "GETMD",
    [PHO_XFER_OP_DEL]   = "DELETE",
    [PHO_XFER_OP_UNDEL] = "UNDELETE",
    [PHO_XFER_OP_COPY]  = "COPY",
};

static inline const char *xfer_op2str(enum pho_xfer_op op)
{
    if (op >= PHO_XFER_OP_LAST)
        return NULL;

    return xfer_op_names[op];
}

/**
 * PUT parameters.
 * Family, layout_name and tags can be set directly or by using a profile.
 * A profile is a name defined in the phobos config to combine these parameters.
 * The profile will not override family and layout if they have been specified
 * in this struct but extend existing tags.
 */
struct pho_xfer_put_params {
    enum rsc_family  family;      /**< Targeted resource family. */
    const char      *grouping;    /**< Grouping attached to the new object.
                                    *  For a new copy of an existing object,
                                    *  we can't set a new grouping. Grouping
                                    *  of the pre-existing object is used.
                                    */
    const char      *library;     /**< Targeted library (If NULL, any available
                                    *  library can be selected.)
                                    */
    const char      *layout_name; /**< Name of the layout module to use. */
    struct pho_attrs lyt_params;  /**< Parameters used for the layout */
    struct string_array     tags; /**< Tags to select a media to write. */
    const char      *profile;     /**< Identifier for family, layout,
                                    *  tag combination
                                    */
    const char      *copy_name;   /**< Copy reference. */
    bool             overwrite;   /**< true if the put command could be an
                                    *  update.
                                    */
    bool             no_split;    /**< true if all xfer of the put command
                                    *  should be put on the same medium.
                                    */
};

/**
 * GET parameters.
 * Node_name corresponds to the name of the node the object can be retrieved
 * from, if a phobos_get call fails.
 * Copy_name corresponds to the copy to get.
 */
struct pho_xfer_get_params {
    const char *copy_name;          /**< Copy to retrieve. */
    enum dss_obj_scope scope;       /**< Scope of the object to get
                                      *  (alive, deprecated, ...).
                                      */
    char *node_name;                /**< Node name [out] */
};

/*
 * DEL parameters.
 * Copy name corresponds to the name of the copy to delete. Copies can only be
 * hard deleted.
 */
struct pho_xfer_del_params {
    char *copy_name;           /**< Copy name [out] */
    enum dss_obj_scope scope;  /**< Scope of the object to delete
                                 *  (alive, deprecated, ...).
                                 */
};

/*
 * COPY parameters.
 */
struct pho_xfer_copy_params {
    struct pho_xfer_get_params get; /**< Get parameters to use to copy */
    struct pho_xfer_put_params put; /**< Put parameters to use to copy */
};

/**
 * Operation parameters.
 */
union pho_xfer_params {
    struct pho_xfer_put_params put;     /**< PUT parameters. */
    struct pho_xfer_get_params get;     /**< GET parameters. */
    struct pho_xfer_del_params delete;  /**< DEL parameters. */
    struct pho_xfer_copy_params copy;   /**< COPY parameters. */
};

/**
 * Xfer descriptor.
 * The source/destination semantics of the fields vary
 * depending on the nature of the operation.
 * See below:
 *  - phobos_getmd()
 *  - phobos_get()
 *  - phobos_put()
 *  - phobos_undelete()
 *  - phobos_copy()
 */
struct pho_xfer_desc {
    enum pho_xfer_op        xd_op;       /**< Operation to perform. */
    union pho_xfer_params   xd_params;   /**< Operation parameters. */
    enum pho_xfer_flags     xd_flags;    /**< See enum pho_xfer_flags doc. */
    int                     xd_rc;       /**< Outcome of this xfer. */
    int                     xd_ntargets; /**< Number of objects. */
    struct pho_xfer_target *xd_targets;  /**< Object(s) to transfer. */
};

struct pho_xfer_target {
    char             *xt_objid;   /**< Object ID to read or write. */
    char             *xt_objuuid; /**< Object UUID to read or write. */
    int               xt_version; /**< Object version. */
    int               xt_fd;      /**< FD of the source/destination. */
    struct pho_attrs  xt_attrs;   /**< User defined attributes. */
    ssize_t           xt_size;    /**< Amount of data to write. */
    int               xt_rc;      /**< Outcome for this target's xfer. */
};

/**
 * Initialize the global context of Phobos.
 *
 * This must be called using the following order:
 *   phobos_init -> ... -> phobos_fini
 *
 * @return              0 on success or -errno on failure.
 */
int phobos_init(void);

/**
 * Finalize the global context of Phobos.
 *
 * This must be called using the following order:
 *   phobos_init -> ... -> phobos_fini
 */
void phobos_fini(void);

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
 *
 * @return              0 on success or -errno on failure.
 *
 * This must be called after phobos_init.
 */
int phobos_put(struct pho_xfer_desc *xfers, size_t n,
               pho_completion_cb_t cb, void *udata);

/**
 * Retrieve N files from the object store
 * desc contains:
 * - objid:   identifier of the object to retrieve, this is mandatory.
 *
 * - objuuid: uuid of the object to retrieve
 *            if not NULL, this field is duplicated internally and freed by
 *            pho_xfer_desc_clean(). The caller have to make sure to keep
 *            a copy of this pointer if it needs to be freed.
 *            if NULL and there is an object alive, get the current generation
 *            if NULL and there is no object alive, check the deprecated
 *            objects:
 *                if they all share the same uuid, the object matching
 *                the version criteria is retrieved
 *
 * - version: version of the object to retrieve
 *            if 0, get the most recent object. Otherwise, the object with the
 *            matching version is returned if it exists
 *            if there is an object in the object table and its version does
 *            not match, phobos_get() will target the current generation and
 *            query the deprecated_object table
 *
 * - fd: an opened fd to write to
 * - attrs: unused (can be NULL)
 * - flags: behavior flags
 *
 * If objuuid and version are NULL and 0, phobos_get() will only query the
 * object table. Otherwise, the object table is queried first and then the
 * deprecated_object table.
 *
 * Other fields are not used.
 *
 * Individual completion notifications are issued via xd_callback.
 * This function returns the first encountered error or 0 if all
 * sub-operations have succeeded.
 *
 * @return              0 on success or -errno on failure.
 *
 * This must be called after phobos_init.
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
 * This function returns the first encountered error or 0 if all
 * sub-operations have succeeded.
 *
 * @return              0 on success or -errno on failure.
 *
 * This must be called after phobos_init.
 */
int phobos_getmd(struct pho_xfer_desc *xfers, size_t n,
                 pho_completion_cb_t cb, void *udata);

/** query metadata of the object store */
/* TODO int phobos_query(criteria, &obj_list); */

/**
 * Delete an object from the object store
 *
 * This deletion is not a hard remove, and only deprecates the object.
 *
 * If the flag PHO_XFER_OBJ_HARD_DEL is set, the object, and its past versions,
 * will be removed from the database. The extents will still be present, to
 * keep tracking usage stats of tapes.
 *
 * @param[in]   xfers       Objects to delete, only the oid field is used
 * @param[in]   num_xfers   Number of objects to delete
 *
 * @return                  0 on success, -errno on failure
 *
 * This must be called after phobos_init.
 */
int phobos_delete(struct pho_xfer_desc *xfers, size_t num_xfers);

/**
 * Undelete a deprecated object from the object store
 *
 * The latest version of each deprecated object is moved back.
 *
 * @param[in]   xfers       Objects to undelete, only the uuid field is used
 * @param[in]   num_xfers   Number of objects to undelete
 *
 * @return                  0 on success, -errno on failure
 *
 * This must be called after phobos_init.
 */
int phobos_undelete(struct pho_xfer_desc *xfers, size_t num_xfers);

/**
 * Retrieve one node name from which an object can be accessed.
 *
 * This function returns the most convenient node to get an object.

 * If possible, this function locks to the returned node the minimum adequate
 * number of media storing extents of this object to ensure that the returned
 * node will be able to get this object. The number of newly added locks is also
 * returned to allow the caller to keep up to date the load of each host, by
 * counting the media that are newly locked to the returned hostname.
 *
 * Among the most convenient nodes, this function will favour the \p focus_host.
 *
 * At least one of \p oid or \p uuid must not be NULL.
 *
 * If \p version is not provided (zero as input), the latest one is located.
 *
 * If \p uuid is not provided, we first try to find the corresponding \p oid
 * from living objects into the object table. If there is no living object with
 * \p oid, we check amongst all deprecated objects. If there is only one
 * corresponding \p uuid, in the deprecated objects, we take this one. If there
 * is more than one \p uuid corresponding to this \p oid, we return -EINVAL.
 *
 * @param[in]   oid         OID of the object to locate (ignored if NULL and
 *                          \p uuid must not be NULL)
 * @param[in]   uuid        UUID of the object to locate (ignored if NULL and
 *                          \p oid must not be NULL)
 * @param[in]   version     Version of the object to locate (ignored if zero)
 * @param[in]   focus_host  Hostname on which the caller would like to access
 *                          the object if there is no node more convenient (if
 *                          NULL, focus_host is set to local hostname)
 * @param[in]   copy_name   Copy to locate
 * @param[out]  hostname    Allocated and returned hostname of the most
 *                          convenient node on which the object can be accessed
 *                          (NULL is returned on error)
 * @param[out]  nb_new_lock Number of new lock on media added for the returned
 *                          hostname
 *
 * @return                  0 on success or -errno on failure,
 *                          -ENOENT if no object corresponds to input
 *                          -EINVAL if more than one object corresponds to input
 *                          -EAGAIN if there is not any convenient node to
 *                          currently retrieve this object
 *                          -ENODEV if there is no existing medium to retrieve
 *                          this object
 *                          -EADDRNOTAVAIL if we cannot get self hostname
 *
 * This must be called after phobos_init.
 */
int phobos_locate(const char *obj_id, const char *uuid, int version,
                  const char *focus_host, const char *copy_name,
                  char **hostname, int *nb_new_lock);

/**
 * Rename an object in the object store.
 *
 * If the object to rename is alive, it can be found using either its
 * \p old_oid or its \p uuid.
 * Otherwise, it must be identified using its \p uuid.
 * Thus, this function can only rename one generation of an object at a time.
 *
 * @param[in]   old_oid OID of the object to rename (ignored if NULL and
 *                      \p uuid must not be NULL)
 * @param[in]   uuid    UUID of the object to rename (ignored if NULL and
 *                      \p old_oid must not be NULL)
 * @param[in]   new_oid The new name to give to the object. It must be
 *                      different from any oid from the object table.
 *
 * @return              0 on success or -errno on failure.
 *
 * This must be called after phobos_init.
 */
int phobos_rename(const char *old_oid, const char *uuid, char *new_oid);

/**
 * Copy N objects
 * desc contains:
 * - objid:   identifier of the object to copy, this is mandatory.
 *
 * - objuuid: uuid of the object to retrieve
 *            if not NULL, this field is duplicated internally and freed by
 *            pho_xfer_desc_clean(). The caller have to make sure to keep
 *            a copy of this pointer if it needs to be freed.
 *            if NULL and there is an object alive, get the current generation
 *            if NULL and there is no object alive, check the deprecated
 *            objects:
 *                if they all share the same uuid, the object matching
 *                the version criteria is retrieved
 *
 * - version: version of the object to retrieve
 *            if 0, get the most recent object. Otherwise, the object with the
 *            matching version is returned if it exists
 *            if there is an object in the object table and its version does
 *            not match, phobos_get() will target the current generation and
 *            query the deprecated_object table
 *
 * - layout_name: (optional) name of the layout module to use
 * - size: unused
 * - fd: unused
 * - attrs: unused (can be NULL)
 * - flags: behavior flags
 * - tags: tags defining constraints on which media can be selected to put the
 *   data
 *
 * If objuuid and version are NULL and 0, phobos_get() will only query the
 * object table. Otherwise, the object table is queried first and then the
 * deprecated_object table.
 *
 * Other fields are not used.
 *
 * Individual completion notifications are issued via xd_callback.
 * This function returns the first encountered error or 0 if all
 * sub-operations have succeeded.
 *
 * @return              0 on success or -errno on failure.
 *
 * This must be called after phobos_init.
 */
int phobos_copy(struct pho_xfer_desc *xfers, size_t n,
                pho_completion_cb_t cb, void *udata);

/**
 * Delete a copy's object from the object store
 *
 * @param[in]   xfers       Copy objects to delete
 * @param[in]   num_xfers   Number of objects to delete
 *
 * @return                  0 on success, -errno on failure
 *
 * This must be called after phobos_init.
 */
int phobos_copy_delete(struct pho_xfer_desc *xfers, size_t num_xfers);

/**
 * Clean a pho_xfer_desc structure by freeing the uuid and attributes, and
 * the tags in case the xfer corresponds to a PUT operation.
 *
 * @param[in]   xfer        The xfer structure to clean.
 *
 * This must be called after phobos_init.
 */
void pho_xfer_desc_clean(struct pho_xfer_desc *xfer);

/**
 * Clean a pho_xfer_target structure by freeing the uuid and attributes
 *
 * @param[in]   xfer        The xfer_target structure to clean.
 *
 * This must be called after phobos_init.
 */
void pho_xfer_clean(struct pho_xfer_target *xfer);

struct pho_list_filters {
    const char **res;       /**< Resource to filters (oids) */
    int n_res;              /**< Number of resources */
    const char *uuid;       /**< UUID of the object */
    int version;            /**< Version of the object */
    bool is_pattern;        /**< True if search using POSIX pattern */
    const char **metadata;  /**< Metadata filter */
    int n_metadata;         /**< Number of metadata */
    int status_filter;      /**< Number corresponding to the copy_status
                              *  filter
                              */
    char *copy_name;        /**< Copy's name filter */
};

/**
 * Retrieve the objects that match the given pattern and metadata.
 * If given multiple objids or patterns, retrieve every item with name
 * matching any of those objids or patterns.
 * If given multiple objids or patterns, and metadata, retrieve every item
 * with name matching any of those objids or pattersn, but containing
 * every given metadata.
 *
 * The caller must release the list calling phobos_store_object_list_free().
 *
 * \param[in]       filters         The filters to use
 * \param[in]       scope           List only/also the deprecated objects.
 * \param[out]      objs            Retrieved objects.
 * \param[out]      n_objs          Number of retrieved items.
 *
 * \return                          0     on success,
 *                                 -errno on failure.
 *
 * This must be called after phobos_init.
 */
int phobos_store_object_list(struct pho_list_filters *filters,
                             enum dss_obj_scope scope,
                             struct object_info **objs, int *n_objs,
                             struct dss_sort *sort);

/**
 * Release the list retrieved using phobos_store_object_list().
 *
 * \param[in]       objs            Objects to release.
 * \param[in]       n_objs          Number of objects to release.
 *
 * This must be called after phobos_init.
 */
void phobos_store_object_list_free(struct object_info *objs, int n_objs);

/**
 * Retrieve the copies that match the given oids.
 *
 * The caller must release the list calling phobos_store_copy_list_free().
 *
 * \param[in]       filters         The filters to use.
 * \param[in]       scope           Retrieve only/also in the deprecated
 *                                  objects.
 * \param[out]      copy            Retrieved copies.
 * \param[out]      n_copy          Number of retrieved items.
 * \param[in]       sort            Sort filter.
 *
 * \return                          0     on success,
 *                                 -errno on failure.
 *
 * This must be called after phobos_init.
 */
int phobos_store_copy_list(struct pho_list_filters *filters,
                           enum dss_obj_scope scope,
                           struct copy_info **copy, int *n_copy,
                           struct dss_sort *sort);

/**
 * Release the list retrieved using phobos_store_copy_list().
 *
 * \param[in]       copy            Copies to release.
 * \param[in]       n_copy          Number of copies to release.
 *
 * This must be called after phobos_init.
 */
void phobos_store_copy_list_free(struct copy_info *copy, int n_copy);

#endif
