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
 * Build a clean path given the extent key and description. Both are mandatory
 * and must be non-empty.
 *
 * The function outputs the path in dst_path and returns 0 on success
 * and a negative error code on failure. Expect nothing in dst_path on
 * failure.
 */
int pho_mapper_clean_path(const char *ext_key, const char *ext_desc,
                          char *dst_path, size_t dst_size)
{
    size_t avail_size;
    size_t key_len;
    int rc;

    if (ext_desc == NULL || ext_key == NULL || dst_path == NULL)
        return -EINVAL;

    key_len = strlen(ext_key);
    if (key_len == 0)
        return -EINVAL;

    /* dst_path must at least store <delim> + key + '\0' */
    if (dst_size < key_len + 2)
        return -EINVAL;

    /* Keep space for delimiter + key, if present */
    avail_size = dst_size - key_len + 1;
    strncpy(dst_path, ext_desc, avail_size);

    /* min of <just written bytes> and <avail_size - 1> */
    rc = min(strnlen(ext_desc, avail_size), avail_size - 1);

    clean_path(dst_path, avail_size);

    dst_path += rc;
    dst_size -= rc;

    snprintf(dst_path, dst_size, ".%s", ext_key);

    return 0;
}

/**
 * Craft path given the extent key and description. Both are mandatory and must
 * be non-empty.
 *
 * The function outputs the path in dst_path and returns 0 on success
 * and a negative error code on failure. Expect nothing in dst_path on
 * failure.
 */
int pho_mapper_hash1(const char *ext_key, const char *ext_desc, char *dst_path,
                     size_t dst_size)
{
    unsigned char   hash[SHA_DIGEST_LENGTH];
    size_t          key_len;
    SHA_CTX         ctx;
    int             rc;

    if (ext_desc == NULL || ext_key == NULL || dst_path == NULL)
        return -EINVAL;

    if (strlen(ext_desc) == 0)
        return -EINVAL;

    key_len = strlen(ext_key);
    if (key_len == 0)
        return -EINVAL;

    /* dst_path must at least store the hash part + <delim> + tag + '\0' */
    if (dst_size < PHO_MAPPER_PREFIX_LENGTH + key_len + 2)
        return -EINVAL;

    SHA1_Init(&ctx);
    SHA1_Update(&ctx, ext_key, key_len);
    SHA1_Final(hash, &ctx);

    rc = snprintf(dst_path, dst_size, "%02x/%02x/", hash[0], hash[1]);

    /* the end of the path is the same as "clean_path" mapping. */
    return pho_mapper_clean_path(ext_key, ext_desc, dst_path + rc,
                                 dst_size - rc);
}
