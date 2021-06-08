/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2021 CEA/DAM.
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
#ifndef _PHO_DSS_H
#define _PHO_DSS_H

#include "pho_types.h"
#include "pho_common.h"

#include <stdint.h>
#include <stdlib.h>

/* Maximum lock_owner size, related to the database schema */
#define PHO_DSS_MAX_LOCK_OWNER_LEN 256

/* Maximum lock id size, related to the database schema */
#define PHO_DSS_MAX_LOCK_ID_LEN 2048

/** item types */
enum dss_type {
    DSS_INVAL  = -1,
    DSS_OBJECT =  0,
    DSS_DEPREC,
    DSS_LAYOUT,
    DSS_DEVICE,
    DSS_MEDIA,
    DSS_LAST,
};

static const char * const dss_type_names[] = {
    [DSS_OBJECT] = "object",
    [DSS_DEPREC] = "deprec",
    [DSS_LAYOUT] = "layout",
    [DSS_DEVICE] = "device",
    [DSS_MEDIA]  = "media",
};

/**
 * get dss_type enum from string
 * @param[in]  str  dss_type string representation.
 * @return dss_type as enum.
*/
static inline enum dss_type str2dss_type(const char *str)
{
    int i;

    for (i = 0; i < DSS_LAST; i++)
        if (!strcmp(str, dss_type_names[i]))
            return i;

    return DSS_INVAL;
}

static inline const char *dss_type2str(enum dss_type type)
{
    if (type < 0 || type >= DSS_LAST)
        return NULL;

    return dss_type_names[type];
}

/** dss_set action type */
enum dss_set_action {
    DSS_SET_INVAL  = -1,
    DSS_SET_INSERT =  0,
    DSS_SET_FULL_INSERT, /** A full insert is an insert which overrides
                           *  the default values when adding a row. In the case
                           *  of an object, it means adding a preexisting object
                           *  with a particular uuid and version, not the
                           *  default DSS values.
                           */
    DSS_SET_UPDATE,
    DSS_SET_DELETE,
    DSS_SET_LAST,
};

static const char * const dss_set_actions_names[] = {
    [DSS_SET_INSERT]      = "insert",
    [DSS_SET_FULL_INSERT] = "full-insert",
    [DSS_SET_UPDATE]      = "update",
    [DSS_SET_DELETE]      = "delete",
};

/**
 * get dss_type enum from string for reg. test
 * @param[in]  str  dss_set_action string representation.
 * @return dss_set_action as enum.
 */
static inline enum dss_set_action str2dss_set_action(const char *str)
{
    int i;

    for (i = 0; i < DSS_SET_LAST; i++)
        if (!strcmp(str, dss_set_actions_names[i]))
            return i;

    return DSS_SET_INVAL;
}

struct dss_field_def {
    const char    *df_public;
    const char    *df_implem;
};

static struct dss_field_def dss_fields_names[] = {
    /* Object related fields */
    {"DSS::OBJ::oid", "oid"},
    {"DSS::OBJ::uuid", "uuid"},
    {"DSS::OBJ::version", "version"},
    {"DSS::OBJ::user_md", "user_md"},
    {"DSS::OBJ::deprec_time", "deprec_time"},
    /* Extent related fields */
    {"DSS::EXT::oid", "oid"},
    {"DSS::EXT::uuid", "uuid"},
    {"DSS::EXT::version", "version"},
    {"DSS::EXT::state", "state"},
    {"DSS::EXT::layout_info", "lyt_info"},
    {"DSS::EXT::layout_type", "lyt_info->>'name'"},
    {"DSS::EXT::info", "info"},
    {"DSS::EXT::media_idx", "extents_mda_idx(extent.extents)"},
    /* Media related fields */
    {"DSS::MDA::family", "family"},
    {"DSS::MDA::model", "model"},
    {"DSS::MDA::id", "id"},
    {"DSS::MDA::adm_status", "adm_status"},
    {"DSS::MDA::fs_status", "fs_status"},
    {"DSS::MDA::fs_type", "fs_type"},
    {"DSS::MDA::address_type", "address_type"},
    {"DSS::MDA::tags", "tags"},
    {"DSS::MDA::stats", "stats"},
    {"DSS::MDA::nb_obj", "stats::json->>'nb_obj'"},
    {"DSS::MDA::vol_used", "(stats->>'phys_spc_used')::bigint"},
    {"DSS::MDA::vol_free", "(stats->>'phys_spc_free')::bigint"},
    {"DSS::MDA::lock", "lock"},
    {"DSS::MDA::put", "put"},
    {"DSS::MDA::get", "get"},
    {"DSS::MDA::delete", "delete"},
    /* Device related fields */
    {"DSS::DEV::family", "family"},
    {"DSS::DEV::serial", "id"},
    {"DSS::DEV::host", "host"},
    {"DSS::DEV::adm_status", "adm_status"},
    {"DSS::DEV::model", "model"},
    {"DSS::DEV::path", "path"},
    {"DSS::DEV::lock", "lock"},
    {NULL, NULL}
};

static inline const char *dss_fields_pub2implem(const char *public_name)
{
    int i;

    for (i = 0; dss_fields_names[i].df_public != NULL; i++) {
        const struct dss_field_def  *df = &dss_fields_names[i];

        if (strcmp(df->df_public, public_name) == 0)
            return df->df_implem;
    }

    return NULL;
}

struct dss_filter {
    json_t  *df_json;
};

/**
 * Generate a dss filter from a text (JSON) query.
 * @param[out] filter  Destination filter to use in dss_*_get().
 * @param[in]  fmt     JSON format string representing the query.
 * @return 0 on success; negated errno code on failure.
 */
int dss_filter_build(struct dss_filter *filter, const char *fmt, ...)
                     __attribute__((format(printf, 2, 3)));

/**
 * Release resources associated to a dss filter built using dss_filter_build().
 * @param[in,out] filter  object to free.
 */
void dss_filter_free(struct dss_filter *filter);


/* Exposed externally for python bindings generation */
struct dss_handle {
    void  *dh_conn;
};

/**
 *  Initialize a connection handle
 *  @param[out] handle      Connection handle
 *  @return 0 on success, negated errno code on failure.
 */
int dss_init(struct dss_handle *handle);

/**
 *  Closes a connection
 *  @param[in,out]  handle  Connection handle
 */
void dss_fini(struct dss_handle *handle);

/**
 * Retrieve devices information from DSS
 * @param[in]  hdl      valid connection handle
 * @param[in]  filter   assembled DSS filtering criteria
 * @param[out] dev_ls   list of retrieved items to be freed w/ dss_res_free()
 * @param[out] dev_cnt  number of items retrieved in the list
 *
 * @return 0 on success, negated errno on failure
 */
int dss_device_get(struct dss_handle *hdl, const struct dss_filter *filter,
                   struct dev_info **dev_ls, int *dev_cnt);

/**
 * Retrieve media information from DSS
 * @param[in]  hdl      valid connection handle
 * @param[in]  filter   assembled DSS filtering criteria
 * @param[out] med_ls   list of retrieved items to be freed w/ dss_res_free()
 * @param[out] med_cnt  number of items retrieved in the list
 *
 * @return 0 on success, negated errno on failure
 */
int dss_media_get(struct dss_handle *hdl, const struct dss_filter *filter,
                  struct media_info **med_ls, int *med_cnt);

/**
 * Retrieve layout information from DSS
 * @param[in]  hdl      valid connection handle
 * @param[in]  filter   assembled DSS filtering criteria
 * @param[out] lyt_ls   list of retrieved items to be freed w/ dss_res_free()
 * @param[out] lyt_cnt  number of items retrieved in the list
 *
 * @return 0 on success, negated errno on failure
 */
int dss_layout_get(struct dss_handle *hdl, const struct dss_filter *filter,
                   struct layout_info **lyt_ls, int *lyt_cnt);

/**
 * Retrieve object information from DSS
 * @param[in]  hdl      valid connection handle
 * @param[in]  filter   assembled DSS filtering criteria
 * @param[out] obj_ls   list of retrieved items to be freed w/ dss_res_free()
 * @param[out] obj_cnt  number of items retrieved in the list
 *
 * @return 0 on success, negated errno on failure
 */
int dss_object_get(struct dss_handle *hdl, const struct dss_filter *filter,
                   struct object_info **obj_ls, int *obj_cnt);

/**
 * Retrieve deprecated object information from DSS
 * @param[in]   hdl     valid connection handle
 * @param[in]   filter  assembled DSS filtering criteria
 * @param[out]  obj_ls  list of retrieved items to be freed w/ dss_res_free()
 * @param[out]  obj_cnt number of items retrieved in the list
 *
 * @return 0 on success, negated errno on failure
 */
int dss_deprecated_object_get(struct dss_handle *hdl,
                              const struct dss_filter *filter,
                              struct object_info **obj_ls, int *obj_cnt);

/**
 *  Generic function: frees item_list that was allocated in dss_xxx_get()
 *  @param[in]  item_list   list of items to free
 *  @param[in]  item_cnt    number of items in item_list
 */
void dss_res_free(void *item_list, int item_cnt);

/**
 * Store information for one or many devices in DSS.
 * @param[in]  hdl      valid connection handle
 * @param[in]  dev_ls   array of entries to store
 * @param[in]  dev_cnt  number of items in the list
 * @param[in]  action   operation code (insert, update, delete)
 *
 * @return 0 on success, negated errno on failure
 */
int dss_device_set(struct dss_handle *hdl, struct dev_info *dev_ls, int dev_cnt,
                   enum dss_set_action action);

/**
 * Store information for one or many media in DSS.
 * @param[in]  hdl      valid connection handle
 * @param[in]  med_ls   array of entries to store
 * @param[in]  med_cnt  number of items in the list
 * @param[in]  action   operation code (insert, update, delete)
 *
 * @return 0 on success, negated errno on failure
 */
int dss_media_set(struct dss_handle *hdl, struct media_info *med_ls,
                  int med_cnt, enum dss_set_action action);

/**
 * Store information for one or many layouts in DSS.
 * @param[in]  hdl      valid connection handle
 * @param[in]  lyt_ls   array of entries to store
 * @param[in]  lyt_cnt  number of items in the list
 * @param[in]  action   operation code (insert, update, delete)
 *
 * @return 0 on success, negated errno on failure
 */
int dss_layout_set(struct dss_handle *hdl, struct layout_info *lyt_ls,
                   int lyt_cnt, enum dss_set_action action);

/**
 * Store information for one or many objects in DSS.
 * @param[in]  hdl      valid connection handle
 * @param[in]  obj_ls   array of entries to store
 * @param[in]  obj_cnt  number of items in the list
 * @param[in]  action   operation code (insert, update, delete)
 *
 * @return 0 on success, negated errno on failure
 */
int dss_object_set(struct dss_handle *hdl, struct object_info *obj_ls,
                   int obj_cnt, enum dss_set_action action);

/**
 * Store information for one or many deprecated objects in DSS.
 * @param[in]   hdl     valid connection handle
 * @param[in]   obj_ls  array of entries to store
 * @param[in]   obj_cnt number of items in the list
 * @param[in]   action  operation code (insert, update, delete)
 *
 * @return 0 on success, negated errno on failure
 */
int dss_deprecated_object_set(struct dss_handle *hdl,
                              struct object_info *obj_ls,
                              int obj_cnt, enum dss_set_action action);

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
 * Locate a medium
 *
 * If the medium is locked by a server, its hostname is copied from the lock
 * and returned.
 *
 * If the medium is not locked by anyone, NULL is returned as hostname.
 *
 * @param[in]   dss         DSS to request
 * @param[in]   medium_id   Medium to locate
 * @param[out]  hostname    Allocated and returned hostname or NULL if the
 *                          medium is not locked by anyone
 *
 * @return 0 if success, -errno if an error occurs and hostname is irrelevant
 *         -ENOENT if no medium with medium_id exists in media table
 *         -EACCES if medium is admin locked
 *         -EPERM if medium get operation flag is set to false
 */
int dss_medium_locate(struct dss_handle *dss, const struct pho_id *medium_id,
                      char **hostname);

/* ****************************************************************************/
/* Generic move ***************************************************************/
/* ****************************************************************************/

/**
 * Move an object between two tables
 *
 * @param[in] handle    DSS handle
 * @param[in] type_from Table from which the object should be deleted
 * @param[in] type_to   Table into which the object should be inserted
 * @param[in] obj_list  Objects to move (only primary keys fields of the start
 *                      table must be filled : oid for object table, uuid and
 *                      version for deprecated_object table)
 * @param[in] obj_count Number of objects
 *
 * @return              0 if success, negated errno code on failure
 */
int dss_object_move(struct dss_handle *handle, enum dss_type type_from,
                    enum dss_type type_to, struct object_info *obj_list,
                    int obj_cnt);

/* ****************************************************************************/
/* Generic lock ***************************************************************/
/* ****************************************************************************/

/**
 * Generate a lock_owner "hostname:tid:time:locknumber"
 *
 * Where:
 * - hostname:      local hostname without domain, limited to 213 characters
 * - tid:           thread id limited to 8 characters
 * - time:          number of seconds since epoch limited to 16 characters
 * - lock_number:   unsigned int counter dedicated to each thread, incremented
 *                  at each new lock owner creation, limited to 16 characters
 *
 * Ensure that we don't build an identifier bigger than 256 characters.
 *
 * For the lock owner name to generate a collision, either the tid or the
 * sched_lock_number has to loop in less than 1 second.
 *
 * @param[out] lock_owner   Newly allocated and generated lock owner if success,
 *                          must be freed by the caller, NULL if an error occurs
 *
 * @return                  0 if success,
 *                          -ENOMEM if there is not enough memory to generate
 *                          the lock owner
 *                          -EADDRNOTAVAIL if hostname can't be found to
 *                          generate the lock owner
 */
int dss_init_lock_owner(char **lock_owner);

/**
 * Take locks.
 *
 * If any lock cannot be taken, then the ones that already are will be
 * forcefully unlocked, and the function will not try to lock any other
 * ressource (all-or-nothing policy).
 *
 * @param[in]   handle          DSS handle.
 * @param[in]   type            Type of the ressources to lock.
 * @param[in]   item_list       List of ressources to lock.
 * @param[in]   item_cnt        Number of ressources to lock.
 * @param[in]   lock_owner      Name of the lock owner.
 * @return                      0 on success,
 *                             -EEXIST if \a lock_id already exists.
 */
int dss_lock(struct dss_handle *handle, enum dss_type type,
             const void *item_list, int item_cnt, const char *lock_owner);

/**
 * Refresh lock timestamps.
 *
 * The function will attempt to refresh as many locks as possible. Should any
 * refresh fail, the first error code obtained will be returned after
 * attempting to refresh all other locks (as-much-as-possible policy).
 *
 * @param[in]   handle          DSS handle.
 * @param[in]   type            Type of the ressources's lock to refresh.
 * @param[in]   item_list       List of ressources's lock to refresh.
 * @param[in]   item_cnt        Number of ressources's lock to refresh.
 * @param[in]   lock_owner      Name of the lock owner, must be specified.
 * @return                      0 on success,
 *                             -ENOLCK if the lock does not exist,
 *                             -EACCES if the lock owner does not match.
 */
int dss_lock_refresh(struct dss_handle *handle, enum dss_type type,
                     const void *item_list, int item_cnt,
                     const char *lock_owner);

/**
 * Release locks.
 *
 * If \a lock_owner is NULL, remove the locks without considering the
 * previous owner.
 *
 * The function will attempt to unlock as many locks as possible. Should any
 * unlock fail, the first error code obtained will be returned after
 * attempting to unlock all other locks (as-much-as-possible policy).
 *
 * @param[in]   handle          DSS handle.
 * @param[in]   type            Type of the ressources to unlock.
 * @param[in]   item_list       List of ressources to unlock.
 * @param[in]   item_cnt        Number of ressources to unlock.
 * @param[in]   lock_owner      Name of the lock owner, ignored if NULL.
 * @return                      0 on success,
 *                             -ENOLCK if the lock does not exist,
 *                             -EACCES if the lock owner does not match.
 */
int dss_unlock(struct dss_handle *handle, enum dss_type type,
               const void *item_list, int item_cnt, const char *lock_owner);

/**
 * Retrieve the status of locks.
 *
 * If \a lock_owner is NULL, the strings are not allocated.
 * If \a lock_timestamp is NULL, the structures are not filled.
 *
 * The function will attempt to query the status of as many locks as possible.
 * Should any query fail, the first error code obtained will be returned after
 * attempting to query all other locks's statuses (as-much-as-possible policy).
 *
 * @param[in]   handle          DSS handle.
 * @param[in]   type            Type of the ressources's lock to query.
 * @param[in]   item_list       List of ressources's lock to query.
 * @param[in]   item_cnt        Number of ressources's lock to query.
 * @param[out]  lock_owner      Name of each lock owner, must be freed by
 *                              the caller.
 * @param[out]  lock_timestamp  Date when each lock was taken or last refreshed.
 * @return                      0 on success,
 *                             -ENOMEM if a \a lock_owner string cannot be
 *                              allocated,
 *                             -ENOLCK if a lock does not exist.
 */
int dss_lock_status(struct dss_handle *handle, enum dss_type type,
                    const void *item_list, int item_cnt,
                    char **lock_owner, struct timeval *lock_timestamp);

/**
 * Allocate and return hostname from a lock owner.
 *
 * @param[in]   lock_owner  Lock owner containing the hostname
 * @param[out]  hostname    Hostname allocated and extracted from /p lock_owner
 *                          (NULL if there was an error)
 *
 * @return  0 is success or negative error code
 *          -EBADF if we can't extract hostname from lock owner
 */
int dss_hostname_from_lock_owner(const char *lock_owner, char **hostname);

#endif
