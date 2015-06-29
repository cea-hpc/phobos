/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 * Copyright 2014-2015 CEA/DAM. All Rights Reserved.
 */
/**
 * \brief  Phobos Common types.
 */
#ifndef _PHO_TYPES_H
#define _PHO_TYPES_H

/* glib will be used to handle common structures (GList, GString, ...) */
#include <glib.h>
#include <limits.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/** max length of a tape label, FS label... */
#define PHO_LABEL_MAX_LEN 32
/** max length of a hash-based address in hexadecimal form.
 * A SHA-1 value is always 160 bit long, hexa representation uses 4 bit per
 * character so the string length is 160/4 + null character.
 */

/**
 * Max layout tag length.
 * Make sure to keep it below NAME_MAX (which is like 255 chars)
 */
#define PHO_LAYOUT_TAG_MAX  8


enum media_type {
    PHO_MED_TAPE = 1,
    /* future: disk, file... */
};

/** media identifier */
struct media_id {
    enum media_type type;
    /** XXX type -> id type may not be straightforward as given media type
     * could be addressed in multiple ways (FS label, FS UUID, device WWID...).
     * So, an id_type enum may be required here in a later version. */
    union {
        char label[PHO_LABEL_MAX_LEN];
    } id_u;
};

enum layout_type {
    PHO_LYT_SIMPLE = 1, /* simple contiguous block of data */
    /* future: SPLIT, RAID0, RAID1, ... */
};

/** describe data layout */
struct layout_descr {
    enum layout_type    type;
    /* v00: no other information needed for simple layout */
};

enum fs_type {
    PHO_FS_POSIX, /* any POSIX filesystem (no specific feature) */
    PHO_FS_LTFS,
};

/* selected address type for a media */
enum address_type {
    PHO_ADDR_PATH,   /* id is entry path (e.g. for imported tapes) */
    PHO_ADDR_HASH1,  /* id hashing, implementation 1 */
    PHO_ADDR_OPAQUE, /* opaque identifier provided by the backend */
};

struct pho_buff {
    int   size;
    char *buff;
};

static const struct pho_buff PHO_BUFF_NULL = { .size = 0, .buff = NULL };

/** describe a piece of data in a layout */
struct extent {
    unsigned int       layout_idx; /**< always 0 for simple layouts */
    uint64_t           size;       /**< size of the extent */
    struct media_id    media;
    enum fs_type       fs_type;    /**< type of filesystem on this media */
    enum address_type  addr_type;  /**< way to address this media */
    struct pho_buff    address;    /**< address on the media */
};

/** Address to read/write data.
 * Also holds resource information to be cleaned after the access.
 */
struct data_loc {
    /** mount-point to access the media referenced in the extent */
    GString      *root_path;
    /** the data extent itself */
    struct extent extent;
};

static inline bool is_data_loc_valid(const struct data_loc *loc)
{
    return loc->extent.address.buff != NULL;
}

/**
 * Family of device
 */
enum dev_family {
    PHO_DEV_INVAL = -1,
    PHO_DEV_DISK  = 0,
    PHO_DEV_TAPE  = 1,
    PHO_DEV_COUNT
};

static const char * const dev_family_names[] = {
    [PHO_DEV_DISK] = "disk",
    [PHO_DEV_TAPE] = "tape",
    [PHO_DEV_COUNT] = NULL
};

static inline const char *dev_family2str(enum dev_family family)
{
    if (family > PHO_DEV_COUNT || family < 0)
        return NULL;
    return dev_family_names[family];
}

static inline enum dev_family str2dev_family(const char *str)
{
    int i;

    for (i = 0; i < PHO_DEV_COUNT; i++)
        if (!strcmp(str, dev_family_names[i]))
            return i;
    return PHO_DEV_INVAL;
}

/**
 * Device model
 *  */
enum dev_model {
    PHO_DEV_MODEL_INVAL = -1,
    PHO_DEV_MODEL_ULTRIUM_TD5  = 0,
    PHO_DEV_MODEL_ULTRIUM_TD6  = 1,
    PHO_DEV_MODEL_COUNT
};

static const char * const dev_model_names[] = {
    [PHO_DEV_MODEL_ULTRIUM_TD5] = "ULTRIUM-TD5",
    [PHO_DEV_MODEL_ULTRIUM_TD6] = "ULTRIUM-TD6",
    [PHO_DEV_MODEL_COUNT] = NULL
};

static inline const char *dev_model2str(enum dev_model model)
{
    if (model > PHO_DEV_MODEL_COUNT || model < 0)
        return NULL;
    return dev_model_names[model];
}

static inline enum dev_model str2dev_model(const char *str)
{
    int i;

    for (i = 0; i < PHO_DEV_MODEL_COUNT; i++)
        if (!strcmp(str, dev_model_names[i]))
            return i;
    return PHO_DEV_MODEL_INVAL;
}

/**
 * Device Administrative state
 */
enum dev_adm_status {
    PHO_DEV_ADM_ST_INVAL    = -1,
    PHO_DEV_ADM_ST_LOCKED   = 0,
    PHO_DEV_ADM_ST_UNLOCKED = 1,
    PHO_DEV_ADM_ST_COUNT
};

static const char * const dev_adm_st_names[] = {
    [PHO_DEV_ADM_ST_LOCKED] = "locked",
    [PHO_DEV_ADM_ST_UNLOCKED] = "unlocked",
    [PHO_DEV_ADM_ST_COUNT] = NULL
};

static inline const char *adm_status2str(enum dev_adm_status adm_st)
{
    if (adm_st > PHO_DEV_ADM_ST_COUNT || adm_st < 0)
        return NULL;
    return dev_adm_st_names[adm_st];
}

static inline enum dev_adm_status str2adm_status(const char *str)
{
    int i;

    for (i = 0; i < PHO_DEV_ADM_ST_COUNT; i++)
        if (!strcmp(str, dev_adm_st_names[i]))
            return i;
    return PHO_DEV_ADM_ST_INVAL;
}

/**
 * Device operational state
 */
enum dev_op_status {
    PHO_DEV_OP_ST_INVAL   = -1,
    PHO_DEV_OP_ST_FAILED  = 0,
    PHO_DEV_OP_ST_EMPTY   = 1,
    PHO_DEV_OP_ST_LOADED  = 2,
    PHO_DEV_OP_ST_MOUNTED = 3,
    PHO_DEV_OP_ST_BUSY    = 4,
    PHO_DEV_OP_ST_COUNT
};

static const char * const dev_op_st_names[] = {
    [PHO_DEV_OP_ST_FAILED]  = "failed",
    [PHO_DEV_OP_ST_EMPTY]   = "empty",
    [PHO_DEV_OP_ST_LOADED]  = "loaded",
    [PHO_DEV_OP_ST_MOUNTED] = "mounted",
    [PHO_DEV_OP_ST_BUSY]    = "busy",
    [PHO_DEV_OP_ST_COUNT]   = NULL
};

static inline const char *op_status2str(enum dev_op_status op_st)
{
    if (op_st > PHO_DEV_OP_ST_BUSY || op_st < 0)
        return NULL;
    return dev_op_st_names[op_st];
}

static inline enum dev_op_status str2op_status(const char *str)
{
    int i;

    for (i = 0; i < PHO_DEV_OP_ST_COUNT; i++)
        if (!strcmp(str, dev_op_st_names[i]))
            return i;
    return PHO_DEV_OP_ST_INVAL;
}

/** Persistent device information (from DB) */
struct dev_info {
    enum dev_family      family;
    /* Device types and their compatibility rules are configurable
     * So, use string instead of enum. */
    char                *type;
    char                *model;
    char                *path;
    char                *serial;
    int                  changer_idx; /**< drive index in mtx */
    enum dev_adm_status  adm_status;
};

/** Live device information (from system) */
struct dev_state {
    enum dev_op_status   op_status;
    char                *model;
    char                *serial;
    char                *mnt_path; /**< FS path, if mounted */
    struct media_id      media;    /**< media, if loaded or mounted */
};

/** media statistics */
struct media_stats {
    uint64_t           nb_obj;    /**< number of objects stored on media */
    size_t             logc_spc_used;  /**< space used (logical)  */
    size_t             phys_spc_used;  /**< space used (physical) */
    size_t             phys_spc_free;  /**< free space (physical) */

    int32_t            nb_load; /**< number of times the tape was loaded
                                     into a drive */
    int32_t            nb_errors; /**< number of errors encountered
                                     while accessing this tape */
    time_t             last_load; /**< last time the tape was mounted into a
                                       drive */
};

/** persistent media and filesystem information */
struct media_info {
    struct media_id      media;
    enum fs_type         fs_type;    /**< type of filesystem on this media */
    enum address_type    addr_type;  /**< way to address this media */
    struct media_stats   stats;      /**< metrics */
};

struct object_info {
    char                *oid;
    char                *user_md;
    struct layout_descr  layout_descr;
    struct extent       *extent;  /* use data_loc instead? */
};

#endif
