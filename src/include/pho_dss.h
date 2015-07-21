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
#include <stdint.h>

/** item types */
enum dss_type {
    DSS_OBJECT,
    DSS_EXTENT,
    DSS_DEVICE,
    DSS_MEDIA
};

/** comparators */
enum dss_cmp {
    DSS_CMP_EQ, /**< equal (=) */
    DSS_CMP_NE, /**< not equal (!=) */
    DSS_CMP_GT, /**< greater than (>) */
    DSS_CMP_GE, /**< greater or equal (>=) */
    DSS_CMP_LT, /**< less than (<) */
    DSS_CMP_LE, /**< less or equal (<=) */
    DSS_CMP_LIKE, /**< LIKE (LIKE) */
    DSS_CMP_JSON_CTN, /**< Json contain=>subset (@>) */
    DSS_CMP_JSON_EXIST, /**< Json test key/array at top level (?) */
    DSS_CMP_LAST
};

static const char * const dss_cmp_names[] = {
    [DSS_CMP_EQ] = "=",
    [DSS_CMP_NE] = "!=",
    [DSS_CMP_GT] = ">",
    [DSS_CMP_GE] = ">=",
    [DSS_CMP_LT] = "<",
    [DSS_CMP_LE] = "<=",
    [DSS_CMP_LE] = "LIKE",
    [DSS_CMP_JSON_CTN] = "@>",
    [DSS_CMP_JSON_EXIST] = "?"
};

static inline const char *dss_cmp2str(enum dss_cmp cmp)
{
    if (cmp >= DSS_CMP_LAST || cmp < 0)
        return NULL;
    return dss_cmp_names[cmp];
}

/** fields of all item types */
enum dss_fields {
    /* object related fields */
    DSS_OBJ_oid,     /**< object id */
    DSS_OBJ_status,  /**< status: stable, pending, removed... */
    DSS_OBJ_user_md, /**< JSON blob of arbitrary user MD */
    DSS_OBJ_st_md, /**< JSON blob of arbitrary st. MD */
    DSS_OBJ_info,    /**< JSON blob with object statistics, lifecycle info... */

    /* extent related fields */
    DSS_EXT_oid,      /* == OBJ_oid? */
    DSS_EXT_copy_num, /* copy number */
    DSS_EXT_status,   /* copy state: stable, pending, removed, ... */
    DSS_EXT_layout_descr, /* JSON: layout type + params (stripe count,
                                                         stripe width, ...) */
    DSS_EXT_layout_info,  /* JSON: layout info:[ {stripe_idx, media, address,
                                                  size}, ... ] */
    DSS_EXT_info,     /* extent info (statistics, lifecycle info...) */

    /* Media @ v0 */
    DSS_MDA_family, /* family of media */
    DSS_MDA_model, /* type of media */
    DSS_MDA_id,
    DSS_MDA_adm_status, /* ready, failed, ... */
    DSS_MDA_fs_status, /* blank, empty, ...  */
    DSS_MDA_format, /* fs type */
    DSS_MDA_stats,
    DSS_MDA_nb_obj, /* ->>stats  */
    DSS_MDA_vol_used, /* ->>stats phys. used */
    DSS_MDA_vol_free, /* ->>stats phys. free */

    /* Device @ v0 */
    DSS_DEV_id,
    DSS_DEV_family,
    DSS_DEV_host, /* FUTURE: hosts (indexed JSON array) */
    DSS_DEV_adm_status, /* locked/unlocked */
    DSS_DEV_model, /* Device type */
    DSS_DEV_path, /* Device path */


    DSS_FIELDS_LAST,
};

static const char * const dss_fields_names[] = {
    [DSS_OBJ_oid] = "oid",
    [DSS_OBJ_status] = "status",
    [DSS_OBJ_user_md] = "user_md",
    [DSS_OBJ_st_md] = "st_md",
    [DSS_OBJ_info] = "info",
    [DSS_EXT_oid] = "oid",
    [DSS_EXT_copy_num] = "copy_num",
    [DSS_EXT_status] = "status",
    [DSS_EXT_layout_descr] = "layout_descr",
    [DSS_EXT_layout_info] = "layout_info",
    [DSS_EXT_info] = "info",
    [DSS_MDA_family] = "family",
    [DSS_MDA_model] = "model",
    [DSS_MDA_id] = "id",
    [DSS_MDA_adm_status] = "adm_status",
    [DSS_MDA_fs_status] = "fs_status",
    [DSS_MDA_format] = "fs_type",
    [DSS_MDA_stats] = "stats",
    [DSS_MDA_nb_obj] = "stats::json->>'nb_obj'",
    [DSS_MDA_vol_used] = "stats::json->>'phys_spc_used'",
    [DSS_MDA_vol_free] = "stats::json->>'phys_spc_free'",
    [DSS_DEV_family] = "family",
    [DSS_DEV_id] = "id",
    [DSS_DEV_host] = "host",
    [DSS_DEV_adm_status] = "adm_status",
    [DSS_DEV_model] = "model",
    [DSS_DEV_path] = "path",
};

static inline const char *dss_fields2str(enum dss_fields fields)
{
    if (fields >= DSS_FIELDS_LAST || fields < 0)
        return NULL;
    return dss_fields_names[fields];
}


/** types */
enum dss_valtype {
    DSS_VAL_UNKNOWN = -1,
    DSS_VAL_BIGINT   = 0,
    DSS_VAL_INT      = 1,
    DSS_VAL_BIGUINT  = 2,
    DSS_VAL_UINT     = 3,
    DSS_VAL_STR      = 4,
    DSS_VAL_ENUM     = 5,
    DSS_VAL_JSON     = 6
};

static const int const dss_fields_type[] = {
    [DSS_OBJ_oid] = DSS_VAL_STR,
    [DSS_OBJ_status] = DSS_VAL_ENUM,
    [DSS_OBJ_user_md] = DSS_VAL_JSON,
    [DSS_OBJ_st_md] = DSS_VAL_JSON,
    [DSS_OBJ_info] = DSS_VAL_JSON,
    [DSS_EXT_oid] = DSS_VAL_STR,
    [DSS_EXT_copy_num] = DSS_VAL_UNKNOWN,
    [DSS_EXT_status] = DSS_VAL_ENUM,
    [DSS_EXT_layout_descr] = DSS_VAL_UNKNOWN,
    [DSS_EXT_layout_info] = DSS_VAL_UNKNOWN,
    [DSS_EXT_info] = DSS_VAL_JSON,
    [DSS_MDA_family] = DSS_VAL_ENUM,
    [DSS_MDA_model] = DSS_VAL_ENUM,
    [DSS_MDA_id] = DSS_VAL_STR,
    [DSS_MDA_adm_status] = DSS_VAL_ENUM,
    [DSS_MDA_fs_status] = DSS_VAL_ENUM,
    [DSS_MDA_format] = DSS_VAL_ENUM,
    [DSS_MDA_stats] = DSS_VAL_JSON,
    [DSS_MDA_nb_obj] = DSS_VAL_INT,
    [DSS_MDA_vol_used] = DSS_VAL_BIGINT,
    [DSS_MDA_vol_free] = DSS_VAL_BIGINT,
    [DSS_DEV_family] = DSS_VAL_ENUM,
    [DSS_DEV_id] = DSS_VAL_STR,
    [DSS_DEV_host] = DSS_VAL_STR,
    [DSS_DEV_adm_status] = DSS_VAL_ENUM,
    [DSS_DEV_model] = DSS_VAL_STR,
    [DSS_DEV_path] = DSS_VAL_STR,
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
    default:
        return NULL;
    }
    return NULL;
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

#endif
