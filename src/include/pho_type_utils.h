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
 * \brief  Handling of internal types.
 */
#ifndef _PHO_TYPE_UTILS_H
#define _PHO_TYPE_UTILS_H

#include "pho_srl_common.h"
#include "pho_types.h"
#include <jansson.h>
#include <glib.h>
#include <stdbool.h>

static const struct string_array NO_STRING = {0};

/** dump basic storage information as JSON to be attached to data objects. */
int storage_info_to_json(const struct layout_info *layout,
                         GString *str, int json_flags);

/** check if two pho_id are equal */
bool pho_id_equal(const struct pho_id *id1, const struct pho_id *id2);

/** hash a pho_id key for a g_hash_table */
guint g_pho_id_hash(gconstpointer p_pho_id);

/** identity test for g_hash_table */
gboolean g_pho_id_equal(gconstpointer p_pho_id_1, gconstpointer p_pho_id_2);


/** initialize a pho_lock structure */
void init_pho_lock(struct pho_lock *lock, char *hostname, int owner,
                   struct timeval *lock_timestamp, struct timeval *last_locate,
                   bool is_early);

/** copy a pho_lock structure */
void pho_lock_cpy(struct pho_lock *lock_dst, const struct pho_lock *lock_src);

/** free a pho_lock structure's contents */
void pho_lock_clean(struct pho_lock *lock);

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

/**
 * Free the internals of struct media_info. Do not free \p medium.
 * To free \p medium as well, use media_info_free.
 */
void media_info_cleanup(struct media_info *medium);

/** copy the a media_info structure in an already allocated one  */
void media_info_copy(struct media_info *dst, const struct media_info *src);

/** duplicate a media_info structure, cannot return NULL */
struct media_info *media_info_dup(const struct media_info *mda);

/** free a media_info structure */
void media_info_free(struct media_info *mda);

/** duplicate an object_info structure, cannot return NULL */
struct object_info *object_info_dup(const struct object_info *obj);

/** free an object_info structure */
void object_info_free(struct object_info *obj);

/** duplicate an copy_info structure, cannot return NULL */
struct copy_info *copy_info_dup(const struct copy_info *copy);

/** free an copy_info structure */
void copy_info_free(struct copy_info *copy);

/**
 * Init string_array by strdup'ing strings.
 */
void string_array_init(struct string_array *string_array, char **strings,
                       size_t count);

/**
 * Free a string_array structure where the string list and its strings have
 * been malloc'd
 */
void string_array_free(struct string_array *string_array);

/** duplicate a string_array. Return 0 or -ENOMEM. */
void string_array_dup(struct string_array *string_array_dst,
                      const struct string_array *string_array_src);

/**
 * Return true if the two string_array are equal, false otherwise. The order of
 * strings into string_array matters.
 */
bool string_array_eq(const struct string_array *string_array1,
                     const struct string_array *string_array2);

/**
 * Return true if all strings in needle are in haystack, false otherwise. Always
 * return true if needle is empty.
 */
bool string_array_in(const struct string_array *haystack,
                     const struct string_array *needle);

/**
 * Add \p string to the end of string_array.
 */
void string_array_add(struct string_array *string_array, const char *string);

/**
 * Return true if the given string is in the string_array, false otherwise.
 */
bool string_exists(const struct string_array *string_array, const char *string);

/**
 * Convert the string of the form "string1,string2" into separate strings
 * (string1 and string2) and add it to an existing string_array
 *
 * @param[in]       str             line containing strings
 * @param[in,out]   string_array    the array of string to fill
 */
void str2string_array(const char *str, struct string_array *string_array);

/**
 * Convert a string of the form "YYYY-mm-dd HH:MM:SS.uuuuuu" into a
 * timeval structure
 *
 * @param[in]     tv_str the string to extract the time from
 * @param[in,out] tv     the extracted time, must be pre-allocated
 * @return               0 on success, -EINVAL on error
 */
int str2timeval(const char *tv_str, struct timeval *tv);

/**
 * Convert a timeval structure into a string of the form
 * "YYYY-mm-dd HH:MM:SS.uuuuuu"
 *
 * @param[in]   tv      the time to convert
 * @param[out]  tv_str  the formatted string
 */
void timeval2str(const struct timeval *tv, char *tv_str);

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
    void                                *sp_handle;     /**< User handle */
};

/**
 * Initialize a new SAJ parser.
 * @param[out]  parser  The parser representation to initialize.
 * @param[in]   ops     The parser operations to use when iterating over a JSON.
 * @param[in]   priv    An opaque pointer passed down the provided callbacks.
 * @param[in]   handle  A pointer to the DSS handle passed down the callbacks.
 * @return 0 on success, negated errno on failure.
 */
int saj_parser_init(struct saj_parser *parser,
                    const struct saj_parser_operations *ops,
                    void *priv, void *handle);

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

/**
 * Threadsafe (fifo) queue initializer.
 * @param[in,out]   tsq     Threadsafe queue.
 *
 * @return 0 on success, non-null error code on failure.
 */
int tsqueue_init(struct tsqueue *tsqueue);

/**
 * Threadsafe queue destructor.
 * @param[in,out]   tsq         Threadsafe queue.
 * @param[in]       free_func   Function used to free queue elements contents.
 */
void tsqueue_destroy(struct tsqueue *tsq, GDestroyNotify free_func);

/**
 * Pop element from threadsafe queue.
 * @param[in,out]   tsq     Threadsafe queue.
 *
 * @return          Element popped.
 */
void *tsqueue_pop(struct tsqueue *tsq);

/**
 * Push element in threadsafe queue.
 * @param[in,out]   tsq         Threadsafe queue.
 * @param[in]       data        Element pushed.
 */
void tsqueue_push(struct tsqueue *tsq, void *data);

/**
 * Get length of a threadsafe queue
 * @param[in]   tsq     Threadsafe queue
 *
 * @return  length of the threadsafe queue
 */
unsigned int tsqueue_get_length(struct tsqueue *tsq);

#endif
