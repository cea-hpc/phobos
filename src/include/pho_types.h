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
#include <errno.h>

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
    PHO_FS_INVAL = -1,
    PHO_FS_POSIX = 0, /* any POSIX filesystem (no specific feature) */
    PHO_FS_LTFS,
    PHO_FS_LAST,
};

static const char * const fs_type_names[] = {
    [PHO_FS_POSIX] = "POSIX",
    [PHO_FS_LTFS]  = "LTFS",
};

static inline const char *fs_type2str(enum fs_type type)
{
    if (type >= PHO_FS_LAST || type < 0)
        return NULL;
    return fs_type_names[type];
}

static inline enum fs_type str2fs_type(const char *str)
{
    int i;

    for (i = 0; i < PHO_FS_LAST; i++)
        if (!strcmp(str, fs_type_names[i]))
            return i;
    return PHO_FS_INVAL;
}

enum fs_status {
    PHO_FS_STATUS_INVAL = -1,
    PHO_FS_STATUS_blank = 0, /* any POSIX filesystem (no specific feature) */
    PHO_FS_STATUS_empty,
    PHO_FS_STATUS_used,
    PHO_FS_STATUS_LAST,
};

static const char * const fs_status_names[] = {
    [PHO_FS_STATUS_blank] = "blank",
    [PHO_FS_STATUS_empty]  = "empty",
    [PHO_FS_STATUS_used]  = "used",
};

static inline const char *fs_status2str(enum fs_status status)
{
    if (status >= PHO_FS_STATUS_LAST || status < 0)
        return NULL;
    return fs_status_names[status];
}

static inline enum fs_status str2fs_status(const char *str)
{
    int i;

    for (i = 0; i < PHO_FS_STATUS_LAST; i++)
        if (!strcmp(str, fs_status_names[i]))
            return i;
    return PHO_FS_STATUS_INVAL;
}

/* selected address type for a media */
enum address_type {
    PHO_ADDR_INVAL = -1,
    PHO_ADDR_PATH  =  0,   /* id is entry path (e.g. for imported tapes) */
    PHO_ADDR_HASH1,  /* id hashing, implementation 1 */
    PHO_ADDR_OPAQUE, /* opaque identifier provided by the backend */
    PHO_ADDR_LAST,
};

static const char * const address_type_names[] = {
    [PHO_ADDR_PATH] = "PATH",
    [PHO_ADDR_HASH1] = "HASH1",
    [PHO_ADDR_OPAQUE] = "OPAQUE",
};

static inline const char *address_type2str(enum address_type type)
{
    if (type >= PHO_ADDR_LAST || type < 0)
        return NULL;
    return address_type_names[type];
}

static inline enum address_type str2address_type(const char *str)
{
    int i;

    for (i = 0; i < PHO_ADDR_LAST; i++)
        if (!strcmp(str, address_type_names[i]))
            return i;
    return PHO_ADDR_INVAL;
}

struct pho_buff {
    int   size;
    char *buff;
};

static const struct pho_buff PHO_BUFF_NULL = { .size = 0, .buff = NULL };

/**
 * Family of device opr media
 */
enum dev_family {
    PHO_DEV_INVAL = -1,
    PHO_DEV_DISK  = 0,
    PHO_DEV_TAPE  = 1,
    PHO_DEV_DIR   = 2,
    PHO_DEV_LAST
};

static const char * const dev_family_names[] = {
    [PHO_DEV_DISK] = "disk",
    [PHO_DEV_TAPE] = "tape",
    [PHO_DEV_DIR]  = "dir",
};

static inline const char *dev_family2str(enum dev_family family)
{
    if (family >= PHO_DEV_LAST || family < 0)
        return NULL;
    return dev_family_names[family];
}

static inline enum dev_family str2dev_family(const char *str)
{
    int i;

    for (i = 0; i < PHO_DEV_LAST; i++)
        if (!strcmp(str, dev_family_names[i]))
            return i;
    return PHO_DEV_INVAL;
}

/** media identifier */
struct media_id {
    enum dev_family type;
    /** XXX type -> id type may not be straightforward as given media type
     * could be addressed in multiple ways (FS label, FS UUID, device WWID...).
     * So, an id_type enum may be required here in a later version. */
    union {
        char label[PHO_LABEL_MAX_LEN];
        char path[NAME_MAX];
    } id_u;
};

/**
 * Get media identifier string, depending of media type.
 */
static inline const char *media_id_get(const struct media_id *mid)
{
    switch (mid->type) {
    case PHO_DEV_TAPE:
        return mid->id_u.label;
    case PHO_DEV_DIR:
        return mid->id_u.path;
    default:
        return NULL;
    }
}

/**
 * Set the appropiate media identifier.
 * type field must be set in media_id.
 */
static inline int media_id_set(struct media_id *mid, const char *id)
{
    switch (mid->type) {
    case PHO_DEV_TAPE:
        if (strlen(id) >= PHO_LABEL_MAX_LEN)
            return -EINVAL;
        strncpy(mid->id_u.label, id, PHO_LABEL_MAX_LEN);
        return 0;
    case PHO_DEV_DIR:
        if (strlen(id) >= NAME_MAX)
            return -EINVAL;
        strncpy(mid->id_u.path, id, NAME_MAX);
        return 0;
    default:
        return -EINVAL;
    }
}

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
    char         *root_path;
    /** the data extent itself */
    struct extent extent;
};

static inline bool is_data_loc_valid(const struct data_loc *loc)
{
    return loc->extent.address.buff != NULL;
}

/**
 * Device Administrative state
 */
enum dev_adm_status {
    PHO_DEV_ADM_ST_INVAL    = -1,
    PHO_DEV_ADM_ST_LOCKED   = 0,
    PHO_DEV_ADM_ST_UNLOCKED = 1,
    PHO_DEV_ADM_ST_FAILED   = 2,
    PHO_DEV_ADM_ST_LAST
};

static const char * const dev_adm_st_names[] = {
    [PHO_DEV_ADM_ST_LOCKED] = "locked",
    [PHO_DEV_ADM_ST_UNLOCKED] = "unlocked",
    [PHO_DEV_ADM_ST_FAILED] = "failed",
};

static inline const char *adm_status2str(enum dev_adm_status adm_st)
{
    if (adm_st >= PHO_DEV_ADM_ST_LAST || adm_st < 0)
        return NULL;
    return dev_adm_st_names[adm_st];
}

static inline enum dev_adm_status str2adm_status(const char *str)
{
    int i;

    for (i = 0; i < PHO_DEV_ADM_ST_LAST; i++)
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
    PHO_DEV_OP_ST_LAST,
    PHO_DEV_OP_ST_UNSPEC = PHO_DEV_OP_ST_LAST,
};

static const char * const dev_op_st_names[] = {
    [PHO_DEV_OP_ST_FAILED]  = "failed",
    [PHO_DEV_OP_ST_EMPTY]   = "empty",
    [PHO_DEV_OP_ST_LOADED]  = "loaded",
    [PHO_DEV_OP_ST_MOUNTED] = "mounted",
    [PHO_DEV_OP_ST_BUSY]    = "busy",
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

    for (i = 0; i < PHO_DEV_OP_ST_LAST; i++)
        if (!strcmp(str, dev_op_st_names[i]))
            return i;
    return PHO_DEV_OP_ST_INVAL;
}

/** Persistent device information (from DB) */
struct dev_info {
    enum dev_family      family;
    /* Device types and their compatibility rules are configurable
     * So, use string instead of enum. */
    char                *model;
    char                *path;
    char                *host;
    char                *serial;
    int                  changer_idx; /**< drive index in mtx */
    enum dev_adm_status  adm_status;
};

/** Live device information (from system) */
struct dev_state {
    enum dev_op_status   op_status;
    enum dev_family      family;
    char                *model;
    char                *serial;
    char                *mnt_path; /**< FS path, if mounted */
    struct media_id      media_id; /**< media, if loaded or mounted */
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

/**
 *  * Media Administrative state
 *   */
enum media_adm_status {
    PHO_MDA_ADM_ST_INVAL    = -1,
    PHO_MDA_ADM_ST_LOCKED   = 0,
    PHO_MDA_ADM_ST_UNLOCKED = 1,
    PHO_MDA_ADM_ST_LAST
};

static const char * const media_adm_st_names[] = {
    [PHO_MDA_ADM_ST_LOCKED] = "locked",
    [PHO_MDA_ADM_ST_UNLOCKED] = "unlocked",
};

static inline const char *media_adm_status2str(enum media_adm_status adm_st)
{
    if (adm_st >= PHO_MDA_ADM_ST_LAST || adm_st < 0)
        return NULL;
    return media_adm_st_names[adm_st];
}

static inline enum media_adm_status media_str2adm_status(const char *str)
{
    int i;

    for (i = 0; i < PHO_MDA_ADM_ST_LAST; i++)
        if (!strcmp(str, media_adm_st_names[i]))
            return i;
    return PHO_MDA_ADM_ST_INVAL;
}


/** persistent media and filesystem information */
struct media_info {
    struct media_id        id;
    enum fs_type           fs_type;    /**< type of filesystem on this media */
    enum address_type      addr_type;  /**< way to address this media */
    char                  *model;
    enum media_adm_status  adm_status;
    enum fs_status         fs_status;
    struct media_stats     stats;      /**< metrics */
};

struct object_info {
    char                *oid;
    char                *user_md;
    struct layout_descr  layout_descr;
    struct extent       *extent;  /* use data_loc instead? */
};

#endif
