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
#include <sys/types.h>
#include <errno.h>

#include "pho_attrs.h"

/** max length of a tape label, FS label... */
#define PHO_LABEL_MAX_LEN 32

/** max length of media URI */
#define PHO_URI_MAX (NAME_MAX + 1)

/**
 * Max layout tag length.
 * Make sure to keep it below NAME_MAX (which is like 255 chars)
 */
#define PHO_LAYOUT_TAG_MAX  8

enum extent_state {
    PHO_EXT_ST_INVAL = -1,
    PHO_EXT_ST_PENDING = 0,
    PHO_EXT_ST_SYNC = 1,
    PHO_EXT_ST_ORPHAN = 2,
    PHO_EXT_ST_LAST = 3,
};

static const char * const extent_state_names[] = {
    [PHO_EXT_ST_PENDING] = "pending",
    [PHO_EXT_ST_SYNC]  = "sync",
    [PHO_EXT_ST_ORPHAN] = "orphan",
};

static inline const char *extent_state2str(enum extent_state state)
{
    if (state >= PHO_EXT_ST_LAST || state < 0)
        return NULL;
    return extent_state_names[state];
}

static inline enum extent_state str2extent_state(const char *str)
{
    int i;

    for (i = 0; i < PHO_EXT_ST_LAST; i++)
        if (!strcmp(str, extent_state_names[i]))
            return i;
    return PHO_EXT_ST_INVAL;
}

/**
 * Generic module description.
 */
#define PHO_MOD_DESC_KEY_NAME   "name"
#define PHO_MOD_DESC_KEY_MAJOR  "major"
#define PHO_MOD_DESC_KEY_MINOR  "minor"
#define PHO_MOD_DESC_KEY_ATTRS  "attrs"
struct module_desc {
    char            *mod_name;  /**< Mandatory module name */
    int              mod_major; /**< Mandatory module major version number */
    int              mod_minor; /**< Mandatory module minor version number */
    struct pho_attrs mod_attrs; /**< Optional set of arbitrary attributes  */
};

/**
 * Layout of an object.
 */
struct layout_info {
    char                *oid;           /**< Referenced object */
    enum extent_state    state;         /**< Object stability state */
    struct module_desc   layout_desc;   /**< Layout module used to write it */
    size_t               wr_size;       /**< Encoding write size */
    struct extent       *extents;       /**< List of data extents */
    int                  ext_count;     /**< Number of extents in the list */
};

/**
 * Library type.
 */
enum lib_type {
    PHO_LIB_INVAL = -1,
    PHO_LIB_DUMMY = 0, /**< fake library, all media are always online. */
    PHO_LIB_SCSI  = 1, /**< SCSI library */
    PHO_LIB_LAST
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
    PHO_FS_STATUS_BLANK = 0, /**< media is not formatted */
    PHO_FS_STATUS_EMPTY,     /**< media is formatted, no data written to it */
    PHO_FS_STATUS_USED,      /**< media contains data */
    PHO_FS_STATUS_FULL,      /**< media is full, no more data can be written */
    PHO_FS_STATUS_LAST,
};

static const char * const fs_status_names[] = {
    [PHO_FS_STATUS_BLANK] = "blank",
    [PHO_FS_STATUS_EMPTY] = "empty",
    [PHO_FS_STATUS_USED]  = "used",
    [PHO_FS_STATUS_FULL]  = "full",
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
    int     size;
    char   *buff;
};

struct pho_lock {
    time_t   lock_ts;
    char    *lock;
};


static const struct pho_buff PHO_BUFF_NULL = { .size = 0, .buff = NULL };

/**
 * Family of device or media
 */
enum dev_family {
    PHO_DEV_INVAL = -1,
    PHO_DEV_DISK  = 0,
    PHO_DEV_TAPE  = 1,
    PHO_DEV_DIR   = 2,
    PHO_DEV_LAST,
    PHO_DEV_UNSPEC = PHO_DEV_LAST,
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

    /** Media identifier (tape label or URI) */
    char id[PHO_URI_MAX];
};

/**
 * Get media identifier string, depending of media type.
 */
static inline const char *media_id_get(const struct media_id *mid)
{
    return mid->id;
}

/**
 * Set the appropiate media identifier.
 */
static inline int media_id_set(struct media_id *mid, const char *id)
{
    if (strlen(id) >= PHO_URI_MAX)
        return -EINVAL;

    strncpy(mid->id, id, sizeof(mid->id));
    return 0;
}

/** describe a piece of data in a layout */
struct extent {
    int                 layout_idx; /**< always 0 for simple layouts */
    ssize_t             size;       /**< size of the extent */
    struct media_id     media;      /**< identifier of the media */
    struct pho_buff     address;    /**< address on the media */
    /*
     * @FIXME: addr_type is a media field in the database, should it be removed
     * from this structure, or stored in db as an extent property?
     */
    enum address_type   addr_type;  /**< way to address this media */

    /* @TODO: remove fs_type fields once raid1 is refactored */
    enum fs_type        fs_type;    /**< type of filesystem on this media */
};

/**
 * Phobos extent location descriptor.
 */
struct pho_ext_loc {
    char            *root_path;
    struct extent   *extent;
};

static inline bool is_ext_addr_set(const struct pho_ext_loc *loc)
{
    return loc->extent->address.buff != NULL;
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
    PHO_DEV_OP_ST_LAST,
    PHO_DEV_OP_ST_UNSPEC = PHO_DEV_OP_ST_LAST,
};

static const char * const dev_op_st_names[] = {
    [PHO_DEV_OP_ST_FAILED]  = "failed",
    [PHO_DEV_OP_ST_EMPTY]   = "empty",
    [PHO_DEV_OP_ST_LOADED]  = "loaded",
    [PHO_DEV_OP_ST_MOUNTED] = "mounted",
};

static inline const char *op_status2str(enum dev_op_status op_st)
{
    if (op_st >= PHO_DEV_OP_ST_LAST || op_st < 0)
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
    enum dev_adm_status  adm_status;
    struct pho_lock      lock;

};

/**
 * Media statistics.
 *
 * Since they are serialized in JSON, the type used here is the type backing
 * json_int_t.
 */
struct media_stats {
    long long   nb_obj;         /**< number of objects stored on media */
    ssize_t     logc_spc_used;  /**< space used (logical)  */
    ssize_t     phys_spc_used;  /**< space used (physical) */
    ssize_t     phys_spc_free;  /**< free space (physical) */
    long        nb_load;        /**< # the tape was loaded into a drive */
    long        nb_errors;      /**< # errors encountered while accessing it */
    time_t      last_load;      /**< last time it was loaded into a drive */
};

/**
 * Description of filesystem contained on a media.
 */
struct media_fs {
    enum fs_type    type;
    enum fs_status  status;
    char            label[PHO_LABEL_MAX_LEN + 1];
};

/**
 * Media Administrative state
 */
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

static inline enum media_adm_status str2media_adm_status(const char *str)
{
    int i;

    for (i = 0; i < PHO_MDA_ADM_ST_LAST; i++)
        if (!strcmp(str, media_adm_st_names[i]))
            return i;
    return PHO_MDA_ADM_ST_INVAL;
}

/**
 * A simple array of tags (strings)
 */
struct tags {
    char   **tags;     /**< The array of tags */
    size_t   n_tags;   /**< Number of tags */
};

/**
 * Persistent media and filesystem information
 */
struct media_info {
    struct media_id          id;            /**< Public media identifier */
    enum address_type        addr_type;     /**< Way to address this media */
    char                    *model;         /**< Media model (if applicable) */
    enum media_adm_status    adm_status;    /**< Administrative status */
    struct media_fs          fs;            /**< Local filesystem information */
    struct media_stats       stats;         /**< Usage metrics */
    struct tags              tags;          /**< Tags used for filtering */
    struct pho_lock          lock;          /**< Distributed access lock */
};

struct object_info {
    char  *oid;
    char  *user_md;
};

#endif
