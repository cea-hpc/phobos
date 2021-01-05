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
 * \brief  Handling of internal types.
 */
#ifndef _PHO_TYPE_UTILS_H
#define _PHO_TYPE_UTILS_H

#include "pho_types.h"
#include <jansson.h>
#include <glib.h>
#include <stdbool.h>

static const struct tags NO_TAGS = {0};

/** dump basic storage information as JSON to be attached to data objects. */
int storage_info_to_json(const struct layout_info *layout,
                         GString *str, int json_flags);

/** copy a pho_id structure */
void pho_id_copy(struct pho_id *id_dest, const struct pho_id *id_src);

/** check if two pho_id are equal */
bool pho_id_equal(const struct pho_id *id1, const struct pho_id *id2);

/** duplicate a dev_info structure */
struct dev_info *dev_info_dup(const struct dev_info *dev);

/** copy a dev_info structure in an already allocated one */
void dev_info_cpy(struct dev_info *dev_dst, const struct dev_info *dev_src);

/**
 * Free a dev_info structure
 *
 * If free_top_struct is false, only releasing the structure contents,
 * the caller is responsible for the top structure.
 */
void dev_info_free(struct dev_info *dev, bool free_top_struct);

/** duplicate a media_info structure */
struct media_info *media_info_dup(const struct media_info *mda);

/** free a media_info structure */
void media_info_free(struct media_info *mda);

/**
 * Init tags by strdup'ing tag_values. Return 0 or -ENOMEM.
 */
int tags_init(struct tags *tags, char **tag_values, size_t n_tags);

/**
 * Free a tags structure where the tag list and tag strings have been malloc'd
 */
void tags_free(struct tags *tags);

/** duplicate a tags structure. Return 0 or -ENOMEM. */
int tags_dup(struct tags *tags_dst, const struct tags *tags_src);

/**
 * Return true if the two tags are equal, false otherwise. The order of tags
 * matters.
 */
bool tags_eq(const struct tags *tags1, const struct tags *tags2);

/**
 * Return true if all tags in needle are in haystack, false otherwise. Always
 * return true if needle is empty.
 */
bool tags_in(const struct tags *haystack, const struct tags *needle);

/**
 * Convert the string of the form "tag1,tag2" into separate tags (tag1 and tag2)
 * and add it to an existing @tags structure
 *
 * @param[in]   tag_str the string to extract the tags from
 * @param[in,out]  tags    the tags struct to fill
 *
 * @return 0 on success, -errno on error.
 */
int str2tags(const char *tag_str, struct tags *tags);

/**
 * Simple on-the-fly JSON parsing engine providing a SAX-like API.
 */
struct saj_parser;

/**
 * @defgroup pho_layout_info Public API for layout information
 * @{
 */

/**
 * Free the extents in this layout.
 */
void layout_info_free_extents(struct layout_info *layout);

/** @} end of pho_layout_mod group */


/**
 * Users to provide desired handlers for the following operations.
 * All of them are optional (although not providing any would not make sense).
 */
struct saj_parser_operations {
    /**
     * Called upon the start of a new object.
     * @param[in]  parser  parsing context.
     * @param[in]  key     object name.
     * @param[in]  value   object value (typecheck, iterable will be iterated).
     * @param[in,out] priv private pointer from saj_parser_init().
     * @return 0 on success. Non-zero codes will stop iteration and be
     *         propagated.
     */
    int (*so_object_begin)(struct saj_parser *, const char *, json_t *, void *);

    /**
     * Called upon the end of an object.
     * @param[in]  parser  parsing context.
     * @param[in,out] priv private pointer from saj_parser_init().
     * @return 0 on success. Non-zero codes will stop iteration and be
     *         propagated.
     */
    int (*so_object_end)(struct saj_parser *, void *);

    /**
     * Called upon the start of an array.
     * @param[in]  parser  parsing context.
     * @param[in,out] priv private pointer from saj_parser_init().
     * @return 0 on success. Non-zero codes will stop iteration and be
     *         propagated.
     */
    int (*so_array_begin)(struct saj_parser *, void *);

    /**
     * Called on each element of an array.
     * @param[in]  parser  parsing context.
     * @param[in]  index   element index in the array.
     * @param[in]  value   object value (typecheck, iterable will be iterated).
     * @param[in,out] priv private pointer from saj_parser_init().
     * @return 0 on success. Non-zero codes will stop iteration and be
     *         propagated.
     */
    int (*so_array_elt)(struct saj_parser *, int, json_t *, void *);

    /**
     * Called upon the end of an array.
     * @param[in]  parser  parsing context.
     * @param[in,out] priv private pointer from saj_parser_init().
     * @return 0 on success. Non-zero codes will stop iteration and be
     *         propagated.
     */
    int (*so_array_end)(struct saj_parser *, void *);
};


/**
 * SAJ parser internal states.
 * This structure should not be accessed by external functions and is only
 * defined here so as to be useable conveniently w/o requiring a dynamic alloc
 * by the SAJ initialization code.
 *
 * Callers can pass custom data to the callbacks via the `priv' pointer.
 */
struct saj_parser {
    GQueue                              *sp_keys;       /**< Internal stack */
    const struct saj_parser_operations  *sp_ops;        /**< User callbacks */
    void                                *sp_private;    /**< User priv data */
};

/**
 * Initialize a new SAJ parser.
 * @param[out]  parser  The parser representation to initialize.
 * @param[in]   ops     The parser operations to use when iterating over a JSON.
 * @param[in]   priv    An opaque pointer passed down the provided callbacks.
 * @return 0 on success, negated errno on failure.
 */
int saj_parser_init(struct saj_parser *parser,
                    const struct saj_parser_operations *ops, void *priv);

/**
 * Release resources associated to a SAJ parser object.
 * @param[in,out]  parser  The parser object to free.
 * @return 0 on success, negated errno on failure.
 */
int saj_parser_free(struct saj_parser *parser);

/**
 * Retrieve the current object key.
 * @param[in]  parser  The parser representation.
 * @return NULL if we are out of any context or a zero-terminated string.
 */
const char *saj_parser_key(const struct saj_parser *parser);

/**
 * Iterate over a JSON object and call the appropriate handlers.
 * @param[in,out]  parser  An initialized SAJ parser object.
 * @param[in]      root    The JSON object to iterate over.
 * @return 0 on success, non-null error code on failure.
 */
int saj_parser_run(struct saj_parser *parser, json_t *root);

#endif
