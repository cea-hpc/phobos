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
    DSS_EXTENT,
    DSS_DEVICE,
    DSS_MEDIA,
    DSS_MEDIA_UPDATE_LOCK,
    DSS_LOGS,
    DSS_FULL_LAYOUT,
    DSS_COPY,
    DSS_LAST,
};

static const char * const dss_type_names[] = {
    [DSS_OBJECT] = "object",
    [DSS_DEPREC] = "deprec",
    [DSS_LAYOUT] = "layout",
    [DSS_EXTENT] = "extent",
    [DSS_DEVICE] = "device",
    [DSS_MEDIA]  = "media",
    [DSS_MEDIA_UPDATE_LOCK]  = "media_update",
    [DSS_LOGS] = "logs",
    [DSS_FULL_LAYOUT] = "full_layout",
    [DSS_COPY] = "copy",
};

#define MAX_UPDATE_LOCK_TRY 5
#define UPDATE_LOCK_SLEEP_MICRO_SECONDS 5000

/** The different types of update allowed for devices */
enum dss_device_operations {
    DSS_DEVICE_UPDATE_ADM_STATUS = (1 << 0),
    DSS_DEVICE_UPDATE_HOST = (1 << 1),
    DSS_DEVICE_UPDATE_LIBRARY = (1 << 2),
};

/** The different types of update allowed for objects */
enum dss_object_operations {
    DSS_OBJECT_UPDATE_USER_MD = (1 << 0),
    DSS_OBJECT_UPDATE_OID = (1 << 1),
};

/** The different types of update allowed for copies */
enum dss_copy_operations {
    DSS_COPY_UPDATE_ACCESS_TIME = (1 << 0),
    DSS_COPY_UPDATE_COPY_STATUS = (1 << 1),
};

/** The different status filter */
enum dss_status_filter {
    DSS_STATUS_FILTER_INCOMPLETE = (1 << 0),
    DSS_STATUS_FILTER_READABLE   = (1 << 1),
    DSS_STATUS_FILTER_COMPLETE   = (1 << 2),
    DSS_STATUS_FILTER_ALL        = DSS_STATUS_FILTER_INCOMPLETE |
        DSS_STATUS_FILTER_READABLE | DSS_STATUS_FILTER_COMPLETE,
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
                           *  the default values when adding a row.
                           *  In the case of an object, it means adding a
                           *  preexisting object with a particular uuid and
                           *  version, not the default DSS values.
                           *  In the case of an extent, it means setting a
                           *  provided creation time instead of the default
                           *  "now" DSS value.
                           */
    DSS_SET_UPDATE,
    DSS_SET_DELETE,
    DSS_SET_LAST,
};

static const char * const dss_set_actions_names[] = {
    [DSS_SET_INSERT]             = "insert",
    [DSS_SET_FULL_INSERT]        = "full-insert",
    [DSS_SET_UPDATE]             = "update",
    [DSS_SET_DELETE]             = "delete",
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
    {"DSS::OBJ::uuid", "object_uuid"},
    {"DSS::OBJ::version", "version"},
    {"DSS::OBJ::user_md", "user_md"},
    {"DSS::OBJ::layout_info", "lyt_info"},
    {"DSS::OBJ::layout_type", "lyt_info->>'name'"},
    {"DSS::OBJ::creation_time", "creation_time"},
    {"DSS::OBJ::access_time", "access_time"},
    {"DSS::OBJ::deprec_time", "deprec_time"},
    /* Layout related fields */
    {"DSS::LYT::object_uuid", "object_uuid"},
    {"DSS::LYT::version", "version"},
    {"DSS::LYT::extent_uuid", "extent_uuid"},
    {"DSS::LYT::layout_index", "layout_index"},
    {"DSS::LYT::copy_name", "copy_name"},
    /* Extent related fields */
    {"DSS::EXT::uuid", "extent_uuid"},
    {"DSS::EXT::state", "state"},
    {"DSS::EXT::size", "size"},
    {"DSS::EXT::medium_family", "medium_family"},
    {"DSS::EXT::medium_id", "medium_id"},
    {"DSS::EXT::medium_library", "medium_library"},
    {"DSS::EXT::address", "address"},
    {"DSS::EXT::md5", "hash->>'md5'"},
    {"DSS::EXT::xxh128", "hash->>'xxh128'"},
    {"DSS::EXT::info", "info"},
    {"DSS::EXT::creation_time", "creation_time"},
    /* Media related fields */
    {"DSS::MDA::family", "family"},
    {"DSS::MDA::model", "model"},
    {"DSS::MDA::id", "id"},
    {"DSS::MDA::library", "library"},
    {"DSS::MDA::groupings", "groupings"},
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
    {"DSS::DEV::library", "library"},
    {"DSS::DEV::host", "host"},
    {"DSS::DEV::adm_status", "adm_status"},
    {"DSS::DEV::model", "model"},
    {"DSS::DEV::path", "path"},
    {"DSS::DEV::lock", "lock"},
    /* Logs related fields */
    {"DSS::LOG::family", "family"},
    {"DSS::LOG::device", "device"},
    {"DSS::LOG::medium", "medium"},
    {"DSS::LOG::library", "library"},
    {"DSS::LOG::errno", "errno"},
    {"DSS::LOG::cause", "cause"},
    {"DSS::LOG::start", "time"},
    {"DSS::LOG::end", "time"},
    /* Copy related fields */
    {"DSS::COPY::copy_name", "copy_name"},
    {"DSS::COPY::copy_status", "copy_status"},
    {"DSS::COPY::object_uuid", "object_uuid"},
    {"DSS::COPY::version", "version"},
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
#define LIBRARY             (1<<13)
#define GROUPINGS           (1<<14)

#define IS_STAT(_f) ((NB_OBJ | NB_OBJ_ADD | LOGC_SPC_USED | LOGC_SPC_USED_ADD |\
                      PHYS_SPC_USED | PHYS_SPC_FREE) & (_f))

struct dss_filter {
    json_t  *df_json;
};

struct dss_sort {
    /**
     * The attribute to sort the list on
     */
    const char *attr;

    /**
     * Boolean to indicate if it's a reverse sort or not
     */
    bool reverse;

    /**
     * Boolean to indicate if the column is in the "lock" table
     */
    bool is_lock;

    /**
     * Boolean to indicate if the sort is in psql
     */
    bool psql_sort;
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
 *  Generic function: frees item_list that was allocated in dss_xxx_get()
 *  @param[in]  item_list   list of items to free
 *  @param[in]  item_cnt    number of items in item_list
 */
mockable
void dss_res_free(void *item_list, int item_cnt);

/**
 * Execute a select query and store the result in item_list
 *
 * @param[in]  handle    Connection handle
 * @param[in]  type      Type of the resource to get
 * @param[in]  clause    Query to execute
 * @param[out] item_list List of items retrieved
 * @param[out] item_cnt  Number of items retrieved
 */
int dss_execute_generic_get(struct dss_handle *handle, enum dss_type type,
                            GString *clause, void **item_list, int *item_cnt);

/**
 * Insert information of one or many devices in DSS.
 *
 * @param[in]  handle        valid connection handle
 * @param[in]  device_list   array of entries to store
 * @param[in]  device_count  number of items in the list
 *
 * @return 0 on success, negated errno on failure
 */
int dss_device_insert(struct dss_handle *handle, struct dev_info *device_list,
                      int device_count);

/**
 * Update the information of one or many devices in DSS.
 *
 * @param[in]  handle        valid connection handle
 * @param[in]  src_list      array of entries to select
 * @param[in]  dst_list      array of entries to update
 * @param[in]  device_count  number of items in the list
 *
 * @return 0 on success, negated errno on failure
 */
int dss_device_update(struct dss_handle *handle, struct dev_info *src_list,
                      struct dev_info *dst_list, int device_count,
                      int64_t fields);

/**
 * Retrieve devices information from DSS.
 *
 * @param[in]  handle        valid connection handle
 * @param[in]  filter        assembled DSS filtering criteria
 * @param[out] device_list   list of retrieved items to be
 *                           freed w/ dss_res_free()
 * @param[out] device_count  number of items retrieved in the list
 *
 * @return 0 on success, negated errno on failure
 */
int dss_device_get(struct dss_handle *handle, const struct dss_filter *filter,
                   struct dev_info **device_list, int *device_count,
                   struct dss_sort *sort);

/**
 * Delete information of one or many devices in DSS.
 *
 * @param[in]  handle        valid connection handle
 * @param[in]  device_list   array of entries to delete
 * @param[in]  device_count  number of items in the list
 *
 * @return 0 on success, negated errno on failure
 */
int dss_device_delete(struct dss_handle *handle, struct dev_info *device_list,
                      int device_count);

/**
 * Store information for one or many media in DSS.
 *
 * @param[in]  handle       valid connection handle
 * @param[in]  media_list   array of entries to store
 * @param[in]  media_count  number of items in the list
 *
 * @return 0 on success, negated errno on failure
 */
int dss_media_insert(struct dss_handle *handle, struct media_info *media_list,
                     int media_count);

/**
 * Update information for one or many media in DSS.
 *
 * @param[in]  handle       valid connection handle
 * @param[in]  src_list     array of entries to select
 * @param[in]  dst_list     array of entries to update
 * @param[in]  media_count  number of items in the list
 * @param[in]  fields       fields to update (ignored for insert and delete)
 *
 * @return 0 on success, negated errno on failure
 */
int dss_media_update(struct dss_handle *handle, struct media_info *src_list,
                     struct media_info *dst_list, int media_count,
                     uint64_t fields);

/**
 * Retrieve media information from DSS.
 *
 * @param[in]  handle       valid connection handle
 * @param[in]  filter       assembled DSS filtering criteria
 * @param[out] media_list   list of retrieved items to be freed
 *                          w/ dss_res_free()
 * @param[out] media_count  number of items retrieved in the list
 *
 * @return 0 on success, negated errno on failure
 */
mockable
int dss_media_get(struct dss_handle *handle, const struct dss_filter *filter,
                  struct media_info **media_list, int *media_count,
                  struct dss_sort *sort);

/**
 * Delete information for one or many media in DSS.
 *
 * @param[in]  handle       valid connection handle
 * @param[in]  media_list   array of entries to delete
 * @param[in]  media_count  number of items in the list
 *
 * @return 0 on success, negated errno on failure
 */
int dss_media_delete(struct dss_handle *handle, struct media_info *media_list,
                     int media_count);

/**
 * Store information for one or many extents in DSS.
 *
 * @param[in]  handle        valid connection handle
 * @param[in]  extents       array of entries to store
 * @param[in]  extent_count  number of items in the list
 *
 * @return 0 on success, negated errno on failure
 */
int dss_extent_insert(struct dss_handle *handle, struct extent *extents,
                      int extent_count, enum dss_set_action action);

/**
 * Update information for one or many extents in DSS.
 *
 * @param[in]  handle        valid connection handle
 * @param[in]  src_extents   array of entries to select
 * @param[in]  dst_extents   array of entries to update
 * @param[in]  extent_count  number of items in the list
 *
 * @return 0 on success, negated errno on failure
 */
int dss_extent_update(struct dss_handle *handle, struct extent *src_extents,
                      struct extent *dst_extents, int extent_count);

/**
 * Retrieve extent information from DSS.
 *
 * @param[in]  handle        valid connection handle
 * @param[in]  filter        assembled DSS filtering criteria
 * @param[out] extents       list of retrieved items to be freed
 *                           w/ dss_res_free()
 * @param[out] extent_count  number of items retrieved in the list
 * @param[in]  sort          sort filter
 *
 * @return 0 on success, negated errno on failure
 */
int dss_extent_get(struct dss_handle *handle, const struct dss_filter *filter,
                   struct extent **extents, int *extent_count,
                   struct dss_sort *sort);

/**
 * Delete information for one or many extent in DSS.
 *
 * @param[in]  handle       valid connection handle
 * @param[in]  extents      array of entries to delete
 * @param[in]  extent_count number of items in the list
 *
 * @return 0 on success, negated errno on failure
 */
int dss_extent_delete(struct dss_handle *handle, struct extent *extents,
                      int extent_count);

/**
 * Store information for one or many layouts in DSS.
 *
 * @param[in]  handle        valid connection handle
 * @param[in]  layout_list   array of entries to store
 * @param[in]  layout_count  number of items in the list
 *
 * @return 0 on success, negated errno on failure
 */
int dss_layout_insert(struct dss_handle *handle,
                      struct layout_info *layout_list,
                      int layout_count);

/**
 * Retrieve layout information from DSS.
 *
 * @param[in]  handle       valid connection handle
 * @param[in]  filter       assembled DSS filtering criteria
 * @param[out] layouts      list of retrieved items to be freed w/
 *                          dss_res_free()
 * @param[out] layout_count number of items retrieved in the list
 *
 * @return 0 on success, negated errno on failure
 */
int dss_layout_get(struct dss_handle *handle, const struct dss_filter *filter,
                   struct layout_info **layouts, int *layout_count);

/**
 * Delete information for one or many layouts in DSS.
 *
 * @param[in]  handle        valid connection handle
 * @param[in]  layout_list   array of entries to delete
 * @param[in]  layout_count  number of items in the list
 *
 * @return 0 on success, negated errno on failure
 */
int dss_layout_delete(struct dss_handle *handle,
                      struct layout_info *layout_list, int layout_count);

/**
 * Retrieve layout + extents information from DSS
 * @param[in]  hdl           valid connection handle
 * @param[in]  object        assembled DSS filtering criteria on objects
 * @param[in]  med_lib       assembled DSS filtering criteria on media and
 *                           library
 * @param[out] layouts       list of retrieved items to be freed with
 *                           dss_res_free()
 * @param[out] layout_count  number of items retrieved in the list
 *
 * @return 0 on success, negated errno on failure
 */
int dss_full_layout_get(struct dss_handle *hdl, const struct dss_filter *object,
                        const struct dss_filter *med_lib,
                        struct layout_info **layouts, int *layout_count,
                        struct dss_sort *sort);

/**
 * Store information for one or many objects in DSS.
 *
 * @param[in]  handle        valid connection handle
 * @param[in]  object_list   array of entries to store
 * @param[in]  object_count  number of items in the list
 * @param[in]  action        operation code (insert or full_insert)
 *
 * @return 0 on success, negated errno on failure
 */
int dss_object_insert(struct dss_handle *handle,
                      struct object_info *object_list,
                      int object_count, enum dss_set_action action);

/**
 * Update the information of one or many objects in DSS.
 *
 * @param[in]  handle        valid connection handle
 * @param[in]  src_list      array of entries to select
 * @param[in]  dst_list      array of entries to update
 * @param[in]  object_count  number of items in the list
 * @param[in]  fields        fields to update
 *
 * @return 0 on success, negated errno on failure
 */
int dss_object_update(struct dss_handle *handle, struct object_info *src_list,
                      struct object_info *dst_list, int object_count,
                      int64_t fields);

/**
 * Retrieve object information from DSS.
 *
 * @param[in]  handle        valid connection handle
 * @param[in]  filter        assembled DSS filtering criteria
 * @param[out] object_list   list of retrieved items to be freed
 *                           w/ dss_res_free()
 * @param[out] object_count  number of items retrieved in the list
 * @param[in]  sort          sort filter
 * @return 0 on success, negated errno on failure
 */
int dss_object_get(struct dss_handle *handle, const struct dss_filter *filter,
                   struct object_info **object_list, int *object_count,
                   struct dss_sort *sort);

/**
 * Delete information for one or many objects in DSS.
 *
 * @param[in]  handle        valid connection handle
 * @param[in]  object_list   array of entries to delete
 * @param[in]  object_count  number of items in the list
 *
 * @return 0 on success, negated errno on failure
 */
int dss_object_delete(struct dss_handle *handle,
                      struct object_info *object_list, int object_count);

/**
 * Rename the information of one or many objects in DSS.
 *
 * /!\ Warning: this function must be called with a \p living_list and \p
 * deprec_list that have the same OID.
 *
 * @param[in]  handle       valid connection handle
 * @param[in]  living_list  array of living objects to update
 * @param[in]  living_count number of items in the living objects list (0 or 1)
 * @param[in]  deprec_list  array of deprecated objects to update
 * @param[in]  deprec_count number of items in the deprecated objects list
 * @param[in]  new_oid      new oid of the living and deprecated objects
 *
 * @return 0 on success, negated errno on failure
 */
int dss_object_rename(struct dss_handle *handle,
                      struct object_info *living_list, int living_count,
                      struct object_info *deprec_list, int deprec_count,
                      char *new_oid);

/**
 * Store information for one or many deprecated objects in DSS.
 *
 * @param[in] handle        valid connection handle
 * @param[in] object_list   array of entries to store
 * @param[in] object_count  number of items in the list
 *
 * @return 0 on success, negated errno on failure
 */
int dss_deprecated_object_insert(struct dss_handle *handle,
                              struct object_info *object_list,
                              int object_count);

/**
 * Update information for one or many deprecated objects in DSS.
 *
 * @param[in] handle        valid connection handle
 * @param[in] src_list      array of entries to select
 * @param[in] dst_list      array of entries to update
 * @param[in] object_count  number of items in the list
 * @param[in] fields        fields to update
 *
 * @return 0 on success, negated errno on failure
 */
int dss_deprecated_object_update(struct dss_handle *handle,
                              struct object_info *src_list,
                              struct object_info *dst_list,
                              int object_count, int64_t fields);

/**
 * Retrieve deprecated object information from DSS.
 *
 * @param[in]  handle        valid connection handle
 * @param[in]  filter        assembled DSS filtering criteria
 * @param[out] object_list   list of retrieved items to be freed
 *                            w/ dss_res_free()
 * @param[out] object_count  number of items retrieved in the list
 * @param[in]  sort          sort filter
 * @return 0 on success, negated errno on failure
 */
int dss_deprecated_object_get(struct dss_handle *handle,
                              const struct dss_filter *filter,
                              struct object_info **object_list,
                              int *object_count, struct dss_sort *sort);

/**
 * Delete information for one or many deprecated objects in DSS.
 *
 * @param[in] handle        valid connection handle
 * @param[in] object_list   array of entries to delete
 * @param[in] object_count  number of items in the list
 *
 * @return 0 on success, negated errno on failure
 */
int dss_deprecated_object_delete(struct dss_handle *handle,
                              struct object_info *object_list,
                              int object_count);

/**
 * Store information for one or many copies in DSS.
 *
 * @param[in] handle      valid connection handle
 * @param[in] copy_list   array of entries to store
 * @param[in] copy_count  number of items in the list
 *
 * @return 0 on success, negated errno on failure
 */
int dss_copy_insert(struct dss_handle *handle, struct copy_info *copy_list,
                    int copy_count);

/**
 * Update the information of one or many copies in DSS.
 *
 * @param[in]  handle        valid connection handle
 * @param[in]  src_list      array of entries to select
 * @param[in]  dst_list      array of entries to update
 * @param[in]  copy_count    number of items in the list
 * @param[in]  fields        fields to update
 *
 * @return 0 on success, negated errno on failure
 */
int dss_copy_update(struct dss_handle *handle, struct copy_info *src_list,
                    struct copy_info *dst_list, int copy_count, int64_t fields);

/**
 * Retrieve copy information from DSS.
 *
 * @param[in]  handle        valid connection handle
 * @param[in]  filter        assembled DSS filtering criteria
 * @param[out] copy_list     list of retrieved items to be freed
 *                            w/ dss_res_free()
 * @param[out] copy_count    number of items retrieved in the list
 * @param[in]  sort          sort filter
 * @return 0 on success, negated errno on failure
 */
int dss_copy_get(struct dss_handle *handle, const struct dss_filter *filter,
                 struct copy_info **copy_list, int *copy_count,
                 struct dss_sort *sort);

/**
 * Delete information for one or many copies in DSS.
 *
 * @param[in] handle        valid connection handle
 * @param[in] copy_list     array of entries to delete
 * @param[in] copy_count    number of items in the list
 *
 * @return 0 on success, negated errno on failure
 */
int dss_copy_delete(struct dss_handle *handle, struct copy_info *copy_list,
                    int copy_count);

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
 * Insert logs into the DSS
 *
 * @param[in]   hdl       valid connection handle
 * @param[out]  logs      list of logs to insert
 * @param[out]  logs_cnt  number of logs to insert
 *
 * @return 0 on success, negated errno on failure
 */
int dss_logs_insert(struct dss_handle *hdl, struct pho_log *logss,
                    int logs_cnt);

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
 * Take locks on a specific hostname, only used with locate operations.
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
 * Take lock and set its last_locate field, only used if an early lock was
 * previously taken for those items.
 *
 * If any lock cannot be taken, then the ones that already are will be
 * forcefully unlocked, and the function will not try to lock any other
 * resource (all-or-nothing policy).
 *
 * @param[in]   handle          DSS handle.
 * @param[in]   type            Type of the resources to lock.
 * @param[in]   item_list       List of resources to lock.
 * @param[in]   item_cnt        Number of resources to lock.
 * @param[in]   last_locate     Last early lock timestamp.
 *
 * @return                      0 on success,
 *                             -EEXIST if one of the targeted locks already
 *                              exists.
 */
int dss_lock_with_last_locate(struct dss_handle *handle, enum dss_type type,
                              const void *item_list, int item_cnt,
                              struct timeval *last_locate);

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
 * @param[in]   locate          True if only need to update `last_locate` field.
 *
 * @return                      0 on success,
 *                             -ENOLCK if the lock does not exist,
 *                             -EACCES if the lock owner does not match.
 */
int dss_lock_refresh(struct dss_handle *handle, enum dss_type type,
                     const void *item_list, int item_cnt, bool locate);

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
 * Emit a fully formated log to the DSS if necessary
 *
 * \param[in] dss      Valid DSS handle
 * \param[in] log      Log to emit
 * \param[in] action   Action performed that triggered the log emission
 *                     If the action is different from log->cause, the cause is
 *                     added to the log.
 * \param[in] rc       Result of the action (can be 0 on success)
 *
 * This function will free the log message and message (if not NULL).
 *
 * If the emission of the log fails, a message is emitted to stderr instead
 * containing the full log.
 */
void emit_log_after_action(struct dss_handle *dss,
                           struct pho_log *log,
                           enum operation_type action,
                           int rc);
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
