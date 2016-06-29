/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 * Copyright 2014-2015 CEA/DAM. All Rights Reserved.
 */
/**
 * \brief  Handling of internal types.
 */
#ifndef _PHO_TYPE_UTILS_H
#define _PHO_TYPE_UTILS_H

#include "pho_types.h"
#include <jansson.h>
#include <glib.h>

/** dump basic storage information as JSON to be attached to data objects. */
int storage_info_to_json(const struct layout_info *layout,
                         GString *str, int json_flags);

/** convert a piece of layout to an extent tag
 * @param tag this buffer must be at least PHO_LAYOUT_TAG_MAX
 */
int layout2tag(const struct layout_info *layout,
               unsigned int layout_idx, char *tag);

/** duplicate a dev_info structure */
struct dev_info *dev_info_dup(const struct dev_info *dev);

/** free a dev_info structure */
void dev_info_free(struct dev_info *dev);

/** duplicate a media_info structure */
struct media_info *media_info_dup(const struct media_info *mda);

/** free a media_info structure */
void media_info_free(struct media_info *mda);


/**
 * Simple on-the-fly JSON parsing engine providing a SAX-like API.
 */
struct saj_parser;


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


struct saj_parser {
    GQueue                              *sp_keys;
    const struct saj_parser_operations  *sp_ops;
    void                                *sp_private;
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
