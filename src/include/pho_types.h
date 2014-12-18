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

/** max length of a tape label, FS label... */
#define PHOBOS_LABEL_MAX_LEN 32
/** max length of a hash-based address in hexadecimal form.
 * A SHA-1 value is always 160 bit long, hexa representation uses 4 bit per
 * character so the string length is 160/4 + null character.
 */
#define HASH_STR_MAX 41

enum media_type {
    MT_TAPE = 1,
    /* future: disk, file... */
};

/** media identifier */
struct media_id {
    enum media_type type;
    /** XXX type -> id type may not be straightforward as given media type
     * could be addressed in multiple ways (FS label, FS UUID, device WWID...).
     * So, an id_type enum may be required here in a later version. */
    union {
        char label[PHOBOS_LABEL_MAX_LEN];
    } id_u;
};

enum layout_type {
    LT_SIMPLE = 1, /* simple contiguous block of data */
    /* future: SPLIT, RAID0, RAID1, ... */
};

/** describe data layout */
struct layout_descr {
    enum layout_type    type;
    /* v00: no other information needed for simple layout */
};

/** describe a piece of data in a layout */
struct extent {
    unsigned int       layout_idx; /* always 0 for simple layouts */
    struct media_id    media;
    enum address_type  addr_type; /**< type of address to access this media */
    union {
        char path[PATH_MAX];   /**< for POSIX backends */
        char hash[HASH_STR_MAX]; /**< for hash-addressed backends */
    } address_u;
    uint64_t size;
};

/** Address to read/write data.
 * Also holds resource information to be cleaned after the access.
 */
struct data_loc {
    /** mount-point to access the media referenced in the extent */
    char          root_path[PATH_MAX];
    /** the data extent itself */
    struct extent extent;
};

#endif
