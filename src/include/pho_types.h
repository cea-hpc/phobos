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
 * \brief  Phobos Common types.
 */
#ifndef _PHO_TYPES_H
#define _PHO_TYPES_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

/* glib will be used to handle common structures (GList, GString, ...) */
#include <assert.h>
#include <errno.h>
#include <glib.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>

#include "pho_attrs.h"

/** max length of a tape label, FS label... */
#define PHO_LABEL_MAX_LEN 32

/** max length of media URI */
#define PHO_URI_MAX (NAME_MAX + 1)

/**
 * upper power of 2 of the length of a timeval representation in PSQL database
 * which is "YYYY-mm-dd HH:MM:SS.uuuuuu"
 */
#define PHO_TIMEVAL_MAX_LEN 32

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
    char                *uuid;          /**< UUID of referenced object */
    int                  version;       /**< Object version */
    struct module_desc   layout_desc;   /**< Layout module used to write it */
    size_t               wr_size;       /**< Encoding write size */
    struct extent       *extents;       /**< List of data extents */
    int                  ext_count;     /**< Number of extents in the list */
    char                *copy_name;     /**< Name of the copy */
};

/**
 * Library type.
 */
enum lib_type {
    PHO_LIB_INVAL = -1,
    PHO_LIB_DUMMY = 0, /**< fake library, all media are always online. */
    PHO_LIB_SCSI  = 1, /**< SCSI library */
    PHO_LIB_RADOS = 2, /**< RADOS library adapter to connect to Ceph cluster */
    PHO_LIB_LAST
};

static const char * const lib_type_names[] = {
    [PHO_LIB_DUMMY] = "DUMMY",
    [PHO_LIB_SCSI]  = "SCSI",
    [PHO_LIB_RADOS] = "RADOS",
};

static inline const char *lib_type2str(enum lib_type type)
{
    if (type >= PHO_LIB_LAST || type < 0)
        return NULL;
    return lib_type_names[type];
}

static inline enum lib_type str2lib_type(const char *str)
{
    int i;

    for (i = 0; i < PHO_LIB_LAST; i++)
        if (!strcmp(str, lib_type_names[i]))
            return i;
    return PHO_LIB_INVAL;
}

enum fs_type {
    PHO_FS_INVAL = -1,
    PHO_FS_POSIX = 0, /* any POSIX filesystem (no specific feature) */
    PHO_FS_LTFS  = 1,
    PHO_FS_RADOS = 2,
    PHO_FS_LAST,
};

static const char * const fs_type_names[] = {
    [PHO_FS_POSIX] = "POSIX",
    [PHO_FS_LTFS]  = "LTFS",
    [PHO_FS_RADOS] = "RADOS",
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
    PHO_FS_STATUS_IMPORTING, /**< media is being imported */
    PHO_FS_STATUS_LAST,
};

static const char * const fs_status_names[] = {
    [PHO_FS_STATUS_BLANK]       = "blank",
    [PHO_FS_STATUS_EMPTY]       = "empty",
    [PHO_FS_STATUS_USED]        = "used",
    [PHO_FS_STATUS_FULL]        = "full",
    [PHO_FS_STATUS_IMPORTING]   = "importing",
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
    size_t  size;
    char   *buff;
};

void pho_buff_alloc(struct pho_buff *buffer, size_t size);
void pho_buff_realloc(struct pho_buff *buffer, size_t size);

void pho_buff_free(struct pho_buff *buffer);

static const struct pho_buff PHO_BUFF_NULL = { .size = 0, .buff = NULL };

struct pho_lock {
    char          *hostname;
    int            owner;
    struct timeval timestamp;
    struct timeval last_locate;
    bool           is_early;
};

/**
 * Family of resource.
 * Families can be seen here as storage technologies.
 */
enum rsc_family {
    PHO_RSC_NONE        = -2,
    PHO_RSC_INVAL       = -1,
    PHO_RSC_TAPE        =  0, /**< Tape, drive tape or tape library */
    PHO_RSC_DIR         =  1, /**< Directory */
    PHO_RSC_RADOS_POOL  =  2, /**< Ceph RADOS pools*/
    PHO_RSC_LAST,
    PHO_RSC_UNSPEC      = PHO_RSC_LAST,
};

static const char * const rsc_family_names[] = {
    [PHO_RSC_TAPE] = "tape",
    [PHO_RSC_DIR]  = "dir",
    [PHO_RSC_RADOS_POOL] = "rados_pool",
};

static inline const char *rsc_family2str(enum rsc_family family)
{
    if (family >= PHO_RSC_LAST || family < 0)
        return NULL;
    return rsc_family_names[family];
}

static inline enum rsc_family str2rsc_family(const char *str)
{
    int i;

    for (i = 0; i < PHO_RSC_LAST; i++)
        if (!strcmp(str, rsc_family_names[i]))
            return i;
    return PHO_RSC_INVAL;
}

/** Identifier */
struct pho_id {
    enum rsc_family family;             /**< Resource family. */
    /* XXX type -> id type may not be straightforward as given media type
     * could be addressed in multiple ways (FS label, FS UUID, device WWID...).
     * So, an id_type enum may be required here in a later version.
     */
    char            name[PHO_URI_MAX];  /**< Resource name. */
    char            library[PHO_URI_MAX]; /**< Library owning the resource */
};

#define FMT_PHO_ID "(%s:%s:%s)"
#define PHO_ID(id) rsc_family2str((id).family), (id).library, (id).name

static inline void pho_id_name_set(struct pho_id *id, const char *name,
                                   const char *library)
{
    size_t len_library = strlen(library);
    size_t len_name = strlen(name);

    assert(len_name < PHO_URI_MAX);
    memcpy(id->name, name, len_name);
    id->name[len_name] = '\0';

    assert(len_library < PHO_URI_MAX);
    memcpy(id->library, library, len_library);
    id->library[len_library] = '\0';
}

struct pho_id *pho_id_dup(const struct pho_id *src);

static inline void pho_id_copy(struct pho_id *dst, const struct pho_id *src)
{
    dst->family = src->family;
    pho_id_name_set(dst, src->name, src->library);
}

/** Resource administrative state */
enum rsc_adm_status {
    PHO_RSC_ADM_ST_INVAL    = -1,
    PHO_RSC_ADM_ST_LOCKED   =  0,
    PHO_RSC_ADM_ST_UNLOCKED =  1,
    PHO_RSC_ADM_ST_FAILED   =  2,
    PHO_RSC_ADM_ST_LAST
};

static const char * const RSC_ADM_STATUS_NAMES[] = {
    [PHO_RSC_ADM_ST_LOCKED]   = "locked",
    [PHO_RSC_ADM_ST_UNLOCKED] = "unlocked",
    [PHO_RSC_ADM_ST_FAILED]   = "failed"
};

static inline const char *rsc_adm_status2str(enum rsc_adm_status adm_st)
{
    if (adm_st >= PHO_RSC_ADM_ST_LAST || adm_st < 0)
        return NULL;
    return RSC_ADM_STATUS_NAMES[adm_st];
}

static inline enum rsc_adm_status str2rsc_adm_status(const char *str)
{
    int i;

    for (i = 0; i < PHO_RSC_ADM_ST_LAST; i++)
        if (!strcmp(str, RSC_ADM_STATUS_NAMES[i]))
            return i;
    return PHO_RSC_ADM_ST_INVAL;
}

/** Resource */
struct pho_resource {
    struct pho_id       id;              /**< Resource identifier. */
    char               *model;           /**< Resource model (if applicable). */

    /* We set this enum to atomic because there can be a data race between a
     * scheduler thread and a device thread using this structure.
     */
    _Atomic enum rsc_adm_status adm_status;
                                        /**< Administrative status */
};

/** describe a piece of data in a layout */
#define MD5_BYTE_LENGTH 16
#define PHO_HASH_MD5_KEY_NAME    "md5"
#define XXH128_BYTE_LENGTH 16
#define PHO_HASH_XXH128_KEY_NAME "xxh128"

struct extent {
    char               *uuid;       /**< extent UUID */
    int                 layout_idx; /**< index of this extent in layout */
    enum extent_state   state;      /**< stability state */
    ssize_t             size;       /**< size of the extent */
    struct pho_id       media;      /**< identifier of the media */
    struct pho_buff     address;    /**< address on the media */
    ssize_t             offset;     /**< offset of the extent */
    bool                with_xxh128;
                                    /**< true if extent xxh128 field is set */
    unsigned char       xxh128[XXH128_BYTE_LENGTH];
                                    /**< canonical XXH128 checksum digest */
    bool                with_md5;   /**< true if extent md5 field is set */
    unsigned char       md5[MD5_BYTE_LENGTH];
                                    /**< MD5 checksum */
    /** Extra attributes specific to the layout which wrote the extent */
    struct pho_attrs    info;
    struct timeval      creation_time;
                                    /**< extent creation time */
};

/**
 * Phobos extent location descriptor.
 */
struct pho_ext_loc {
    char                *root_path;
    struct extent       *extent;
    enum address_type   addr_type;  /**< way to address this location */
};

static inline bool is_ext_addr_set(const struct pho_ext_loc *loc)
{
    return loc->extent->address.buff != NULL;
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
    [PHO_DEV_OP_ST_UNSPEC]  = "unspecified",
};

static inline const char *op_status2str(enum dev_op_status op_st)
{
    if (op_st > PHO_DEV_OP_ST_LAST || op_st < 0)
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
    struct pho_resource  rsc;
    /* Device types and their compatibility rules are configurable
     * So, use string instead of enum. */
    char                *path;
    char                *host;
    struct pho_lock      lock;
    size_t               health;    /**< Current health of the device */
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
 * A simple array of strings
 */
struct string_array {
    char   **strings;   /**< The array of strings */
    size_t   count;     /**< Number of strings */
};

/**
 * Media operation flags
 */
struct operation_flags {
    bool    put;
    bool    get;
    bool    delete;
};

/**
 * Persistent media and filesystem information
 */
struct media_info {
    struct pho_resource    rsc;          /**< Resource information */
    enum address_type      addr_type;    /**< Way to address this media */
    struct media_fs        fs;           /**< Local filesystem information */
    struct media_stats     stats;        /**< Usage metrics */
    struct string_array    tags;         /**< Tags used for filtering */
    struct pho_lock        lock;         /**< Distributed access lock */
    struct operation_flags flags;        /**< Media operation flags */
    size_t                 health;       /**< Current health of the medium */
    struct string_array    groupings;    /**< Groupings to keep some objects
                                           *  together on this medium
                                           */
};

enum copy_status {
    PHO_COPY_STATUS_INVAL = -1,
    PHO_COPY_STATUS_INCOMPLETE = 0,  /**< Copy has not enough splits */
    PHO_COPY_STATUS_READABLE,        /**< Enough splits to reconstruct a copy */
    PHO_COPY_STATUS_COMPLETE,        /**< All copies */
    PHO_COPY_STATUS_LAST
};

static const char * const COPY_STATUS_NAMES[] = {
    [PHO_COPY_STATUS_INCOMPLETE] = "incomplete",
    [PHO_COPY_STATUS_READABLE]   = "readable",
    [PHO_COPY_STATUS_COMPLETE]   = "complete"
};

static inline const char *copy_status2str(enum copy_status status)
{
    if (status >= PHO_COPY_STATUS_LAST || status < 0)
        return NULL;
    return COPY_STATUS_NAMES[status];
}

static inline enum copy_status str2copy_status(const char *str)
{
    int i;

    for (i = 0; i < PHO_COPY_STATUS_LAST; i++)
        if (!strcmp(str, COPY_STATUS_NAMES[i]))
            return i;
    return PHO_COPY_STATUS_INVAL;
}

struct copy_info {
    char *object_uuid;
    int version;
    const char *copy_name;
    enum copy_status copy_status;
    struct timeval creation_time;
    struct timeval access_time;
};

struct object_info {
    char *oid;
    char *uuid;
    int version;
    char *user_md;
    struct timeval creation_time;
    struct timeval deprec_time;
    const char *grouping;
    ssize_t size;
};

/**
 *
 */
enum read_target_allocation_op {
    PHO_READ_TARGET_ALLOC_OP_INVAL = -1,
    PHO_READ_TARGET_ALLOC_OP_READ,
    PHO_READ_TARGET_ALLOC_OP_DELETE,
    PHO_READ_TARGET_ALLOC_OP_LAST,
};

/**
 * Kind of notify operations
 */
enum notify_op {
    PHO_NTFY_OP_INVAL = -1,
    PHO_NTFY_OP_DEVICE_ADD,
    PHO_NTFY_OP_DEVICE_LOCK,
    PHO_NTFY_OP_DEVICE_UNLOCK,
    PHO_NTFY_OP_MEDIUM_UPDATE,
    PHO_NTFY_OP_LAST
};

enum configure_op {
    PHO_CONF_OP_INVAL = -1,
    PHO_CONF_OP_SET,
    PHO_CONF_OP_GET,
    PHO_CONF_OP_LAST
};

static inline bool pho_configure_op_is_valid(enum configure_op op)
{
    return op > PHO_CONF_OP_INVAL && op < PHO_CONF_OP_LAST;
}

/**
 * Threadsafe FIFO queue, composed of a GLIB queue and a mutex.
 *
 * Functions that interact with this structure are available in
 * pho_type_utils.h.
 *
 */
struct tsqueue {
    GQueue             *queue;          /**< Object queue */
    pthread_mutex_t     mutex;          /**< Mutex to protect the queue */
};

#endif
