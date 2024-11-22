/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2024 CEA/DAM.
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
 * \brief  Filter file of Phobos's Distributed State Service.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <gmodule.h>
#include <jansson.h>
#include <libpq-fe.h>
#include <stdio.h>

#include "pho_type_utils.h"

#include "filters.h"

void dss_filter_free(struct dss_filter *filter)
{
    if (filter && filter->df_json)
        json_decref(filter->df_json);
}

int dss_filter_build(struct dss_filter *filter, const char *fmt, ...)
{
    json_error_t    err;
    va_list         args;
    char           *query;
    int             rc;

    if (!filter)
        return -EINVAL;

    memset(filter, 0, sizeof(*filter));

    va_start(args, fmt);
    rc = vasprintf(&query, fmt, args);
    va_end(args);

    if (rc < 0)
        return -ENOMEM;

    filter->df_json = json_loads(query, JSON_REJECT_DUPLICATES, &err);
    if (!filter->df_json) {
        pho_debug("Invalid filter: %s", query);
        LOG_GOTO(out_free, rc = -EINVAL, "Cannot decode filter: %s", err.text);
    }

    rc = 0;

out_free:
    free(query);
    return rc;
}

/**
 * This enum is used to define the type of the string value retrieved from the
 * DSS filter.
 */
enum strvalue_type {
    STRVAL_DEFAULT = 0, /* default string value */
    STRVAL_INDEX   = 1, /* index of an array */
    STRVAL_KEYVAL  = 2  /* key/value pair in an array */
};

/**
 * Insert a given \p string into \p query after escaping it and formatting it to
 * follow the format specified by \p type.
 *
 * \param[in]  handle  A valid handle to the DSS
 * \param[out] query   The query in which the string should be inserted
 * \param[in]  string  The string to escape and insert
 * \param[in]  type    The type of \p string
 */
static void insert_string(struct dss_handle *handle, GString *query,
                          const char *string, enum strvalue_type type)
{
    size_t esc_len = strlen(string) * 2 + 1;
    char *esc_str;
    char *esc_val;

    esc_str = xmalloc(esc_len);
    PQescapeStringConn(handle->dh_conn, esc_str, string, esc_len, NULL);

    switch (type) {
    case STRVAL_INDEX:
        g_string_append_printf(query, "array['%s']", esc_str);
        break;
    case STRVAL_KEYVAL:
        esc_val = strchr(esc_str, '=');
        assert(esc_val);

        *esc_val++ = '\0';
        g_string_append_printf(query, "'{\"%s\": \"%s\"}'", esc_str, esc_val);
        break;
    default:
        g_string_append_printf(query, "'%s'", esc_str);
    }

    free(esc_str);
}

static int json2sql_object_begin(struct saj_parser *parser, const char *key,
                                 json_t *value, void *priv)
{
    struct dss_handle *handle = (struct dss_handle *)parser->sp_handle;
    const char *current_key = saj_parser_key(parser);
    enum strvalue_type type = STRVAL_DEFAULT;
    const char *field_impl;
    GString *str = priv;

    /* out-of-context: nothing to do */
    if (!key)
        return 0;

    /* operators will be stacked as contextual keys: nothing to do */
    if (key[0] == '$')
        return 0;

    /* Not an operator: write the affected field name */
    field_impl = dss_fields_pub2implem(key);
    if (!field_impl)
        LOG_RETURN(-EINVAL, "Unexpected filter field: '%s'", key);
    g_string_append_printf(str, "%s", field_impl);

    /* -- key is an operator: translate it into SQL -- */

    /* If top-level key is a logical operator, we have an implicit '=' */
    if (!current_key || key_is_logical_op(current_key)) {
        g_string_append(str, " = ");
    } else if (!g_ascii_strcasecmp(current_key, "$NE")) {
        g_string_append(str, " != ");
    } else if (!g_ascii_strcasecmp(current_key, "$GT")) {
        g_string_append(str, " > ");
    } else if (!g_ascii_strcasecmp(current_key, "$GTE")) {
        g_string_append(str, " >= ");
    } else if (!g_ascii_strcasecmp(current_key, "$LT")) {
        g_string_append(str, " < ");
    } else if (!g_ascii_strcasecmp(current_key, "$LTE")) {
        g_string_append(str, " <= ");
    } else if (!g_ascii_strcasecmp(current_key, "$LIKE")) {
        g_string_append(str, " LIKE ");
    } else if (!g_ascii_strcasecmp(current_key, "$REGEXP")) {
        g_string_append(str, " ~ ");
    } else if (!g_ascii_strcasecmp(current_key, "$INJSON")) {
        g_string_append(str, " @> ");
        type = STRVAL_INDEX;
    } else if (!g_ascii_strcasecmp(current_key, "$KVINJSON")) {
        g_string_append(str, " @> ");
        type = STRVAL_KEYVAL;
    } else if (!g_ascii_strcasecmp(current_key, "$XJSON")) {
        g_string_append(str, " ? ");
    } else {
        LOG_RETURN(-EINVAL, "Unexpected operator: '%s'", current_key);
    }

    switch (json_typeof(value)) {
    case JSON_STRING:
        insert_string(handle, str, json_string_value(value), type);
        break;
    case JSON_INTEGER:
        g_string_append_printf(str, "%"JSON_INTEGER_FORMAT,
                               json_integer_value(value));
        break;
    case JSON_REAL:
        g_string_append_printf(str, "%lf", json_number_value(value));
        break;
    case JSON_TRUE:
        g_string_append(str, "TRUE");
        break;
    case JSON_FALSE:
        g_string_append(str, "FALSE");
        break;
    case JSON_NULL:
        g_string_append(str, "NULL");
        break;
    default:
        /* Complex type (operands) will be handled by the following iteration */
        break;
    }

    return 0;
}

static int json2sql_array_begin(struct saj_parser *parser, void *priv)
{
    GString     *str = priv;
    const char  *current_key = saj_parser_key(parser);

    /* $NOR expanded as "NOT (... OR ...) */
    if (!g_ascii_strcasecmp(current_key, "$NOR"))
        g_string_append(str, "NOT ");

    g_string_append(str, "(");
    return 0;
}

static int json2sql_array_elt(struct saj_parser *parser, int index, json_t *elt,
                              void *priv)
{
    GString     *str = priv;
    const char  *current_key = saj_parser_key(parser);

    /* Do not insert operator before the very first item... */
    if (index == 0)
        return 0;

    if (!g_ascii_strcasecmp(current_key, "$NOR"))
        /* NOR is expanded as "NOT ( ... OR ...)" */
        g_string_append_printf(str, " OR ");
    else
        /* All others expanded as-is, skip the '$' prefix though */
        g_string_append_printf(str, " %s ", current_key + 1);

    return 0;
}

static int json2sql_array_end(struct saj_parser *parser, void *priv)
{
    GString *str = priv;

    g_string_append(str, ")");
    return 0;
}

static const struct saj_parser_operations json2sql_ops = {
    .so_object_begin = json2sql_object_begin,
    .so_array_begin  = json2sql_array_begin,
    .so_array_elt    = json2sql_array_elt,
    .so_array_end    = json2sql_array_end,
};

int clause_filter_convert(struct dss_handle *handle, GString *qry,
                          const struct dss_filter *filter)
{
    struct saj_parser   json2sql;
    int                 rc;

    if (!filter)
        return 0; /* nothing to do */

    if (!json_is_object(filter->df_json))
        LOG_RETURN(-EINVAL, "Filter is not a valid JSON object");

    g_string_append(qry, " WHERE ");

    rc = saj_parser_init(&json2sql, &json2sql_ops, qry, handle);
    if (rc)
        LOG_RETURN(rc, "Cannot initialize JSON to SQL converter");

    rc = saj_parser_run(&json2sql, filter->df_json);
    if (rc)
        LOG_GOTO(out_free, rc, "Cannot convert filter into SQL query");

out_free:
    saj_parser_free(&json2sql);
    return rc;
}

void build_object_json_filter(char **filter, const char *oid, const char *uuid,
                              int version)
{
    GString *str = g_string_new(NULL);
    int count = 0;

    count += (oid ? 1 : 0);
    count += (uuid ? 1 : 0);
    count += (version && uuid ? 1 : 0);

    g_string_append(str, "{\"$AND\": [");

    if (oid)
        g_string_append_printf(str, "{\"DSS::OBJ::oid\": \"%s\"}%s",
                               oid, --count != 0 ? ", " : "");

    if (uuid)
        g_string_append_printf(str, "{\"DSS::OBJ::uuid\": \"%s\"}%s",
                               uuid, --count != 0 ? ", " : "");

    if (version && uuid)
        g_string_append_printf(str, "{\"DSS::OBJ::version\": \"%d\"}",
                               version);

    g_string_append(str, "]}");

    *filter = xstrdup(str->str);
    g_string_free(str, true);
}
