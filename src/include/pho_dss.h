/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2022 CEA/DAM.
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
#include "pho_cfg.h"
#include "pho_common.h"

#include <stdint.h>
#include <stdlib.h>

/* Maximum lock_owner size, related to the database schema */
#define PHO_DSS_MAX_LOCK_OWNER_LEN 256

/* Maximum lock id size, related to the database schema */
#define PHO_DSS_MAX_LOCK_ID_LEN 2048

/** item types */
enum dss_type {
    DSS_NONE   = -2,
    DSS_INVAL  = -1,
    DSS_OBJECT =  0,
    DSS_DEPREC,
    DSS_LAYOUT,
    DSS_DEVICE,
    DSS_MEDIA,
    DSS_MEDIA_UPDATE_LOCK,
    DSS_LOGS,
    DSS_LAST,
};

static const char * const dss_type_names[] = {
    [DSS_OBJECT] = "object",
    [DSS_DEPREC] = "deprec",
    [DSS_LAYOUT] = "layout",
    [DSS_DEVICE] = "device",
    [DSS_MEDIA]  = "media",
    [DSS_MEDIA_UPDATE_LOCK]  = "media_update",
    [DSS_LOGS] = "logs",
};

#define MAX_UPDATE_LOCK_TRY 5
#define UPDATE_LOCK_SLEEP_MICRO_SECONDS 5000

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
    DSS_SET_UPDATE_ADM_STATUS, /** Atomic update of adm_status column */
    DSS_SET_UPDATE_HOST,
    DSS_SET_DELETE,
    DSS_SET_LAST,
};

static const char * const dss_set_actions_names[] = {
    [DSS_SET_INSERT]            = "insert",
    [DSS_SET_FULL_INSERT]       = "full-insert",
    [DSS_SET_UPDATE]            = "update",
    [DSS_SET_UPDATE_ADM_STATUS] = "update_adm_status",
    [DSS_SET_UPDATE_HOST]       = "update_host",
    [DSS_SET_DELETE]            = "delete",
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
    /* Logs related fields */
    {"DSS::LOG::family", "family"},
    {"DSS::LOG::device", "device"},
    {"DSS::LOG::medium", "medium"},
    {"DSS::LOG::errno", "errno"},
    {"DSS::LOG::cause", "cause"},
    {"DSS::LOG::start", "time"},
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

/**
 * Media update bits fields : 64 bits are available
 */
#define ADM_STATUS          (1<<0)
#define FS_STATUS           (1<<1)
#define FS_LABEL            (1<<2)
#define NB_OBJ_ADD          (1<<3)
#define LOGC_SPC_USED_ADD   (1<<4)
#define PHYS_SPC_USED       (1<<5)
#define PHYS_SPC_FREE       (1<<6)
#define TAGS                (1<<7)
#define PUT_ACCESS          (1<<8)
#define GET_ACCESS          (1<<9)
#define DELETE_ACCESS       (1<<10)
#define NB_OBJ              (1<<11)
#define LOGC_SPC_USED       (1<<12)

#define IS_STAT(_f) ((NB_OBJ | NB_OBJ_ADD | LOGC_SPC_USED | LOGC_SPC_USED_ADD |\
                      PHYS_SPC_USED | PHYS_SPC_FREE) & (_f))

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
 * Retrieve usable devices information from DSS, meaning devices that are
 * unlocked.
 *
 * @param[in]  hdl      valid connection handle
 * @param[in]  family   family of the devices to retrieve
 * @param[in]  host     host of the devices to retrieve, if NULL, will retrieve
 *                      usable devices of every host
 * @param[out] dev_ls   list of retrieved items to be freed w/ dss_res_free()
 * @param[out] dev_cnt  number of items retrieved in the list
 *
 * @return 0 on success, negated errno on failure
 */
int dss_get_usable_devices(struct dss_handle *hdl, const enum rsc_family family,
                           const char *host, struct dev_info **dev_ls,
                           int *dev_cnt);

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
 * Retrieve media containing extents of an object from DSS
 * @param[in]  hdl      valid connection handle
 * @param[in]  obj      object information
 * @param[out] media    list of retrieved media, to be freed with dss_res_free()
 * @param[out] cnt      number of media retrieved in the list
 *
 * @return 0 on success, -errno on failure
 *                       -EINVAL if \p obj doesn't exist in the DSS
 */
int dss_media_of_object(struct dss_handle *hdl, struct object_info *obj,
                        struct media_info **media, int *cnt);

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
 * Retrieve logs information from DSS
 *
 * @param[in]   hdl      valid connection handle
 * @param[in]   filter   assembled DSS filtering criteria
 * @param[out]  logs_ls  list of retrieved items to be freed w/ dss_res_free()
 * @param[out]  logs_cnt number of items retrieved in the list
 *
 * @return 0 on success, negated errno on failure
 */
int dss_logs_get(struct dss_handle *hdl, const struct dss_filter *filter,
                 struct pho_log **logs_ls, int *logs_cnt);

/**
 * Clear logs information from DSS
 *
 * @param[in]   hdl      valid connection handle
 * @param[in]   filter   assembled DSS filtering criteria, if NULL, will delete
 *                       every log
 *
 * @return 0 on success, negated errno on failure
 */
int dss_logs_delete(struct dss_handle *hdl, const struct dss_filter *filter);

/**
 *  Generic function: frees item_list that was allocated in dss_xxx_get()
 *  @param[in]  item_list   list of items to free
 *  @param[in]  item_cnt    number of items in item_list
 */
void dss_res_free(void *item_list, int item_cnt);

/**
 * Insert information of one or many devices in DSS.
 * @param[in]  hdl      valid connection handle
 * @param[in]  dev_ls   array of entries to store
 * @param[in]  dev_cnt  number of items in the list
 *
 * @return 0 on success, negated errno on failure
 */
int dss_device_insert(struct dss_handle *hdl, struct dev_info *dev_ls,
                      int dev_cnt);

/**
 * Delete information of one or many devices in DSS.
 * @param[in]  hdl      valid connection handle
 * @param[in]  dev_ls   array of entries to delete
 * @param[in]  dev_cnt  number of items in the list
 *
 * @return 0 on success, negated errno on failure
 */
int dss_device_delete(struct dss_handle *hdl, struct dev_info *dev_ls,
                      int dev_cnt);

/**
 * Update adm_status of one or many devices in DSS.
 * @param[in]  hdl      valid connection handle
 * @param[in]  dev_ls   array of entries to update with new adm_status
 * @param[in]  dev_cnt  number of items in the list
 *
 * @return 0 on success, negated errno on failure
 */
int dss_device_update_adm_status(struct dss_handle *hdl,
                                 struct dev_info *dev_ls,
                                 int dev_cnt);

/**
 * Update host of one or many devices in DSS.
 * @param[in]  hdl      valid connection handle
 * @param[in]  dev_ls   array of entries to update with new host
 * @param[in]  dev_cnt  number of items in the list
 *
 * @return 0 on success, negated errno on failure
 */
int dss_device_update_host(struct dss_handle *hdl, struct dev_info *dev_ls,
                           int dev_cnt);

/**
 * Store information for one or many media in DSS.
 * @param[in]  hdl      valid connection handle
 * @param[in]  med_ls   array of entries to store
 * @param[in]  med_cnt  number of items in the list
 * @param[in]  action   operation code (insert, update, delete)
 * @param[in]  fields   fields to update (ignored for insert and delete)
 *
 * @return 0 on success, negated errno on failure
 */
int dss_media_set(struct dss_handle *hdl, struct media_info *med_ls,
                  int med_cnt, enum dss_set_action action, uint64_t fields);

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
 * If the medium of the family dir is not locked by anyone, -ENODEV is returned
 * because it means that the corresponding LRS is not started or that the medium
 * is failed.
 * If the medium is not locked by anyone and its family is not dir, NULL is
 * returned as hostname.
 *
 * @param[in]   dss         DSS to request
 * @param[in]   medium_id   Medium to locate
 * @param[out]  hostname    Allocated and returned hostname or NULL if the
 *                          medium is not locked by anyone
 * @param[out]  medium_info Allocated and returned additionnal information about
 *                          the medium or NULL if the medium couldn't be
 *                          retrieved
 *
 * @return 0 if success, -errno if an error occurs and hostname is irrelevant
 *         -ENOENT if no medium with medium_id exists in media table
 *         -ENODEV if a medium dir is not currently locked
 *         -EACCES if medium is admin locked
 *         -EPERM if medium get operation flag is set to false
 */
int dss_medium_locate(struct dss_handle *dss, const struct pho_id *medium_id,
                      char **hostname, struct media_info **medium_info);

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
 *
 * @return                      0 on success,
 *                             -EEXIST if one of the targeted locks already
 *                              exists.
 */
int dss_lock(struct dss_handle *handle, enum dss_type type,
             const void *item_list, int item_cnt);

/**
 * Take locks on a specific hostname.
 *
 * If any lock cannot be taken, then the ones that already are will be
 * forcefully unlocked, and the function will not try to lock any other
 * ressource (all-or-nothing policy).
 *
 * @param[in]   handle          DSS handle.
 * @param[in]   type            Type of the resources to lock.
 * @param[in]   item_list       List of resources to lock.
 * @param[in]   item_cnt        Number of resources to lock.
 * @param[in]   hostname        Hostname of the lock to set
 *
 * @return                      0 on success,
 *                             -EEXIST if one of the targeted locks already
 *                              exists.
 */
int dss_lock_hostname(struct dss_handle *handle, enum dss_type type,
                      const void *item_list, int item_cnt,
                      const char *hostname);

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
 *
 * @return                      0 on success,
 *                             -ENOLCK if the lock does not exist,
 *                             -EACCES if the lock owner does not match.
 */
int dss_lock_refresh(struct dss_handle *handle, enum dss_type type,
                     const void *item_list, int item_cnt);

/**
 * Release locks.
 *
 * If \p force_unlock is true, the lock's hostname and owner are not matched
 * against the caller's.
 *
 * The function will attempt to unlock as many locks as possible. Should any
 * unlock fail, the first error code obtained will be returned after
 * attempting to unlock all other locks (as-much-as-possible policy).
 *
 * @param[in]   handle          DSS handle.
 * @param[in]   type            Type of the ressources to unlock.
 * @param[in]   item_list       List of ressources to unlock.
 * @param[in]   item_cnt        Number of ressources to unlock.
 * @param[in]   force_unlock    Whether we ignore the lock's hostname and owner
 *                              or not.
 *
 * @return                      0 on success,
 *                             -ENOLCK if the lock does not exist,
 *                             -EACCES if the lock owner does not match.
 */
int dss_unlock(struct dss_handle *handle, enum dss_type type,
               const void *item_list, int item_cnt, bool force_unlock);

/**
 * Retrieve the status of locks.
 *
 * If \p locks is NULL, the structures are not filled.
 *
 * The function will attempt to query the status of as many locks as possible.
 * Should any query fail, the first error code obtained will be returned after
 * attempting to query all other locks's statuses (as-much-as-possible policy).
 *
 * @param[in]   handle          DSS handle.
 * @param[in]   type            Type of the ressources's lock to query.
 * @param[in]   item_list       List of ressources's lock to query.
 * @param[in]   item_cnt        Number of ressources's lock to query.
 * @param[out]  locks           List of \p item_cnt structures, filled with
 *                              each lock owner, hostname and timestamp, must
 *                              be cleaned by calling pho_lock_clean.
 *
 * @return                      0 on success,
 *                             -ENOMEM if a \p lock_owner string cannot be
 *                              allocated,
 *                             -ENOLCK if a lock does not exist.
 */
int dss_lock_status(struct dss_handle *handle, enum dss_type type,
                    const void *item_list, int item_cnt,
                    struct pho_lock *locks);

/**
 * Clean locks based on hostname and type.
 *
 * The function will attempt to clean all device locks not owned by the
 * device's host and with a different owner.
 *
 * @param[in]   handle          DSS handle.
 * @param[in]   lock_family     Family of the locks to clean.
 * @param[in]   lock_hostname   Hostname owning the locks to clean.
 * @param[in]   lock_owner      Owner whose locks shouldn't be removed.
 *
 * @return                      0 on success
 */
int dss_lock_device_clean(struct dss_handle *handle, const char *lock_family,
                          const char *lock_hostname, int lock_owner);

/**
 * Clean media locks based on hostname and owner.
 *
 * The function will attempt to clean all media locks having a certain
 * hostname and owner that shouldn't be locked.
 *
 * @param[in]   handle          DSS handle.
 * @param[in]   media           List of media that are actually locked and
 *                              shouldn't be modified.
 * @param[in]   media_cnt       Number of ressources actually locked.
 * @param[in]   lock_hostname   Hostname owning the locks to clean.
 * @param[in]   lock_owner      Owner whose locks shouldn't be removed.
 *
 * @return                      0 on success
 */
int dss_lock_media_clean(struct dss_handle *handle,
                         const struct media_info *media, int media_cnt,
                         const char *lock_hostname, int lock_owner);

/**
 * Clean all locks with the given parameters.
 *
 * The function will attempt to clean all locks which have a certain
 * hostname and/or owner and/or type and/or family and/or id.
 *
 * Lock_hostname, lock_type, dev_family and lock_id can be NULL.
 *
 * If type is NULL, a non-NULL family parameter will not be considered.
 *
 * @param[in]   handle          DSS handle.
 * @param[in]   lock_hostname   Hostname owning the locks to clean.
 * @param[in]   lock_type       Type of the ressources to clean.
 * @param[in]   dev_family      Family of the devices/media to clean.
 * @param[in]   lock_ids        List of ids of the ressources to clean.
 * @param[in]   n_ids           Number of ids.
 *
 * @return                      0 on success
 *                              errno on failure
 */
int dss_lock_clean_select(struct dss_handle *handle,
                          const char *lock_hostname, const char *lock_type,
                          const char *dev_family, char **lock_ids, int n_ids);

/**
 * Clean all locks.
 *
 * @param[in]   handle          DSS handle.
 *
 * @return                      0 on success
 *                              errno on failure
 */
int dss_lock_clean_all(struct dss_handle *handle);

/**
 * Emit a log.
 *
 * @param[in]   dss            DSS to request
 * @param[in]   log            error log to emit
 *
 * @return 0 if success, -errno if an error occurs
 */
int dss_emit_log(struct dss_handle *dss, struct pho_log *log);

/**
 * Create a valid dss_filter based on the criteria given in \p log_filter.
 *
 * @param[in]   log_filter      Criteria which will be transformed into a
 *                              dss_filter
 * @param[out]  dss_log_filter  Filter corresponding to the given criteria
 *
 * @return 0 on success, -errno on failure
 */
int create_logs_filter(struct pho_log_filter *log_filter,
                       struct dss_filter **dss_log_filter);

/**
 * List of valid configuration values for tape_model
 *
 * Mutualized between dss.c and lrs_device.c
 */
enum pho_cfg_params_tape_model {
    /* parameters */
    PHO_CFG_TAPE_MODEL_supported_list,

    /* Delimiters, update when modifying options */
    PHO_CFG_TAPE_MODEL_FIRST = PHO_CFG_TAPE_MODEL_supported_list,
    PHO_CFG_TAPE_MODEL_LAST  = PHO_CFG_TAPE_MODEL_supported_list,
};

extern const struct pho_config_item cfg_tape_model[];

#endif
