/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2026 CEA/DAM.
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
 * Input strings of the API entries of this header must be valid UTF-8: the
 * database and the metadata attached to the stored objects only handle
 * UTF-8.
 *
 * The keys and the values of the user metadata (the xt_attrs fields) are
 * silently sanitized when they enter the API: each of their invalid bytes
 * is replaced with the Unicode replacement character U+FFFD. The sanitized
 * form is what gets stored, and, as the sanitization is deterministic and
 * applied by every API entry, passing the same input string again addresses
 * the same stored data.
 *
 * To store raw (non textual) data in a metadata value, encode it first in
 * a UTF-8 compatible representation, e.g. hexadecimal or base64, as the
 * sanitization would otherwise replace its non-UTF-8 bytes with U+FFFD.
 *
 * Any other input string that is not valid UTF-8 cannot be stored nor
 * queried: the operation fails with an explicit error.
 */

/**
 * Xfer behavior flags.
 *
 * The exact meaning of each flag depends on the operation to which it is
 * applied.
 */
enum pho_xfer_flags {
    /* GET: replace the target file if it already exists */
    PHO_XFER_OBJ_REPLACE    = (1 << 0),
    /* GET: check the object's location before getting it */
    PHO_XFER_OBJ_BEST_HOST  = (1 << 1),
    /* DEL: hard remove the object */
    PHO_XFER_OBJ_HARD_DEL   = (1 << 2),
    /* COPY DEL: hard remove the copy */
    PHO_XFER_COPY_HARD_DEL  = (1 << 3),
};

/**
 * Xfer completion callback.
 *
 * This callback is invoked once for each processed Xfer.
 *
 * \param[in] udata  User-provided data.
 * \param[in] xfer   Xfer descriptor associated with the operation.
 * \param[in] rc     Return code for this Xfer operation: 0 on sucess, errno on
 *                   failure.
 */
typedef void (*pho_completion_cb_t)(void *udata,
                                    const struct pho_xfer_desc *xfer, int rc);

/**
 * Phobos Xfer operations.
 */
enum pho_xfer_op {
    PHO_XFER_OP_PUT,     /**< PUT operation. */
    PHO_XFER_OP_GET,     /**< GET operation. */
    PHO_XFER_OP_GETMD,   /**< GET metadata operation. */
    PHO_XFER_OP_SETMD,   /**< SET metedata operation. */
    PHO_XFER_OP_DEL,     /**< DEL operation. */
    PHO_XFER_OP_UNDEL,   /**< UNDEL operation. */
    PHO_XFER_OP_COPY,    /**< COPY operation. */
    PHO_XFER_OP_REBUILD, /**< REBUILD operation. */
    PHO_XFER_OP_LAST
};

static const char * const xfer_op_names[] = {
    [PHO_XFER_OP_PUT]     = "PUT",
    [PHO_XFER_OP_GET]     = "GET",
    [PHO_XFER_OP_GETMD]   = "GETMD",
    [PHO_XFER_OP_SETMD]   = "SETMD",
    [PHO_XFER_OP_DEL]     = "DELETE",
    [PHO_XFER_OP_UNDEL]   = "UNDELETE",
    [PHO_XFER_OP_COPY]    = "COPY",
    [PHO_XFER_OP_REBUILD] = "REBUILD",
};

static inline const char *xfer_op2str(enum pho_xfer_op op)
{
    if (op >= PHO_XFER_OP_LAST)
        return NULL;

    return xfer_op_names[op];
}

/**
 * PUT parameters.
 *
 * Family, layout_name, library and tags can be set directly or by using a
 * profile. A profile is a name defined in the phobos config to combine these
 * parameters. The profile does not override family, library and layout if they
 * are specified, but it extends existing tags.
 *
 * Copy_name can be set directly, otherwise the default copy_name is used.
 * The copy_name can also be associated with a profile.
 *
 * The grouping and overwrite/no_split options must be set directly in order to
 * use them. They cannot be set by a profile.
 */
struct pho_xfer_put_params {
    enum rsc_family  family;      /**< [in] Targeted resource family. */
    const char      *grouping;    /**< [in] Grouping attached to the new object.
                                    *  For a new copy of an existing object,
                                    *  we can't set a new grouping. Grouping
                                    *  of the pre-existing object is used.
                                    */
    const char      *library;     /**< [in] Targeted library (If NULL, any
                                    *  available library can be selected.)
                                    */
    const char      *layout_name; /**< [in] Name of the layout module to use. */
    struct pho_attrs lyt_params;  /**< [in] Parameters used for the layout */
    struct string_array     tags; /**< [in] Tags to select a media to write. */
    const char      *profile;     /**< [in] A configuration profile which
                                    *  defines the family, library, layout and
                                    *  tag combination to use.
                                    */
    const char      *copy_name;   /**< [in] Name of the copy being written. A
                                    *  copy name can also be associated with a
                                    *  profile in the configuration.
                                    */
    bool             overwrite;   /**< [in] true if the put command could be an
                                    *  update.
                                    */
    bool             no_split;    /**< [in] true if all targets inside a xfer of
                                    *  the put command should be written without
                                    *  split (.eg on the same media).
                                    */
};

/**
 * GET parameters.
 *
 * Scope corresponds to where Phobos should search for the object to retrieve.
 * All possible values are DSS_OBJ_ALIVE (search only in the alive objects),
 * DSS_OBJ_DEPRECATED (search only in the deprecated objects) and DSS_OBJ_ALL
 * (search in the alive and deprecated objects).
 */
struct pho_xfer_get_params {
    const char *copy_name;          /**< [in] Preferred copy to retrieve. If
                                      *  NULL, Phobos selects a copy from the
                                      *  preferred_order, then the default copy
                                      *  and finally the copy found.
                                      */
    enum dss_obj_scope scope;       /**< [in] Object visibility scope to query
                                      *  (alive, deprecated, both).
                                      */
    char *node_name;                /**< [out] Hostname determined during a GET
                                      *  with the flag PHO_XFER_OBJ_BEST_HOST.
                                      *  node_name is NULL if the object is on
                                      *  the local node, otherwise set with the
                                      *  hostname where the object can be get.
                                      */
};

/*
 * DEL parameters.
 *
 * These parameters are not used when doing a soft delete on objects.
 *
 * Scope corresponds to where Phobos should search the object to either
 * delete it or delete a copy of this object. All possible values are
 * DSS_OBJ_ALIVE (check only the alive objects), DSS_OBJ_DEPRECATED (check only
 * the deprecated objects) and DSS_OBJ_ALL (check the alive and deprecated
 * objects).
 *
 * Check the description of `phobos_delete`.
 */
struct pho_xfer_del_params {
    const char *copy_name;     /**< [in] Name of the copy to hard delete when
                                 *  the flag PHO_XFER_COPY_HARD_DEL is used.
                                 */
    enum dss_obj_scope scope;  /**< [in] Object visibility scope to delete
                                 *  (alive, deprecated, both).
                                 */
};

/*
 * COPY parameters.
 *
 * COPY combines the PUT and GET parameters because it selects an object and
 * does another copy.
 */
struct pho_xfer_copy_params {
    struct pho_xfer_get_params get; /**< Get parameters to use to copy */
    struct pho_xfer_put_params put; /**< Put parameters to use to copy */
};

/*
 * REBUILD parameters.
 *
 * REBUILD combines the PUT and GET parameters because it selects a copy and
 * rebuild some extents of this copy.
 *
 * If \p n_extents > 0, only the extent listed in \p extents_idx are rebuilt.
 * Otherwise, all missing extents are rebuilt.
 */
struct pho_xfer_rebuild_params {
    struct pho_xfer_get_params get;  /**< Get parameters to use to rebuild */
    struct pho_xfer_put_params put;  /**< Put parameters to use to rebuild */
    const int *extents_idx;          /**< List of extent index to rebuild
                                       *  XXX: works only with one target per
                                       *  xfer
                                       */
    size_t n_extents;
    const struct pho_id *media_read; /**< Media to read from. Only supported
                                       *  with a single extent in extents_idx.
                                       */
};

/**
 * Operation parameters.
 */
union pho_xfer_params {
    struct pho_xfer_put_params put;         /**< PUT parameters. */
    struct pho_xfer_get_params get;         /**< GET parameters. */
    struct pho_xfer_del_params delete;      /**< DEL parameters. */
    struct pho_xfer_copy_params copy;       /**< COPY parameters. */
    struct pho_xfer_rebuild_params rebuild; /**< REBUILD parameters. */
};

/**
 * Xfer descriptor.
 * The source/destination semantics of the fields vary depending on the nature
 * of the operation.
 *
 * See below:
 *  - phobos_copy()
 *  - phobos_delete()
 *  - phobos_get()
 *  - phobos_getmd()
 *  - phobos_put()
 *  - phobos_undelete()
 */
struct pho_xfer_desc {
    enum pho_xfer_op        xd_op;       /**< Operation to perform. */
    union pho_xfer_params   xd_params;   /**< Operation parameters. */
    enum pho_xfer_flags     xd_flags;    /**< Bitmask of pho_xfer_flags values
                                           */
    int                     xd_rc;       /**< Outcome of this xfer,
                                           *  0 on success, -errno on failure
                                           */
    int                     xd_ntargets; /**< Number of objects. */
    struct pho_xfer_target *xd_targets;  /**< Object(s) to transfer. */
};

/**
 * Xfer target.
 *
 * This structure carries all the information for one object.
 *
 * The keys and the values of xt_attrs must be valid UTF-8: invalid bytes
 * are silently replaced with the Unicode replacement character U+FFFD (see
 * the note on input strings at the top of this header).
 */
struct pho_xfer_target {
    const char       *xt_objid;   /**< Object ID to PUT/GET/GETMD/SETMD/DEL/
                                    *  UNDEL/COPY
                                    *  Ownership: Caller
                                    */
    char             *xt_objuuid; /**< Object UUID to GET/GETMD/SETMD/DEL/UNDEL/
                                    *  COPY
                                    *  Phobos duplicates this field internally
                                    *  if not NULL. Therefore, the caller
                                    *  remains responsible for freeing their own
                                    *  original pointer.
                                    *  The internal duplicate will automatically
                                    *  be freed when the caller invokes
                                    *  pho_xfer_clean().
                                    */
    int               xt_version; /**< Object version. */
    int               xt_fd;      /**< FD of the source/destination while doing
                                    *  a GET or PUT. If it's a PUT, xt_fd is
                                    *  the object's source. If it's a GET,
                                    *  xt_fd is where the object is retrieved.
                                    */
    struct pho_attrs  xt_attrs;   /**< User defined metadata.
                                    *  Since this struct is populated using the
                                    *  Phobos API, it must be freed by calling
                                    *  either pho_attrs_free() or
                                    *  pho_xfer_clean().
                                    */
    ssize_t           xt_size;    /**< Amount of data to write during a PUT */
    int               xt_rc;      /**< Outcome for this target's xfer. */
};

/**
 * Initialize the global context of Phobos.
 *
 * This function must be called before any other API entry point from this
 * header.
 *
 * This must be called using the following order:
 *   phobos_init -> ... -> phobos_fini
 *
 * @return              0 on success or -errno on failure.
 */
int phobos_init(void);

void phobos_init_without_dss(void);

/**
 * Finalize the global context of Phobos.
 *
 * This must be called using the following order:
 *   phobos_init -> ... -> phobos_fini
 */
void phobos_fini(void);

/**
 * Put files to the object store with minimal overhead.
 *
 * Each Xfer descriptor must describe a PUT operation and provide one or more
 * targets. Each target must provide:
 *  - xt_objid : the target object identifier
 *  - xt_fd    : an open fd to read from
 *  - xt_size  : amount of data to read from fd
 *  - xt_attrs : user metadata (OPTIONAL)
 * other parameters are not used for a PUT.
 *
 * The xd_params can be provided or they will be overriden with default values.
 *
 * Memory ownership:
 *  - xt_objid: Provided by the caller. Phobos will not free it.
 *  - xt_attrs: Set using pho_attr_set(). Must be freed by calling
 *              pho_xfer_desc_clean()
 * For xd_params.put fields:
 *  - tags and lyt_params: Ownership transferred to Phobos, who will free it.
 *  - if other string pointers are allocated by the caller, Phobos will not free
 *    them.
 *
 * \param[in,out]  xfers  List of Xfer descriptors
 * \param[in]      n      Number of Xfer descriptors
 * \param[in]      cb     Optional completion callback per Xfer
 * \param[in]      udata  User data passed to cb
 *
 * @return                0 on success or -errno on failure.
 *
 * This must be called after phobos_init.
 */
int phobos_put(struct pho_xfer_desc *xfers, size_t n,
               pho_completion_cb_t cb, void *udata);

/**
 * Retrieve N files from the object store
 *
 * Each Xfer descriptor must describe a GET operation and provide ONLY one
 * target. A target must provide:
 *  - xt_objid   : the target object identifier
 *
 *  - xt_objuuid : uuid of the object to retrieve (OPTIONAL)
 *                 if NULL and there is an object alive, get the current
 *                 generation
 *                 if NULL and there is no object alive, check the deprecated
 *                 objects:
 *                      if they all share the same uuid, the object matching
 *                      the version criteria or the most recent one is retrieved
 *
 *  - xt_version : version of the object to retrieve (OPTIONAL)
 *                 if 0, get the most recent object. Otherwise, the object with
 *                 the matching version is returned if it exists if there is an
 *                 object in the object table and its version does not match,
 *                 phobos_get() will target the current generation and
 *                 query the deprecated_object table
 *
 *  - xt_fd      : an opened fd to write to
 *
 * The scope inside the GET xd_params must be set. See the pho_xfer_get_params.
 *
 * xd_flags can be set to PHO_XFER_OBJ_BEST_HOST to check the object's location
 * before getting it. If the object is not on the current node, node_name in
 * the get parameters will be set with the object's location.
 *
 * If objuuid and version are NULL and 0, phobos_get() will only query the
 * object table. Otherwise, the object table is queried first and then the
 * deprecated_object table.
 *
 * Memory ownership:
 *  - xt_objid: Provided by the caller. pho_xfer_desc_clean() will not free it.
 *  - xt_objuuid: if not NULL, this field is duplicated internally and freed by
 *                pho_xfer_desc_clean(). The caller has to make sure to keep a
 *                copy of this pointer if it needs to be freed.
 *  - xt_attrs: [out] Filled by phobos_get(). Freed by calling
 *              pho_xfer_desc_clean().
 * For xd_params.get fields:
 *  - copy_name: Provided by the caller. Ownership retained by the caller
 *
 * \param[in,out]  xfers  List of Xfer descriptors
 * \param[in]      n      Number of Xfer descriptors
 * \param[in]      cb     Optional completion callback per Xfer
 * \param[in]      udata  User data passed to cb
 *
 * @return                0 on success or -errno on failure.
 *
 * This must be called after phobos_init.
 */
int phobos_get(struct pho_xfer_desc *xfers, size_t n,
               pho_completion_cb_t cb, void *udata);

/**
 * Retrieve N file metadata from the object store
 *
 * Each Xfer descriptor must provide ONLY one target. A target must provide:
 *  - xt_objid   : the target object identifier (OPTIONAL if xt_objuuid is
 *                 provided)
 *  - xt_objuuid : uuid of the object to retrieve (OPTIONAL if xt_objid is
 *                 provided)
 *  - xt_version : version of the object to retrieve (OPTIONAL)
 *                 if 0, get the most recent object. Otherwise, the object with
 *                 the matching version is returned if it exists
 *
 * xt_attrs is filled with the retrieved metadata.
 *
 * Memory ownership:
 *  - xt_objid: Provided by the caller. pho_xfer_desc_clean() will not free it.
 *  - xt_objuuid: if not NULL, this field is duplicated internally and freed by
 *                pho_xfer_desc_clean(). The caller has to make sure to keep a
 *                copy of this pointer if it needs to be freed.
 *  - xt_attrs: [out] Filled by phobos_getmd(). Freed by calling
 *              pho_xfer_desc_clean().
 *
 * \param[in,out]  xfers  List of Xfer descriptors
 * \param[in]      n      Number of Xfer descriptors
 *
 * @return                0 on success or -errno on failure.
 *
 * This must be called after phobos_init.
 */
int phobos_getmd(struct pho_xfer_desc *xfers, size_t n);

/**
 * Set (replace) the metadata from the object store
 *
 * This will only update the user_md in the DSS. No new object version and no
 * media are created or modified.
 *
 * @param[in]   xfers       Objects whose metedata will be updated
 * @param[in]   num_xfers   Number of objects to update
 *
 * The following fields are used inside the xfer descriptor:
 * - xt_objid: ID of the object to update cannot be NULL
 * - xt_objuuid: UUID of the object to update
 * - xt_version: object version targeted
 * - xt_attrs: the new metadata to write
 *
 * Other fields are not used.
 *
 * Memory ownership:
 *  - xt_objid: Provided by the caller. pho_xfer_desc_clean() will not free it.
 *  - xt_objuuid: if not NULL, this field is duplicated internally and freed by
 *                pho_xfer_desc_clean(). The caller has to make sure to keep a
 *                copy of this pointer if it needs to be freed.
 *  - xt_attrs: Set using pho_attr_set(). Must be freed by calling
 *              pho_xfer_desc_clean()
 *
 * @return              0 on success or -errno on failure.
 *
 * This must be called after phobos_init.
 */
int phobos_setmd(struct pho_xfer_desc *xfers, size_t num_xfers);

/** query metadata of the object store */
/* TODO int phobos_query(criteria, &obj_list); */

/**
 * Delete an object or a copy's object from the object store
 *
 * If no flag is set, it will delete an object. This deletion is not a hard
 * remove, and only deprecates the object. It can be done even if an object has
 * several copies. As the object will be deprecated, all copies of this object
 * will also be considered deprecated.
 *
 * If the flag PHO_XFER_OBJ_HARD_DEL is set, the object, and its past versions,
 * will be removed from the database. With the tapes, the extents will still be
 * present on the tapes and the extents in the DSS are marked as orphan. It's to
 * keep tracking usage stats of tapes. With the directories, the extents on
 * the directory and in the DSS are removed.
 * It can only be done if an object has only one copy left. Multiple
 * phobos_delete with the flag PHO_XFER_COPY_HARD_DEL must be done to delete all
 * the copies of an object.
 *
 * If the flag PHO_XFER_COPY_HARD_DEL is set, the copy's object will be removed
 * from the database. With the tapes, the extents will still be present on the
 * tapes and the extents in the DSS are marked as orphan. It's to keep tracking
 * usage stats of tapes. With the directories, the extents on the directory and
 * in the DSS are removed.
 * The copy name set in the delete pho_xfer_params is used to select which copy
 * to delete. It can't be done if its the last copy of an object. The deletion
 * of the last copy of an object is done by doing a phobos_delete with no flag
 * or the flag PHO_XFER_OBJ_HARD_DEL.
 *
 * Each Xfer must describe a DEL operation and provide ONLY one target. A target
 * must provide:
 *  - xt_objid   : the target object identifier
 *  - xt_objuuid : uuid of the object, not used with a soft delete. (OPTIONAL)
 *  - xt_version : version of the object, not used with a soft delete
 *                 (OPTIONAL)
 *
 * The scope inside the DEL xd_params MUST be set. See the pho_xfer_del_params.
 *
 * If the scope is alive, Phobos will search the object in the alive table with
 * the oid, uuid and version specified.
 *
 * If the scope is deprecated, Phobos will search the object in the deprecated
 * table with the oid, uuid and version specified. If there are many objects
 * available with the provided filter it will fail. The uuid or version must be
 * added to resolve this conflict.
 *
 * If the scope is all, Phobos will search the object in the alive table first
 * and then in the deprecated table. Phobos will apply the rules described
 * above.
 *
 * Memory ownership:
 *  - xt_objid: Provided by the caller. pho_xfer_desc_clean() will not free it.
 *  - xt_objuuid: if not NULL, this field is duplicated internally and freed by
 *                pho_xfer_desc_clean(). The caller has to make sure to keep a
 *                copy of this pointer if it needs to be freed.
 *  - xt_attrs: [out] Set by phobos_delete only on hard delete. Must be freed by
 *              calling pho_xfer_desc_clean()
 * For xd_params.delete fields:
 *  - copy_name: Provided by the caller. pho_xfer_desc_clean() will not free it.
 *
 * @param[in]   xfers       Objects or copies to delete
 * @param[in]   num_xfers   Number of objects or copies to delete
 *
 * @return                  0 on success, -errno on failure
 *
 * This must be called after phobos_init.
 */
int phobos_delete(struct pho_xfer_desc *xfers, size_t num_xfers);

/**
 * Hard delete the incomplete copies located on the current node
 *
 * If the deleted incomplete copy was the last of an object, this object is hard
 * deleted.
 * All incomplete copies that are located on an other host are skipped.
 * Copies younger than "current_time - delete_incomplete_delay_second" are
 * skipped. "delete_incomplete_delay_second" is a config file parameter.
 *
 * @param[in]   dry_run         If true, only logs actions instead of deleting
 *                              anything.
 * @param[in]   delay_second    Delay in second before an incomplete copy could
 *                              be cleaned. If NULL, the config
                                "delete_incomplete_delay_second" parameter value
                                is used.
 *
 * @return                  0 on success, -errno on failure
 */
int phobos_delete_incomplete_copy(bool dry_run, const int * const delay_second);

/**
 * Undelete a deprecated object from the object store
 *
 * The latest version of each deprecated object is moved back.
 *
 * Each Xfer descriptor must provide ONLY one target. A target must provide:
 *  - xt_objid   : the target object identifier
 *  - xt_objuuid : uuid of the object to undelete (OPTIONAL)
 *
 * Memory ownership:
 *  - xt_objid: Provided by the caller. pho_xfer_desc_clean() will not free it.
 *  - xt_objuuid: if not NULL, this field is duplicated internally and freed by
 *                pho_xfer_desc_clean(). The caller has to make sure to keep a
 *                copy of this pointer if it needs to be freed.
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
int phobos_rename(const char *old_oid, const char *uuid, const char *new_oid);

/**
 * Copy N objects
 *
 * Each Xfer descriptor must describe a COPY operation and provide ONLY one
 * target. A target must provide:
 *   - xt_objid   : identifier of the object to copy (MANDATORY)
 *   - xt_objuuid : uuid of the object to copy
 *                  if not NULL, this field is duplicated internally and freed
 *                  by pho_xfer_desc_clean(). The caller have to make sure to
 *                  keep a copy of this pointer if it needs to be freed.
 *  - xt_version  : version of the object to copy
 *  - Other target attributes are unused.
 *
 * The PUT parameters inside the `pho_xfer_copy_params` can be specified.
 * ONLY the grouping cannot be specify because it is inherited from the
 * existing object. See the `pho_xfer_put_params`.
 *
 * The copy_name in the GET params can also be set to select which source copy
 * to duplicate.
 *
 * The scope in the GET params inside the `pho_xfer_copy_params` MUST be set.
 * See the `pho_xfer_get_params`.
 *
 * If the scope is alive, Phobos will search the object in the alive table with
 * the oid, uuid and version specified.
 *
 * If the scope is deprecated, Phobos will search the object in the deprecated
 * table with the oid, uuid and version specified. If there are many objects
 * available with the provided filter it will fail. The uuid or version must be
 * also specified.
 *
 * If the scope is all, Phobos will search the object in the alive table first
 * and then in the deprecated table. Phobos will apply the rules described
 * above.
 *
 * Memory ownership:
 *  - xt_objid: Provided by the caller. pho_xfer_desc_clean() will not free it.
 *  - xt_objuuid: if not NULL, this field is duplicated internally and freed by
 *                pho_xfer_desc_clean(). The caller has to make sure to keep a
 *                copy of this pointer if it needs to be freed.
 *  - xt_attrs: Set using pho_attr_set(). Freed by Phobos calling
 *              pho_xfer_desc_clean().
 * For the xd_params.copy fields:
 *  - copy.put.tags & copy.put.lyt_params: Freed by calling pho_xfer_desc_clean.
 *  - get.copy_name / put.copy_name / other strings: Allocated and freed by
 *                                            caller. pho_xfer_desc_clean() will
 *                                            not free them.
 *
 * \param[in,out]  xfers  List of Xfer descriptors
 * \param[in]      n      Number of Xfer descriptors
 * \param[in]      cb     Optional completion callback per Xfer
 * \param[in]      udata  User data passed to cb

 * @return              0 on success or -errno on failure.
 *
 * This must be called after phobos_init.
 */
int phobos_copy(struct pho_xfer_desc *xfers, size_t n,
                pho_completion_cb_t cb, void *udata);

/**
 * Rebuild N copies
 *
 * Each Xfer descriptor must describe a REBUILD operation and provide ONLY one
 * target. A target must provide:
 *   - xt_objid   : identifier of the object to rebuild (MANDATORY)
 *   - xt_objuuid : uuid of the object to rebuild
 *                  if not NULL, this field is duplicated internally and freed
 *                  by pho_xfer_desc_clean(). The caller have to make sure to
 *                  keep a copy of this pointer if it needs to be freed.
 *  - xt_version  : version of the object to rebuild
 *  - Other target attributes are unused.
 *
 * The copy_name in the GET and PUT params must also be set to select which
 * copy to rebuild.
 *
 * The scope in the GET params inside the `pho_xfer_copy_params` MUST be set.
 * See the `pho_xfer_get_params`.
 *
 * If the scope is alive, Phobos will search the object in the alive table with
 * the oid, uuid and version specified.
 *
 * If the scope is deprecated, Phobos will search the object in the deprecated
 * table with the oid, uuid and version specified. If there are many objects
 * available with the provided filter it will fail. The uuid or version must be
 * also specified.
 *
 * If the scope is all, Phobos will search the object in the alive table first
 * and then in the deprecated table. Phobos will apply the rules described
 * above.
 *
 * \param[in,out]  xfers  List of Xfer descriptors
 * \param[in]      n      Number of Xfer descriptors

 * @return              0 on success or -errno on failure.
 *
 * This must be called after phobos_init.
 */
int phobos_copy_rebuild(struct pho_xfer_desc *xfers, size_t num_xfers);

/**
 * Clean a pho_xfer_desc structure by freeing the uuid and attributes. For each
 * pho_xfer_target struct it will call the pho_xfer_clean() function.
 * In case the xfer corresponds to a PUT operation the layout and the tags field
 * are freed from the pho_xfer_put_params structure.
 * In case the xfer corresponds to a COPY operation the layout, the tags and the
 * grouping field are freed from the pho_xfer_put_params structure
 *
 * @param[in]   xfer        The xfer structure to clean.
 *
 * This must be called after phobos_init.
 */
void pho_xfer_desc_clean(struct pho_xfer_desc *xfer);

/**
 * Clean a pho_xfer_target structure by freeing the following fields:
 * the object uuid and the attributes.
 * No other field is freed.
 *
 * @param[in]   xfer        The xfer_target structure to clean.
 *
 * This must be called after phobos_init.
 */
void pho_xfer_clean(struct pho_xfer_target *xfer);

struct pho_list_filters {
    const char * const *res;      /**< Resource to filters (oids) */
    int n_res;                    /**< Number of resources */
    const char * const uuid;      /**< UUID of the object */
    int version;                  /**< Version of the object */
    bool is_pattern;              /**< True if search using POSIX pattern */
    const char * const *metadata; /**< Metadata filter */
    int n_metadata;               /**< Number of metadata */
    int status_filter;            /**< Number corresponding to the copy_status
                                    *  filter
                                    */
    const char * const copy_name; /**< Copy's name filter */
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
 * \param[in]       filters         The filters to use.
 * \param[in]       scope           List only/also the deprecated objects.
 * \param[out]      objs            Retrieved objects.
 * \param[out]      n_objs          Number of retrieved items.
 * \param[in]       sort            The sort filters to use.
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
                           struct copy_info **copy, int *n_copy);

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
