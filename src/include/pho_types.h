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
/* for PATH_MAX */
#include <limits.h>
#include <stdint.h>
#include <stdbool.h>

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

#endif
