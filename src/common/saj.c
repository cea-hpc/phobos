/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 * Copyright 2016 CEA/DAM. All Rights Reserved.
 */
/**
 * \brief  Simple API for JSON with a low memory footprint
 *         and convenient callbacks just like in the good ol' days.
 *
 *         Callers can provide hooks for entering/leaving/processing states via
 *         a struct saj_parser_operations. Any non-zero return code from a
 *         callback interrupts the processing and is returned back to the top
 *         function.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "pho_dss.h"
#include "pho_type_utils.h"

#include <ctype.h>
#include <stdarg.h>
#include <errno.h>
#include <assert.h>


/**
 * Invoke the appropriate user callback (if any) to signal the beginning of an
 * object.
 */
static int sp_object_begin(struct saj_parser *parser, const char *key,
                                  json_t *value)
{
    assert(parser);
    assert(parser->sp_ops);

    if (!parser->sp_ops->so_object_begin)
        return 0;

    return parser->sp_ops->so_object_begin(parser, key, value,
                                           parser->sp_private);
}

/**
 * Invoke the appropriate user callback (if any) to signal the end of an object.
 */
static int sp_object_end(struct saj_parser *parser)
{
    assert(parser);
    assert(parser->sp_ops);

    if (!parser->sp_ops->so_object_end)
        return 0;

    return parser->sp_ops->so_object_end(parser, parser->sp_private);
}

/**
 * Invoke the appropriate user callback (if any) to signal the beginning of an
 * array.
 */
static int sp_array_begin(struct saj_parser *parser)
{
    assert(parser);
    assert(parser->sp_ops);

    if (!parser->sp_ops->so_array_begin)
        return 0;

    return parser->sp_ops->so_array_begin(parser, parser->sp_private);
}

/**
 * Invoke the appropriate user callback (if any) to process an array element.
 */
static int sp_array_elt(struct saj_parser *parser, int idx, json_t *val)
{
    assert(parser);
    assert(parser->sp_ops);

    if (!parser->sp_ops->so_array_elt)
        return 0;

    return parser->sp_ops->so_array_elt(parser, idx, val, parser->sp_private);
}

/**
 * Invoke the appropriate user callback (if any) to signal the end of an array.
 */
static int sp_array_end(struct saj_parser *parser)
{
    assert(parser);
    assert(parser->sp_ops);

    if (!parser->sp_ops->so_array_end)
        return 0;

    return parser->sp_ops->so_array_end(parser, parser->sp_private);
}


static int parser_json_next(struct saj_parser *, const char *, json_t *);

/**
 * Process an array of arbitrary elements.
 * Invoke begin hook, process each item and invoke the end hook.
 */
static int parser_json_array_handle(struct saj_parser *parser, const char *key,
                                    json_t *array)
{
    json_t  *value;
    size_t   index;
    int      rc = 0;

    if (!json_is_array(array))
        return -EINVAL;

    rc = sp_array_begin(parser);
    if (rc)
        return rc;

    json_array_foreach(array, index, value) {
        rc = sp_array_elt(parser, index, value);
        if (rc)
            return rc;

        rc = parser_json_next(parser, key, value);
        if (rc)
            return rc;
    }

    rc = sp_array_end(parser);
    if (rc)
        return rc;

    return 0;
}

/**
 * Process a JSON object (multiple key/value mapping).
 * Invoke begin hook, process each item and invoke the end hook.
 */
static int parser_json_object_handle(struct saj_parser *parser, const char *key,
                                     json_t *obj)
{
    const char  *subkey;
    json_t      *value;
    int          rc;

    if (!json_is_object(obj))
        return -EINVAL;

    rc = sp_object_begin(parser, key, obj);
    if (rc)
        return rc;

    json_object_foreach(obj, subkey, value) {
        rc = parser_json_next(parser, subkey, value);
        if (rc)
            return rc;
    }

    rc = sp_object_end(parser);
    if (rc)
        return rc;

    return 0;
}

/**
 * Iterate once over the object being parsed.
 * Call the dedicated handlers for recursive types (object and array) or the
 * user begin/end callbacks for simple ones (string, number).
 */
int parser_json_next(struct saj_parser *parser, const char *key, json_t *next)
{
    int rc;

    /* Objects whose name start w/ '$' are special context keys that the SAJ
     * parser puts on a stack and exposes to the user callbacks. */
    if (key && key[0] == '$')
        g_queue_push_head(parser->sp_keys, g_strdup(key));

    switch (json_typeof(next)) {
    case JSON_OBJECT:
        rc = parser_json_object_handle(parser, key, next);
        if (rc)
            return rc;
        break;
    case JSON_ARRAY:
        rc = parser_json_array_handle(parser, key, next);
        if (rc)
            return rc;
        break;
    default:
        rc = sp_object_begin(parser, key, next);
        if (rc)
            return rc;
        rc = sp_object_end(parser);
        if (rc)
            return rc;
        break;
    }

    if (key && key[0] == '$')
        g_free(g_queue_pop_head(parser->sp_keys));

    return 0;
}

/**
 * Initialize a new parser with user operations and user private data.
 */
int saj_parser_init(struct saj_parser *parser,
                    const struct saj_parser_operations *ops, void *priv)
{
    if (!parser || !ops)
        return -EINVAL;

    parser->sp_keys    = g_queue_new();
    parser->sp_ops     = ops;
    parser->sp_private = priv;
    return 0;
}

/**
 * Release resources associated to a SAJ parser.
 */
int saj_parser_free(struct saj_parser *parser)
{
    while (!g_queue_is_empty(parser->sp_keys))
        g_free(g_queue_pop_head(parser->sp_keys));

    g_queue_free(parser->sp_keys);
    return 0;
}

/**
 * Return the currently active key (being processed).
 * This is the one on top of the stack or NULL if the stack is empty.
 */
const char *saj_parser_key(const struct saj_parser *parser)
{
    if (g_queue_is_empty(parser->sp_keys))
        return NULL;

    return g_queue_peek_head(parser->sp_keys);
}

/**
 * Unroll processing recursively over a JSON object.
 */
int saj_parser_run(struct saj_parser *parser, json_t *root)
{
    assert(root != NULL);
    return parser_json_next(parser, NULL, root);
}
