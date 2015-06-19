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
    DSS_CMP_EQ, /**< equal (==) */
    DSS_CMP_NE, /**< not equal (!=) */
    DSS_CMP_GT, /**< greater than (>) */
    DSS_CMP_GE, /**< greater or equal (>=) */
    DSS_CMP_LT, /**< less than (<) */
    DSS_CMP_LE, /**< less or equal (<=) */
};

/** fields of all item types */
enum dss_fields {
    /* object related fields */
    DSS_OBJ_oid,     /**< object id */
    DSS_OBJ_status,  /**< status: stable, pending, removed... */
    DSS_OBJ_user_md, /**< JSON blob of arbitrary user MD */
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

    DSS_DEV_type,
    DSS_DEV_id,
    DSS_DEV_host, /* FUTURE: hosts (indexed JSON array) */
    DSS_DEV_adm_status, /* locked/unlocked */

    /** @TODO to be continued (media...) */
};

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
 *  @param[out] item_list list of retrieved items (the list and its contents
 *                        must be freed by the caller).
 *  @param[out] item_cnt  number of items in item_list.
 */
int dss_get(void *dss_handle, enum dss_type type, struct dss_crit *crit,
            int crit_cnt, void **item_list, int *item_cnt);

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
