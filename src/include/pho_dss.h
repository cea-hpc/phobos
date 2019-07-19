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

/** item types */
enum dss_type {
    DSS_INVAL  = -1,
    DSS_OBJECT =  0,
    DSS_EXTENT,
    DSS_DEVICE,
    DSS_MEDIA,
    DSS_LAST,
};

static const char * const dss_type_names[] = {
    [DSS_OBJECT] = "object",
    [DSS_EXTENT] = "extent",
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
    DSS_SET_UPDATE,
    DSS_SET_DELETE,
    DSS_SET_LAST,
};

static const char * const dss_set_actions_names[] = {
    [DSS_SET_INSERT] = "insert",
    [DSS_SET_UPDATE] = "update",
    [DSS_SET_DELETE] = "delete",
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
    {"DSS::OBJ::user_md", "user_md"},
    /* Extent related fields */
    {"DSS::EXT::oid", "oid"},
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
 * Retrieve extent information from DSS
 * @param[in]  hdl      valid connection handle
 * @param[in]  filter   assembled DSS filtering criteria
 * @param[out] lyt_ls   list of retrieved items to be freed w/ dss_res_free()
 * @param[out] lyt_cnt  number of items retrieved in the list
 *
 * @return 0 on success, negated errno on failure
 */
int dss_extent_get(struct dss_handle *hdl, const struct dss_filter *filter,
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
 * Store information for one or many extents in DSS.
 * @param[in]  hdl      valid connection handle
 * @param[in]  lyt_ls   array of entries to store
 * @param[in]  lyt_cnt  number of items in the list
 * @param[in]  action   operation code (insert, update, delete)
 *
 * @return 0 on success, negated errno on failure
 */
int dss_extent_set(struct dss_handle *hdl, struct layout_info *lyt_ls,
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
 *  Lock a device for concurrent accesses
 *  @param[in] item_list  list of items
 *  @param[in] item_cnt   number of items in item_list.
 *  @param[in] lock_owner name of the new owner of the lock
 *  @retval 0 on success
 *  @retval -EEXIST on lock failure (device(s) already locked)
 */
int dss_device_lock(struct dss_handle *handle, struct dev_info *dev_ls,
                    int dev_cnt, const char *lock_owner);

/**
 *  Unlock a device
 *  @param[in] item_list  list of items
 *  @param[in] item_cnt   number of items in item_list.
 *  @param[in] lock_owner name of the owner of the lock to be released; will
 *                        fail if the lock is currently locked by another owner.
 *                        If NULL, will unlock inconditionally.
 *  @retval 0 on success, -ENOLCK on failure (no previous lock or previous
 *          lock had a different name)
 */
int dss_device_unlock(struct dss_handle *handle, struct dev_info *dev_ls,
                      int dev_cnt, const char *lock_owner);

/**
 *  Lock a media for concurrent accesses
 *  @param[in] item_list  list of items
 *  @param[in] item_cnt   number of items in item_list.
 *  @param[in] lock_owner name of the new owner of the lock
 *  @retval 0 on success
 *  @retval -EEXIST on lock failure (device(s) already locked)
 */
int dss_media_lock(struct dss_handle *handle, struct media_info *media_ls,
                   int media_cnt, const char *lock_owner);

/**
 *  Unlock a media
 *  @param[in] item_list list of items
 *  @param[in] item_cnt  number of items in item_list.
 *  @param[in] lock_owner name of the owner of the lock to be released; will
 *                        fail if the lock is currently locked by another owner.
 *                        If NULL, will unlock inconditionally.
 *  @retval 0 on success, -ENOLCK on failure (no previous lock or previous
 *          lock had a different name)
 */
int dss_media_unlock(struct dss_handle *handle, struct media_info *media_ls,
                     int media_cnt, const char *lock_owner);
#endif
