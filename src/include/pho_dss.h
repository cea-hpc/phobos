/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 * Copyright 2014-2015 CEA/DAM. All Rights Reserved.
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
 *  get dss_type enum from string
 *  @param[in]   dss_type as str
 *  @result  dss_type  as enum
*/
static inline enum dss_type str2dss_type(const char *str)
{
    int i;

    for (i = 0; i < DSS_LAST; i++)
        if (!strcmp(str, dss_type_names[i]))
            return i;
    return DSS_INVAL;
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
 *  get dss_type enum from string for reg. test
 *  @param[in]   dss_set_action as str
 *  @result  dss_set_action  as enum
 */
static inline enum dss_set_action str2dss_set_action(const char *str)
{
    int i;

    for (i = 0; i < DSS_SET_LAST; i++)
        if (!strcmp(str, dss_set_actions_names[i]))
            return i;
    return DSS_SET_INVAL;
}


/** comparators */
enum dss_cmp {
    DSS_CMP_INVAL = -1,
    DSS_CMP_EQ    =  0,  /**< equal (=) */
    DSS_CMP_NE,          /**< not equal (!=) */
    DSS_CMP_GT,          /**< greater than (>) */
    DSS_CMP_GE,          /**< greater or equal (>=) */
    DSS_CMP_LT,          /**< less than (<) */
    DSS_CMP_LE,          /**< less or equal (<=) */
    DSS_CMP_LIKE,        /**< LIKE (LIKE) */
    DSS_CMP_JSON_CTN,    /**< Json contain=>subset (@>) */
    DSS_CMP_JSON_EXIST,  /**< Json test key/array at top level (?) */
    DSS_CMP_LAST
};

static const char * const dss_cmp_names[] = {
    [DSS_CMP_EQ]         = "=",
    [DSS_CMP_NE]         = "!=",
    [DSS_CMP_GT]         = ">",
    [DSS_CMP_GE]         = ">=",
    [DSS_CMP_LT]         = "<",
    [DSS_CMP_LE]         = "<=",
    [DSS_CMP_LIKE]       = "LIKE",
    [DSS_CMP_JSON_CTN]   = "@>",
    [DSS_CMP_JSON_EXIST] = "?"
};

static inline const char *dss_cmp2str(enum dss_cmp cmp)
{
    if (cmp >= DSS_CMP_LAST || cmp < 0)
        return NULL;
    return dss_cmp_names[cmp];
}

/**
 *  get cmp enum from string used for reg. test
 *  Test use only
 *  @param[in]   Comparator as str
 *  @result  Comparator as enum
*/
static inline enum dss_cmp str2dss_cmp(const char *str)
{
    int i;

    for (i = 0; i < DSS_CMP_LAST; i++)
        if (!strcmp(str, dss_cmp_names[i]))
            return i;
    return DSS_CMP_INVAL;
}

/** fields of all item types */
enum dss_fields {
    DSS_FIELDS_INVAL = -1,

    /* object related fields */
    DSS_OBJ_oid,     /**< object id */
    DSS_OBJ_user_md, /**< JSON blob of arbitrary user MD */

    /* extent related fields */
    DSS_EXT_oid,      /* == OBJ_oid? */
    DSS_EXT_copy_num, /* copy number */
    DSS_EXT_state,   /* copy state: stable, pending, removed, ... */
    DSS_EXT_layout_type, /* JSON: layout type */
    DSS_EXT_layout_info,  /* JSON: layout info:[ {stripe_idx, media, address,
                                                  size}, ... ] */
    DSS_EXT_info,     /* extent info (statistics, lifecycle info...) */
    DSS_EXT_media_idx, /* access extents media id with index */

    /* Media @ v0 */
    DSS_MDA_family,     /* family of media */
    DSS_MDA_model,      /* type of media */
    DSS_MDA_id,
    DSS_MDA_adm_status, /* ready, failed, ... */
    DSS_MDA_fs_status,  /* blank, empty, ...  */
    DSS_MDA_address_type,
    DSS_MDA_fs_type,     /* fs type */
    DSS_MDA_stats,
    DSS_MDA_nb_obj,     /* ->>stats  */
    DSS_MDA_vol_used,   /* ->>stats phys. used */
    DSS_MDA_vol_free,   /* ->>stats phys. free */

    /* Device @ v0 */
    DSS_DEV_serial,
    DSS_DEV_family,
    DSS_DEV_host, /* FUTURE: hosts (indexed JSON array) */
    DSS_DEV_adm_status, /* locked/unlocked */
    DSS_DEV_model, /* Device type */
    DSS_DEV_path, /* Device path */
    DSS_DEV_changer_idx,


    DSS_FIELDS_LAST,
};

static const char * const dss_fields_names[] = {
    [DSS_OBJ_oid] = "oid",
    [DSS_OBJ_user_md] = "user_md",
    [DSS_EXT_oid] = "oid",
    [DSS_EXT_copy_num] = "copy_num",
    [DSS_EXT_state] = "state",
    [DSS_EXT_layout_type] = "lyt_type",
    [DSS_EXT_layout_info] = "lyt_info",
    [DSS_EXT_info] = "info",
    [DSS_EXT_media_idx] = "extents_mda_idx(extent.extents)",
    [DSS_MDA_family] = "family",
    [DSS_MDA_model] = "model",
    [DSS_MDA_id] = "id",
    [DSS_MDA_adm_status] = "adm_status",
    [DSS_MDA_fs_status] = "fs_status",
    [DSS_MDA_fs_type] = "fs_type",
    [DSS_MDA_address_type] = "address_type",
    [DSS_MDA_stats] = "stats",
    [DSS_MDA_nb_obj] = "stats::json->>'nb_obj'",
    [DSS_MDA_vol_used] = "(stats->>'phys_spc_used')::bigint",
    [DSS_MDA_vol_free] = "(stats->>'phys_spc_free')::bigint",
    [DSS_DEV_family] = "family",
    [DSS_DEV_serial] = "id",
    [DSS_DEV_host] = "host",
    [DSS_DEV_adm_status] = "adm_status",
    [DSS_DEV_model] = "model",
    [DSS_DEV_path] = "path",
    [DSS_DEV_changer_idx] = "changer_idx",
};

static inline const char *dss_fields2str(enum dss_fields fields)
{
    if (fields >= DSS_FIELDS_LAST || fields < 0)
        return NULL;
    return dss_fields_names[fields];
}

/**
 *  Get dss_field from string
 *  Test use only, no guarantees
 *  @param[in]   dss_field name as string
 *  @return dss_field enum
 *  @retval DSS_FIELDS_INVAL on failure
 */
static inline enum dss_fields str2dss_fields(const char *str)
{
    int i;

    for (i = 0; i < DSS_FIELDS_LAST; i++)
        if (!strcmp(str, dss_fields_names[i]))
            return i;
    return DSS_FIELDS_INVAL;
}


/** types */
enum dss_valtype {
    DSS_VAL_UNKNOWN = -1,
    DSS_VAL_BIGINT   = 0,
    DSS_VAL_INT      = 1,
    DSS_VAL_BIGUINT  = 2,
    DSS_VAL_UINT     = 3,
    DSS_VAL_STR      = 4,
    DSS_VAL_ARRAY    = 5,
    DSS_VAL_ENUM     = 6,
    DSS_VAL_JSON     = 7
};

static const int const dss_fields_type[] = {
    [DSS_OBJ_oid] = DSS_VAL_STR,
    [DSS_OBJ_user_md] = DSS_VAL_JSON,
    [DSS_EXT_oid] = DSS_VAL_STR,
    [DSS_EXT_copy_num] = DSS_VAL_UINT,
    [DSS_EXT_state] = DSS_VAL_ENUM,
    [DSS_EXT_layout_type] = DSS_VAL_ENUM,
    [DSS_EXT_layout_info] = DSS_VAL_JSON,
    [DSS_EXT_info] = DSS_VAL_JSON,
    [DSS_EXT_media_idx] = DSS_VAL_ARRAY,
    [DSS_MDA_family] = DSS_VAL_ENUM,
    [DSS_MDA_model] = DSS_VAL_STR,
    [DSS_MDA_id] = DSS_VAL_STR,
    [DSS_MDA_adm_status] = DSS_VAL_ENUM,
    [DSS_MDA_fs_status] = DSS_VAL_ENUM,
    [DSS_MDA_address_type] = DSS_VAL_ENUM,
    [DSS_MDA_fs_type] = DSS_VAL_ENUM,
    [DSS_MDA_stats] = DSS_VAL_JSON,
    [DSS_MDA_nb_obj] = DSS_VAL_INT,
    [DSS_MDA_vol_used] = DSS_VAL_BIGINT,
    [DSS_MDA_vol_free] = DSS_VAL_BIGINT,
    [DSS_DEV_family] = DSS_VAL_ENUM,
    [DSS_DEV_serial] = DSS_VAL_STR,
    [DSS_DEV_host] = DSS_VAL_STR,
    [DSS_DEV_adm_status] = DSS_VAL_ENUM,
    [DSS_DEV_model] = DSS_VAL_STR,
    [DSS_DEV_path] = DSS_VAL_STR,
    [DSS_DEV_changer_idx] = DSS_VAL_INT,
};

static inline const int dss_fields2type(enum dss_fields fields)
{
    if (fields >= DSS_FIELDS_LAST || fields < 0)
        return DSS_VAL_UNKNOWN;
    return dss_fields_type[fields];
}

static inline const char *dss_fields_enum2str(enum dss_fields fields, int val)
{
    switch (fields) {
    case DSS_DEV_family:
    case DSS_MDA_family:
        return dev_family2str((enum dev_family) val);
    case DSS_DEV_adm_status:
        return adm_status2str((enum dev_adm_status) val);
    case DSS_MDA_adm_status:
        return media_adm_status2str((enum media_adm_status) val);
    case DSS_MDA_fs_status:
        return fs_status2str((enum fs_status) val);
    case DSS_MDA_fs_type:
        return fs_type2str((enum fs_type) val);
    case DSS_MDA_address_type:
        return address_type2str((enum address_type) val);
    case DSS_EXT_state:
        return extent_state2str((enum extent_state) val);
    case DSS_EXT_layout_type:
        return layout_type2str((enum layout_type) val);
    default:
        pho_error(-EINVAL, "dss_fields_enum2str unmanaged dss_field: %d",
                  fields);
        abort();
    }
}


/**
 *  get enum from type and str
 *  Test use only, no guarantees
 *  @param[in]   dss_fields enum
 *  @param[in]   val as string
 *  @return  corresponding field enum
 */
static inline const int str2dss_fields_enum(enum dss_fields fields, char *val)
{
    switch (fields) {
    case DSS_DEV_family:
    case DSS_MDA_family:
        return (int)str2dev_family(val);
    case DSS_DEV_adm_status:
        return (int)str2adm_status(val);
    case DSS_MDA_adm_status:
        return (int)str2media_adm_status(val);
    case DSS_MDA_fs_status:
        return (int)str2fs_status(val);
    case DSS_MDA_fs_type:
        return (int)str2fs_type(val);
    case DSS_MDA_address_type:
        return (int)str2address_type(val);
    case DSS_EXT_state:
        return (int)str2extent_state(val);
    case DSS_EXT_layout_type:
        return (int)str2layout_type(val);
    default:
        pho_error(-EINVAL, "str2dss_fields_enum unmanaged dss_field %d",
                  fields);
        abort();
    }
}

union dss_val {
    int64_t      val_bigint;
    int32_t      val_int;
    uint64_t     val_biguint;
    uint32_t     val_uint;
    const char  *val_str;
};

struct dss_crit {
    enum dss_fields crit_name;
    enum dss_cmp    crit_cmp;
    union dss_val   crit_val;
};

/**
 *  Automaticaly fill dss_val vals from a string
 *  Test use only, no guarantees
 *  @param[in]   dss_fields enum
 *  @param[in]   value as string
 *  @param[in]   pointer to dss_val
 *  @retval 0 on success
 *  @retval -EINVAL if dss field type is not supported
 */
static inline const int str2dss_val_fill(enum dss_fields fields, char *str,
                                         union dss_val *val)
{
    /* @todo clean parsing for all types */
    switch (dss_fields2type(fields)) {
    case DSS_VAL_ARRAY:
    case DSS_VAL_STR:
        val->val_str = str;
        break;
    case DSS_VAL_INT:
        val->val_biguint = strtol(str, NULL, 10);
        break;
    case DSS_VAL_UINT:
        val->val_biguint = strtoul(str, NULL, 10);
        break;
    case DSS_VAL_BIGINT:
        val->val_bigint = strtoll(str, NULL, 10);
        break;
    case DSS_VAL_BIGUINT:
        val->val_biguint = strtoull(str, NULL, 10);
        break;
    case DSS_VAL_ENUM:
        val->val_int = str2dss_fields_enum(fields, str);
        break;
    case DSS_VAL_JSON:
    case DSS_VAL_UNKNOWN:
    default:
        return -EINVAL;
    }
    return 0;
}

static inline char *dss_char4sql(const char *s)
{
    char *ns;

    if (s != NULL && s[0] != '\0') {
        if (asprintf(&ns, "'%s'", s) == -1)
            return NULL;
    } else {
        ns = strdup("NULL");
    }

    return ns;
}
/** helper to add a criteria to a criteria array */
#define dss_crit_add(_pcrit, _pctr, _n, _cmp, _field, _value) do { \
        (_pcrit)[*(_pctr)].crit_name = (_n);                  \
        (_pcrit)[*(_pctr)].crit_cmp = (_cmp);                 \
        (_pcrit)[*(_pctr)].crit_val._field = (_value);        \
        (*(_pctr))++;                                         \
    } while (0)

/**
 *  Initialize a connection handle
 *  @param[in]  conninfo    Connection information e.g. "dbname = phobos"
 *
 *  @param[out] handle      Connection handle
 */
int dss_init(const char *conninfo, void **handle);

/**
 *  Closes a connection
 *  @param[in]  handle      Connection handle
 */
void dss_fini(void *handle);

/**
 *  Generic function to get/list items from DSS.
 *  @param[in]  type      type of items to query.
 *  @param[in]  crit      list of criteria to select items.
 *  @param[in]  crit_cnt  count of criteria in crit list.
 *  @param[out] item_list list of retrieved items (the caller must call
 *                        dss_res_free() later)
 *  @param[out] item_cnt  number of items in item_list.
 */
int dss_get(void *dss_handle, enum dss_type type, struct dss_crit *crit,
            int crit_cnt, void **item_list, int *item_cnt);

/**
 *  Generic function, frees item_list that was allocated in get
 *  @param[in]  item_list list of items to free
 *  @param[in]  item_cnt  number of items in item_list
 */
void dss_res_free(void *item_list, int item_cnt);

/** wrapper to get devices from DSS */
static inline int dss_device_get(void *dss_handle, struct dss_crit *crit,
                                 int crit_cnt, struct dev_info **dev_ls,
                                 int *dev_cnt)
{
    return dss_get(dss_handle, DSS_DEVICE, crit, crit_cnt, (void **)dev_ls,
                   dev_cnt);
}

/** wrapper to get devices from DSS */
static inline int dss_media_get(void *dss_handle, struct dss_crit *crit,
                                int crit_cnt, struct media_info **med_ls,
                                int *med_cnt)
{
    return dss_get(dss_handle, DSS_MEDIA, crit, crit_cnt, (void **)med_ls,
                   med_cnt);
}

/** wrapper to get extent from DSS */
static inline int dss_extent_get(void *dss_handle, struct dss_crit *crit,
                                 int crit_cnt, struct layout_info **lyt_ls,
                                 int *lyt_cnt)
{
    return dss_get(dss_handle, DSS_EXTENT, crit, crit_cnt, (void **)lyt_ls,
                   lyt_cnt);
}

/**
 *  Generic function to set a list of items in DSS.
 *  @param[in]  type      type of items to set.
 *  @param[out] item_list list of items
 *  @param[out] item_cnt  number of items in item_list.
 */
int dss_set(void *handle, enum dss_type type, void *item_list, int item_cnt,
            enum dss_set_action action);


static inline int dss_device_set(void *dss_handle, struct dev_info *dev_ls,
                                 int dev_cnt, enum dss_set_action action)
{
    return dss_set(dss_handle, DSS_DEVICE, (void *)dev_ls, dev_cnt, action);
}

static inline int dss_media_set(void *dss_handle, struct media_info *media_ls,
                                 int media_cnt, enum dss_set_action action)
{
    return dss_set(dss_handle, DSS_MEDIA, (void *)media_ls, media_cnt, action);
}

static inline int dss_extent_set(void *dss_handle, struct layout_info *lyt_ls,
                                 int lyt_cnt, enum dss_set_action action)
{
    return dss_set(dss_handle, DSS_EXTENT, (void *)lyt_ls, lyt_cnt, action);
}

static inline int dss_object_set(void *dss_handle, struct object_info *obj_ls,
                                 int object_cnt, enum dss_set_action action)
{
    return dss_set(dss_handle, DSS_OBJECT, (void *)obj_ls, object_cnt, action);
}

#endif
