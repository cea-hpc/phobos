/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 * Copyright 2014-2015 CEA/DAM. All Rights Reserved.
 */
/**
 * \brief Phobos objects mapping
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "pho_types.h"
#include "pho_common.h"
#include "pho_mapper.h"

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <ctype.h>

#include <openssl/sha.h>

/**
 * Implementation of extent objects mapping.
 * Extents are identified by an ID and a tag, from which a path is inferred.
 * The paths are based on sha1 hashes of the input components, and
 * systematically organized in a two-levels tree.
 *
 * A NULL byte is inserted between object ID and extent tag during the hashing,
 * so that both fields are clearly identified in the process. (a/bc produces a
 * different hash than ab/c).
 *
 * They are of the form:
 * "<sha1 byte0>/<sha1 byte1>/<sha1 prefix>_<cleaned ID>[.<cleaned tag>]"
 *
 * The first two bytes of the sha1 are used to spread the objects evenly within
 * a two-levels tree. This makes 255*255 available leaf directories, which is
 * enough to store 1 million objects with an average of 15 objects per
 * directory. Because of the significantly reduced collision space, only 4 bytes
 * of the SHA1 hash are put at the beginning of each object, for the sake of
 * readable `ls' output.
 *
 * The object ID component are truncated if need be, so that the *WHOLE* path
 * does not exceed NAME_MAX bytes. Annoying characters are replaced by
 * underscores. The extent tag, if present, won't be truncated, and but is
 * required to be smaller than PHO_LAYOUT_TAG_MAX.
 *
 * TODO support evolutions of the algorithm, to be future-proof.
 */


/**
 * Number of bytes needed to represent a stringified SHA1 hash, with NULL-term.
 */
#define SHA_DIGEST_STR_LENGTH   (2 * SHA_DIGEST_LENGTH + 1)

/**
 * Replace undesired characters with underscores.
 */
static void clean_path(char *path, size_t path_len)
{
    int i;

    for (i = 0; path[i] != 0 && i < path_len - 1; i++) {
        if (!pho_mapper_chr_valid(path[i]))
            path[i] = '_';
    }
}

/**
 * Craft path given the object ID and extent tag.
 *
 * The object ID is mandatory (must be non-empty)
 *
 * The extent tag is optional (either NULL or empty). Must be smaller than
 * PHO_LAYOUT_TAG_MAX bytes.
 *
 * The destination buffer must be at least NAME_MAX + 1 bytes
 *
 * The function outputs the path in dst_path and returns 0 on success
 * and a negative error code on failure. Expect nothing in dst_path on
 * failure.
 */
int pho_mapper_extent_resolve(const char *obj_id, const char *ext_tag,
                              char *dst_path, size_t dst_size)
{
    unsigned char   hash[SHA_DIGEST_LENGTH];
    size_t          obj_len;
    size_t          tag_len;
    SHA_CTX         ctx;
    size_t          avail_size;
    int             rc;

    if (obj_id == NULL || dst_path == NULL)
        return -EINVAL;

    if (dst_size < NAME_MAX + 1)
        return -EINVAL;

    obj_len = strlen(obj_id);
    if (obj_len == 0)
        return -EINVAL;

    tag_len = ext_tag ? strlen(ext_tag) : 0;
    if (tag_len > PHO_LAYOUT_TAG_MAX)
        return -ENAMETOOLONG;

    SHA1_Init(&ctx);
    SHA1_Update(&ctx, obj_id, obj_len);
    SHA1_Update(&ctx, "", 1);   /* Hash a null byte to separate ID/Tag */
    SHA1_Update(&ctx, ext_tag, tag_len);
    SHA1_Final(hash, &ctx);

    /* Keep space for delimiter + tag, if present */
    avail_size = dst_size - (tag_len ? tag_len + 1 : 0);
    rc = snprintf(dst_path, avail_size, "%02x/%02x/%02x%02x%02x%02x_%s",
                  hash[0], hash[1], hash[0], hash[1], hash[2], hash[3], obj_id);

    rc = min(rc, avail_size - 1);

    clean_path(dst_path + PHO_MAPPER_PREFIX_LENGTH,
               avail_size - PHO_MAPPER_PREFIX_LENGTH);

    if (tag_len > 0) {
        dst_path += rc;
        dst_size -= rc;
        snprintf(dst_path, dst_size, ".%s", ext_tag);
        clean_path(dst_path + 1, dst_size - 1); /* preserve delimiter */
    }

    return 0;
}
