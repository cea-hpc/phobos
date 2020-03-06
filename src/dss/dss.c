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
 * \brief  Phobos Distributed State Service API.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "pho_common.h"
#include "pho_type_utils.h"
#include "pho_dss.h"
#include "pho_cfg.h"
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <inttypes.h>
#include <string.h>
#include <glib.h>
#include <libpq-fe.h>
#include <jansson.h>
#include <string.h>
#include <strings.h>
#include <assert.h>
#include <gmodule.h>

/* Necessary local function declaration
 * (first two declaration are swapped because of a checkpatch bug)
 */
static void dss_device_result_free(void *void_dev);
static int dss_device_from_pg_row(void *void_dev, PGresult *res, int row_num);
static int dss_media_from_pg_row(void *void_media, PGresult *res, int row_num);
static void dss_media_result_free(void *void_media);
static int dss_layout_from_pg_row(void *void_layout, PGresult *res,
                                  int row_num);
static void dss_layout_result_free(void *void_layout);
static int dss_object_from_pg_row(void *void_object, PGresult *res,
                                  int row_num);
static void dss_object_result_free(void *void_object);

/** List of configuration parameters for tape_model */
enum pho_cfg_params_tape_model {
    /* DSS parameters */
    PHO_CFG_TAPE_MODEL_supported_list,

    /* Delimiters, update when modifying options */
    PHO_CFG_TAPE_MODEL_FIRST = PHO_CFG_TAPE_MODEL_supported_list,
    PHO_CFG_TAPE_MODEL_LAST  = PHO_CFG_TAPE_MODEL_supported_list,
};

const struct pho_config_item cfg_tape_model[] = {
    [PHO_CFG_TAPE_MODEL_supported_list] = {
        .section = "tape_model",
        .name    = "supported_list",
        .value   = "LTO5,LTO6,LTO7,LTO8,T10KB,T10KC,T10KD"
    },
};

/* init by parse_supported_tape_models function called at config init */
static GPtrArray *supported_tape_models;

/**
 * Parse config to init supported model for media of tape type
 *
 * @return 0 if success, -EALREADY if job already done or a negative error code
 */
static int parse_supported_tape_models(void)
{
    const char *config_list;
    char *parsed_config_list;
    char *saved_ptr;
    char *conf_model;
    GPtrArray *built_supported_tape_models;

    if (supported_tape_models)
        return -EALREADY;

    /* get tape supported model from conf */
    config_list = PHO_CFG_GET(cfg_tape_model, PHO_CFG_TAPE_MODEL,
                              supported_list);
    if (!config_list)
        LOG_RETURN(-EINVAL, "no supported_list tape model found in config");

    /* duplicate supported model to parse it */
    parsed_config_list = strdup(config_list);
    if (!parsed_config_list)
        LOG_RETURN(-errno, "Error on duplicating list of tape models");

    /* allocate built_supported_tape_models */
    built_supported_tape_models = g_ptr_array_new_with_free_func(free);
    if (!built_supported_tape_models) {
        free(parsed_config_list);
        LOG_RETURN(-ENOMEM, "Error on allocating built_supported_tape_models");
    }

    /* parse model list */
    for (conf_model = strtok_r(parsed_config_list, ",", &saved_ptr);
         conf_model;
         conf_model = strtok_r(NULL, ",", &saved_ptr)) {
        char *new_model;

        /* dup tape model */
        new_model = strdup(conf_model);
        if (!new_model) {
            int rc;

            rc = -errno;
            g_ptr_array_unref(built_supported_tape_models);
            free(parsed_config_list);
            LOG_RETURN(rc, "Error on duplicating parsed tape model");
        }

        /* store tape model */
        g_ptr_array_add(built_supported_tape_models, new_model);
    }

    free(parsed_config_list);
    supported_tape_models = built_supported_tape_models;
    return 0;
}

/** List of configuration parameters for DSS */
enum pho_cfg_params_dss {
    /* DSS parameters */
    PHO_CFG_DSS_connect_string,

    /* Delimiters, update when modifying options */
    PHO_CFG_DSS_FIRST = PHO_CFG_DSS_connect_string,
    PHO_CFG_DSS_LAST  = PHO_CFG_DSS_connect_string,
};

const struct pho_config_item cfg_dss[] = {
    [PHO_CFG_DSS_connect_string] = {
        .section = "dss",
        .name    = "connect_string",
        .value   = "dbname=phobos host=localhost"
    },
};

struct dss_result {
    PGresult *pg_res;
    enum dss_type item_type;
    union {
        char   raw[0];
        struct media_info   media[0];
        struct dev_info     dev[0];
        struct object_info  object[0];
        struct layout_info  layout[0];
    } items;
};

#define res_of_item_list(_list) \
    container_of((_list), struct dss_result, items)

/**
 * Handle notices from PostgreSQL. Strip the trailing newline and re-emit them
 * through phobos log API.
 */
static void dss_pg_logger(void *arg, const char *message)
{
    size_t mlen = strlen(message);

    if (message[mlen - 1] == '\n')
        mlen -= 1;

    pho_info("%*s", (int)mlen, message);
}

static inline char *dss_char4sql(PGconn *conn, const char *s)
{
    char *ns;

    if (s != NULL && s[0] != '\0') {
        // FIXME: this memory is leaked (for now), and should be freed with
        // PQfreemem (although this is an alias for free() on unix systems:
        // https://www.postgresql.org/docs/9.4/libpq-misc.html#LIBPQ-PQFREEMEM)
        ns = PQescapeLiteral(conn, s, strlen(s));
        if (ns == NULL) {
            pho_error(
                EINVAL, "Cannot escape litteral %s: %s", s, PQerrorMessage(conn)
            );
            return NULL;
        }
    } else {
        ns = strdup("NULL");
    }

    return ns;
}

int dss_init(struct dss_handle *handle)
{
    const char *conn_str;
    int rc;

    /* init static config parsing */
    rc = parse_supported_tape_models();
    if (rc && rc != -EALREADY)
        return rc;

    conn_str = PHO_CFG_GET(cfg_dss, PHO_CFG_DSS, connect_string);
    if (conn_str == NULL)
        return -EINVAL;

    handle->dh_conn = PQconnectdb(conn_str);

    if (PQstatus(handle->dh_conn) != CONNECTION_OK) {
        rc = -ENOTCONN;
        pho_error(rc, "Connection to database failed: %s",
                  PQerrorMessage(handle->dh_conn));
        PQfinish(handle->dh_conn);
        handle->dh_conn = NULL;
        return rc;
    }

    (void)PQsetNoticeProcessor(handle->dh_conn, dss_pg_logger, NULL);

    return 0;
}

void dss_fini(struct dss_handle *handle)
{
    PQfinish(handle->dh_conn);
}


struct sqlerr_map_item {
    const char *smi_prefix;     /**< SQL error code or class (prefix) */
    int         smi_errcode;    /**< Corresponding negated errno code */
};

/**
 * Map errors from SQL to closest errno.
 * The list is traversed from top to bottom and stops at first match, so make
 * sure that new items are inserted in most-specific first order.
 * See: https://www.postgresql.org/docs/9.4/static/errcodes-appendix.html
 */
static const struct sqlerr_map_item sqlerr_map[] = {
    /* Class 00 - Successful completion */
    {"00000", 0},
    /* Class 22 - Data exception */
    {"22", -EINVAL},
    /* Class 23 - Integrity constraint violation */
    {"23", -EEXIST},
    /* Class 42 - Syntax error or access rule violation */
    {"42", -EINVAL},
    /* Class 53 - Insufficient resources */
    {"53100", -ENOSPC},
    {"53200", -ENOMEM},
    {"53300", -EUSERS},
    {"53", -EIO},
    /* Catch all -- KEEP LAST -- */
    {"", -ECOMM}
};

/**
 * Convert PostgreSQL status codes to meaningful errno values.
 * @param  res[in]  Failed query result descriptor
 * @return negated errno value corresponding to the error
 */
static int psql_state2errno(const PGresult *res)
{
    char *sqlstate = PQresultErrorField(res, PG_DIAG_SQLSTATE);
    int   i;

    if (sqlstate == NULL)
        return 0;

    for (i = 0; i < ARRAY_SIZE(sqlerr_map); i++) {
        const char *pfx = sqlerr_map[i].smi_prefix;

        if (strncmp(pfx, sqlstate, strlen(pfx)) == 0)
            return sqlerr_map[i].smi_errcode;
    }

    /* sqlerr_map must contain a catch-all entry */
    UNREACHED();
}

/**
 * Retrieve a copy of a string contained in a JSON object under a given key.
 * The caller is responsible for freeing the result after use.
 *
 * \return a newly allocated copy of the string on success or NULL on error.
 */
static char *json_dict2str(const struct json_t *obj, const char *key)
{
    struct json_t   *current_obj;

    current_obj = json_object_get(obj, key);
    if (!current_obj) {
        pho_debug("Cannot retrieve object '%s'", key);
        return NULL;
    }

    return strdup(json_string_value(current_obj));
}

/**
 * Retrieve a signed but positive integer contained in a JSON object under a
 * given key.
 * An error is returned if no integer was found at this location.
 *
 * \retval the value on success and -1 on error.
 */
static int json_dict2int(const struct json_t *obj, const char *key)
{
    struct json_t   *current_obj;
    json_int_t       val;

    current_obj = json_object_get(obj, key);
    if (!current_obj) {
        pho_debug("Cannot retrieve object '%s'", key);
        return -1;
    }

    if (!json_is_integer(current_obj)) {
        pho_debug("JSON attribute '%s' not an integer", key);
        return -1;
    }

    val = json_integer_value(current_obj);
    if (val > INT_MAX) {
        pho_error(EOVERFLOW, "Cannot cast value from DSS for '%s'", key);
        return -1;
    }

    return val;
}

/**
 * Retrieve a signed but positive long long integer contained in a JSON object
 * under a given key.
 * An error is returned if no long long integer was found at this location.
 *
 * \retval the value on success and -1LL on error.
 */
static long long json_dict2ll(const struct json_t *obj, const char *key)
{
    struct json_t   *current_obj;

    current_obj = json_object_get(obj, key);
    if (!current_obj) {
        pho_debug("Cannot retrieve object '%s'", key);
        return -1LL;
    }

    if (!json_is_integer(current_obj)) {
        pho_debug("JSON attribute '%s' is not an integer", key);
        return -1LL;
    }

    return json_integer_value(current_obj);
}

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
 * helper arrays to build SQL query
 */
static const char * const select_query[] = {
    [DSS_DEVICE] = "SELECT family, model, id, adm_status,"
                   " host, path, lock, lock_ts FROM device",
    [DSS_MEDIA]  = "SELECT family, model, id, adm_status,"
                   " address_type, fs_type, fs_status, fs_label, stats, tags,"
                   " lock, lock_ts FROM media",
    [DSS_LAYOUT] = "SELECT oid, state, lyt_info, extents FROM extent",
    [DSS_OBJECT] = "SELECT oid, user_md FROM object",
};

static const size_t const res_size[] = {
    [DSS_DEVICE]  = sizeof(struct dev_info),
    [DSS_MEDIA]   = sizeof(struct media_info),
    [DSS_LAYOUT]  = sizeof(struct layout_info),
    [DSS_OBJECT]  = sizeof(struct object_info),
};

typedef int (*res_pg_constructor_t)(void *item, PGresult *res, int row_num);
static const res_pg_constructor_t const res_pg_constructor[] = {
    [DSS_DEVICE]  = dss_device_from_pg_row,
    [DSS_MEDIA]   = dss_media_from_pg_row,
    [DSS_LAYOUT]  = dss_layout_from_pg_row,
    [DSS_OBJECT]  = dss_object_from_pg_row,
};

typedef void (*res_destructor_t)(void *item);
static const res_destructor_t const res_destructor[] = {
    [DSS_DEVICE]  = dss_device_result_free,
    [DSS_MEDIA]   = dss_media_result_free,
    [DSS_LAYOUT]  = dss_layout_result_free,
    [DSS_OBJECT]  = dss_object_result_free,
};

static const char * const insert_query[] = {
    [DSS_DEVICE] = "INSERT INTO device (family, model, id, host, adm_status,"
                   " path, lock) VALUES ",
    [DSS_MEDIA]  = "INSERT INTO media (family, model, id, adm_status,"
                   " fs_type, address_type, fs_status, fs_label, stats, tags,"
                   " lock)"
                   " VALUES ",
    [DSS_LAYOUT] = "INSERT INTO extent (oid, state, lyt_info, extents) VALUES ",
    [DSS_OBJECT] = "INSERT INTO object (oid, user_md) VALUES ",
};

static const char * const update_query[] = {
    [DSS_DEVICE] = "UPDATE device SET (family, model, host, adm_status,"
                   " path) ="
                   " ('%s', %s, '%s', '%s', '%s')"
                   " WHERE id = '%s';",
    [DSS_MEDIA]  = "UPDATE media SET (family, model, adm_status,"
                   " fs_type, address_type, fs_status, fs_label, stats, tags) ="
                   " ('%s', %s, '%s', '%s', '%s', '%s', %s, %s, %s)"
                   " WHERE id = '%s';",
    [DSS_LAYOUT] = "UPDATE extent SET (state, lyt_info, extents) ="
                   " ('%s', '%s', '%s')"
                   " WHERE oid = '%s';",
    [DSS_OBJECT] = "UPDATE object SET user_md = '%s' "
                   " WHERE oid = '%s';",

};

static const char * const delete_query[] = {
    [DSS_DEVICE] = "DELETE FROM device WHERE id = '%s'; ",
    [DSS_MEDIA]  = "DELETE FROM media WHERE id = '%s'; ",
    [DSS_LAYOUT] = "DELETE FROM extent WHERE oid = '%s'; ",
    [DSS_OBJECT] = "DELETE FROM object WHERE oid = '%s'; ",

};

static const char * const insert_query_values[] = {
    [DSS_DEVICE] = "('%s', %s, '%s', '%s', '%s', '%s', '')%s",
    [DSS_MEDIA]  = "('%s', %s, %s, '%s', '%s', '%s', '%s', '%s', %s, %s,"
                   " '')%s",
    [DSS_LAYOUT] = "('%s', '%s', '%s', '%s')%s",
    [DSS_OBJECT] = "('%s', '%s')%s",
};

enum dss_lock_queries {
    DSS_LOCK_QUERY =  0,
    DSS_UNLOCK_QUERY,
    DSS_UNLOCK_ALWAYS_QUERY,
};

/**
 * In order to avoid partials lock we check if all the items are ready
 * to be locked
 * "%d IN (SELECT count(*) FROM .." allow to compare the count of item pass
 * to the (un)lock function to the current lockable item count.
 * "IN" is used as we can't do a subquery with ==
 */
static const char * const lock_query[] = {
    [DSS_UNLOCK_QUERY] = "UPDATE %s SET lock='', lock_ts=0 WHERE id IN %s AND "
                         "lock=%s AND "
                         "%d IN (SELECT count(*) FROM %s WHERE id IN %s AND "
                         "       lock=%s);",
    [DSS_UNLOCK_ALWAYS_QUERY] =
                         "UPDATE %s SET lock='', lock_ts=0 WHERE id IN %s AND "
                         "lock!='' AND "
                         "%d IN (SELECT count(*) FROM %s WHERE id IN %s AND "
                         "       lock!='');",
    [DSS_LOCK_QUERY] =   "UPDATE %s SET lock=%s, "
                         "lock_ts=extract(epoch from NOW()) "
                         "WHERE lock='' AND id IN %s AND "
                         "%d IN (SELECT count(*) FROM %s WHERE id IN %s AND "
                         "       lock='');",
};

static const char * const simple_lock_query[] = {
    [DSS_UNLOCK_QUERY] = "UPDATE %s SET lock='', lock_ts=0 WHERE id IN %s",
                         "AND lock=%s;",
    [DSS_UNLOCK_ALWAYS_QUERY] =
                         "UPDATE %s SET lock='', lock_ts=0 WHERE id IN %s;",
    [DSS_LOCK_QUERY] =   "UPDATE %s SET lock=%s, "
                         "lock_ts=extract(epoch from NOW()) "
                         "WHERE lock='' AND id IN %s;",
};

/**
 * Load long long integer value from JSON object and zero-out the field on error
 * Caller is responsible for using the macro on a compatible type, a signed 64
 * integer preferrably or anything compatible with the expected range of value
 * for the given field.
 * If 'optional' is true and the field is missing, we use 0 as a default value.
 * Updates the rc variable (_rc) in case of error.
 */
#define LOAD_CHECK64(_rc, _j, _s, _f, optional)  do {           \
                            long long _tmp;                     \
                            _tmp  = json_dict2ll((_j), #_f);    \
                            if (_tmp < 0) {                     \
                                (_s)->_f = 0LL;                 \
                                if (!optional)                  \
                                    _rc = -EINVAL;              \
                            } else {                            \
                                (_s)->_f = _tmp;                \
                            }                                   \
                        } while (0)

/**
 * Load integer value from JSON object and zero-out the field on error
 * Caller is responsible for using the macro on a compatible type, a signed 32
 * integer preferrably or anything compatible with the expected range of value
 * for the given field.
 * If 'optional' is true and the field is missing, we use 0 as a default value.
 * Updates the rc variable (_rc) in case of error.
 */
#define LOAD_CHECK32(_rc, _j, _s, _f, optional)    do {         \
                            int _tmp;                           \
                            _tmp = json_dict2int((_j), #_f);    \
                            if (_tmp < 0) {                     \
                                (_s)->_f = 0;                   \
                                if (!optional)                  \
                                    _rc = -EINVAL;              \
                            } else {                            \
                                (_s)->_f = _tmp;                \
                            }                                   \
                        } while (0)

/**
 * Extract media statistics from json
 *
 * \param[out]  stats  media stats to be filled with stats
 * \param[in]  json   String with json media stats
 *
 * \return 0 on success, negative error code on failure.
 */
static int dss_media_stats_decode(struct media_stats *stats, const char *json)
{
    json_t          *root;
    json_error_t     json_error;
    int              rc = 0;
    ENTRY;

    root = json_loads(json, JSON_REJECT_DUPLICATES, &json_error);
    if (!root)
        LOG_RETURN(-EINVAL, "Failed to parse json data: %s", json_error.text);

    if (!json_is_object(root))
        LOG_GOTO(out_decref, rc = -EINVAL, "Invalid stats description");

    pho_debug("STATS: '%s'", json);

    LOAD_CHECK64(rc, root, stats, nb_obj, false);
    LOAD_CHECK64(rc, root, stats, logc_spc_used, false);
    LOAD_CHECK64(rc, root, stats, phys_spc_used, false);
    LOAD_CHECK64(rc, root, stats, phys_spc_free, false);
    LOAD_CHECK32(rc, root, stats, nb_errors, true);
    LOAD_CHECK32(rc, root, stats, last_load, true);

out_decref:
    /* Most of the values above are not used to make decisions, so don't
     * break the whole dss_get because of missing values in media stats
     * (from previous phobos version).
     *
     * The only important field is phys_spc_free, which is used to check if
     * a media has enough room to write data. In case this field is
     * invalid, this function set it to 0, so the media won't be selected
     * (as in the case we would return an error here).
     */
    if (rc)
        pho_debug("Json parser: missing/invalid fields in media stats");

    json_decref(root);
    return rc;
}

#define JSON_INTEGER_SET_NEW(_j, _s, _f)                        \
    do {                                                        \
        json_t  *_tmp = json_integer((_s)._f);                  \
        if (!_tmp)                                              \
            pho_error(-ENOMEM, "Failed to encode '%s'", #_f);   \
        else                                                    \
            json_object_set_new((_j), #_f, _tmp);               \
    } while (0)


/**
 * Encode media statistics to json
 *
 * \param[in]  stats  media stats to be encoded
 *
 * \return Return a string json object
 */
static char *dss_media_stats_encode(struct media_stats stats)
{
    json_t  *root;
    char    *res = NULL;
    ENTRY;

    root = json_object();
    if (!root) {
        pho_error(-ENOMEM, "Failed to create json object");
        return NULL;
    }

    JSON_INTEGER_SET_NEW(root, stats, nb_obj);
    JSON_INTEGER_SET_NEW(root, stats, logc_spc_used);
    JSON_INTEGER_SET_NEW(root, stats, phys_spc_used);
    JSON_INTEGER_SET_NEW(root, stats, phys_spc_free);
    JSON_INTEGER_SET_NEW(root, stats, nb_errors);
    JSON_INTEGER_SET_NEW(root, stats, last_load);

    res = json_dumps(root, 0);
    if (!res)
        pho_error(EINVAL, "Failed to dump JSON to ASCIIZ");

    json_decref(root);

    pho_debug("Created JSON representation for stats: '%s'",
              res ? res : "(null)");
    return res;
}

/**
 * Extract media tags from json
 *
 * \param[out] tags   pointer to an array of tags (char**) to allocate in this
 *                    function
 * \param[out] n_tags size of tags
 * \param[in]  json   String with json media tags
 *
 * \return 0 on success, negative error code on failure.
 */
static int dss_tags_decode(struct tags *tags, const char *json)
{
    json_error_t     json_error;
    json_t          *array_entry;
    json_t          *tag_array;
    const char      *tag;
    size_t           i;
    int              rc = 0;
    ENTRY;

    if (!json || json[0] == '\0') {
        memset(tags, 0, sizeof(*tags));
        return 0;
    }

    tag_array = json_loads(json, JSON_REJECT_DUPLICATES, &json_error);
    if (!tag_array)
        LOG_RETURN(-EINVAL, "Failed to parse media tags json data '%s': %s",
                   json, json_error.text);

    if (!(json_is_array(tag_array) || json_is_null(tag_array)))
        pho_warn("media tags json is not an array");

    /* No tags (not an array or empty array), set table to NULL */
    tags->n_tags = json_array_size(tag_array);
    if (tags->n_tags == 0) {
        tags->tags = NULL;
        goto out_free;
    }

    tags->tags = calloc(tags->n_tags, sizeof(*tags->tags));
    for (i = 0; i < tags->n_tags; i++) {
        array_entry = json_array_get(tag_array, i);
        tag = json_string_value(array_entry);
        if (tag) {
            tags->tags[i] = strdup(tag);
        } else {
            /* Fallback to empty string to avoid unexpected NULL */
            tags->tags[i] = strdup("");
            pho_warn("Non string tag in media tags");
        }
    }

out_free:
    json_decref(tag_array);
    return rc;
}

/**
 * Encode media tags to json
 *
 * \param[in]  tags    media tags to be encoded
 * \param[in]  n_tags  size of the tags array
 *
 * \return Return a string json object allocated with malloc. The caller must
 *     free() this string.
 */
static char *dss_tags_encode(const struct tags *tags)
{
    json_t  *tag_array;
    size_t   i;
    char    *res = NULL;
    ENTRY;

    tag_array = json_array();
    if (!tag_array) {
        pho_error(-ENOMEM, "Failed to create json object");
        return NULL;
    }

    for (i = 0; i < tags->n_tags; i++) {
        if (json_array_append_new(tag_array, json_string(tags->tags[i]))) {
            res = NULL;
            LOG_GOTO(out_free, -ENOMEM,
                     "Could not append media tag to json tag array");
        }
    }
    res = json_dumps(tag_array, 0);

out_free:
    json_decref(tag_array);
    return res;
}

/**
 * Extract layout type and parameters from json
 *
 */
static int dss_layout_desc_decode(struct module_desc *desc, const char *json)
{
    json_t          *root;
    json_t          *attrs;
    json_error_t     json_error;
    int              rc;
    ENTRY;

    pho_debug("Decoding JSON representation for module desc: '%s'", json);

    memset(desc, 0, sizeof(*desc));

    root = json_loads(json, JSON_REJECT_DUPLICATES, &json_error);
    if (!root)
        LOG_RETURN(-EINVAL, "Failed to parse json data: %s", json_error.text);

    if (!json_is_object(root))
        LOG_GOTO(out_free, rc = -EINVAL, "Invalid module description");

    /* Mandatory fields */
    desc->mod_name  = json_dict2str(root, PHO_MOD_DESC_KEY_NAME);
    if (!desc->mod_name)
        LOG_GOTO(out_free, rc = -EINVAL, "Missing attribute %s",
                 PHO_MOD_DESC_KEY_NAME);

    desc->mod_major = json_dict2int(root, PHO_MOD_DESC_KEY_MAJOR);
    if (desc->mod_major < 0)
        LOG_GOTO(out_free, rc = -EINVAL, "Missing attribute %s",
                 PHO_MOD_DESC_KEY_MAJOR);

    desc->mod_minor = json_dict2int(root, PHO_MOD_DESC_KEY_MINOR);
    if (desc->mod_minor < 0)
        LOG_GOTO(out_free, rc = -EINVAL, "Missing attribute %s",
                 PHO_MOD_DESC_KEY_MINOR);

    /* Optional attributes */
    attrs = json_object_get(root, PHO_MOD_DESC_KEY_ATTRS);
    if (!attrs) {
        rc = 0;
        goto out_free;  /* no attributes, nothing else to do */
    }

    if (!json_is_object(attrs))
        LOG_GOTO(out_free, rc = -EINVAL, "Invalid attributes format");

    rc = pho_json_raw_to_attrs(&desc->mod_attrs, attrs);
    if (rc)
        LOG_GOTO(out_free, rc, "Cannot decode module attributes");

out_free:
    if (rc) {
        free(desc->mod_name);
        memset(desc, 0, sizeof(*desc));
    }

    json_decref(root);
    return rc;
}

static char *dss_layout_desc_encode(struct module_desc *desc)
{
    char    *result = NULL;
    json_t  *attrs  = NULL;
    json_t  *root;
    int      rc;
    ENTRY;

    root = json_object();
    if (!root) {
        pho_error(-ENOMEM, "Failed to create json object");
        return NULL;
    }

    rc = json_object_set_new(root, PHO_MOD_DESC_KEY_NAME,
                             json_string(desc->mod_name));
    if (rc)
        LOG_GOTO(out_free, rc = -EINVAL, "Cannot set layout name");

    rc = json_object_set_new(root, PHO_MOD_DESC_KEY_MAJOR,
                             json_integer(desc->mod_major));
    if (rc)
        LOG_GOTO(out_free, rc = -EINVAL, "Cannot set layout major number");

    rc = json_object_set_new(root, PHO_MOD_DESC_KEY_MINOR,
                             json_integer(desc->mod_minor));
    if (rc)
        LOG_GOTO(out_free, rc = -EINVAL, "Cannot set layout minor number");

    if (!pho_attrs_is_empty(&desc->mod_attrs)) {
        attrs = json_object();
        if (!attrs)
            LOG_GOTO(out_free, rc = -ENOMEM, "Cannot set layout attributes");

        rc = pho_attrs_to_json_raw(&desc->mod_attrs, attrs);
        if (rc)
            LOG_GOTO(out_free, rc, "Cannot convert layout attributes");

        rc = json_object_set_new(root, PHO_MOD_DESC_KEY_ATTRS, attrs);
        if (rc)
            LOG_GOTO(out_free, rc = -EINVAL, "Cannot set layout attributes");
    }

    result = json_dumps(root, 0);

    pho_debug("Created json representation for layout type: '%s'",
              result ? result : "(null)");

out_free:
    json_decref(root); /* if attrs, it is included in root */
    return result;
}

/**
 * Extract extents from json
 *
 * \param[out] extents extent list
 * \param[out] count  number of extents decoded
 * \param[in]  json   String with json media stats
 *
 * \return 0 on success, negative error code on failure.
 */
static int dss_layout_extents_decode(struct extent **extents, int *count,
                                     const char *json)
{
    json_t          *root;
    json_t          *child;
    json_error_t     json_error;
    struct extent   *result = NULL;
    size_t           extents_res_size;
    int              rc;
    int              i;
    ENTRY;

    pho_debug("Decoding JSON representation for extents: '%s'", json);

    root = json_loads(json, JSON_REJECT_DUPLICATES, &json_error);
    if (!root)
        LOG_RETURN(-EINVAL, "Failed to parse json data: %s", json_error.text);

    if (!json_is_array(root))
        LOG_GOTO(out_decref, rc = -EINVAL, "Invalid extents description");

    *count = json_array_size(root);
    if (*count == 0) {
        extents = NULL;
        LOG_GOTO(out_decref, rc = -EINVAL,
                 "json parser: extents array is empty");
    }

    extents_res_size = sizeof(struct extent) * (*count);
    result = malloc(extents_res_size);
    if (result == NULL)
        LOG_GOTO(out_decref, rc = -ENOMEM,
                 "Memory allocation of size %zu failed", extents_res_size);

    for (i = 0; i < *count; i++) {
        char    *tmp;

        child = json_array_get(root, i);
        result[i].layout_idx = i;
        result[i].size = json_dict2ll(child, "sz");
        if (result[i].size < 0)
            LOG_GOTO(out_decref, rc = -EINVAL, "Missing attribute 'sz'");

        tmp = json_dict2str(child, "addr");
        if (!tmp)
            LOG_GOTO(out_decref, rc = -EINVAL, "Missing attribute 'addr'");

        result[i].address.buff = tmp;
        result[i].address.size = strlen(tmp) + 1;

        /* FIXME: unnecessary allocation */
        tmp = json_dict2str(child, "fam");
        if (!tmp)
            LOG_GOTO(out_decref, rc = -EINVAL, "Missing attribute 'fam'");

        result[i].media.family = str2rsc_family(tmp);
        free(tmp);

        /*XXX fs_type & address_type retrieved from media info */
        if (result[i].media.family == PHO_RSC_INVAL)
            LOG_GOTO(out_decref, rc = -EINVAL, "Invalid medium family");

        /* FIXME: unnecessary allocation */
        tmp = json_dict2str(child, "media");
        if (!tmp)
            LOG_GOTO(out_decref, rc = -EINVAL, "Missing attribute 'media'");

        rc = pho_id_name_set(&result[i].media, tmp);
        if (rc)
            LOG_GOTO(out_decref, rc = -EINVAL, "Failed to set media id");
        free(tmp);
    }

    *extents = result;
    rc = 0;

out_decref:
    if (rc)
        free(result);
    json_decref(root);
    return rc;
}

/**
 * Encode extents to json
 *
 * \param[in]   extents extent list
 * \param[in]   count   number of extents
 * \param[out]  error   error count
 *
 * \return json as string
 */

static char *dss_layout_extents_encode(struct extent *extents,
                                       unsigned int count, int *error)
{
    json_t  *root;
    json_t  *child;
    char    *s;
    int      err_cnt = 0;
    int      rc;
    int      i;
    ENTRY;

    root = json_array();
    if (!root) {
        pho_error(ENOMEM, "Failed to create json root object");
        return NULL;
    }

    for (i = 0; i < count; i++) {
        child = json_object();
        if (!child) {
            pho_error(ENOMEM, "Failed to create json child object");
            err_cnt++;
            continue;
        }

        rc = json_object_set_new(child, "sz", json_integer(extents[i].size));
        if (rc) {
            pho_error(EINVAL, "Failed to encode 'sz' (%zu)", extents[i].size);
            err_cnt++;
        }

        /* We may have no address yet. */
        if (extents[i].address.buff != NULL) {
            rc = json_object_set_new(child, "addr",
                                     json_string(extents[i].address.buff));
            if (rc) {
                pho_error(EINVAL, "Failed to encode 'addr' (%s)",
                          extents[i].address.buff);
                err_cnt++;
            }
        }

        rc = json_object_set_new(child, "fam",
                         json_string(rsc_family2str(extents[i].media.family)));
        if (rc) {
            pho_error(EINVAL, "Failed to encode 'fam' (%d:%s)",
                      extents[i].media.family,
                      rsc_family2str(extents[i].media.family));
            err_cnt++;
        }

        rc = json_object_set_new(child, "media",
                         json_string(extents[i].media.name));
        if (rc) {
            pho_error(EINVAL, "Failed to encode 'media' (%s)",
                      extents[i].media.name);
            err_cnt++;
        }

        rc = json_array_append_new(root, child);
        if (rc) {
            pho_error(EINVAL, "Failed to attach child to root object");
            err_cnt++;
        }
    }

    *error = err_cnt;

    s = json_dumps(root, 0);
    json_decref(root);
    if (!s) {
        pho_error(EINVAL, "Failed to dump JSON to ASCIIZ");
        return NULL;
    }

    pho_debug("Created JSON representation for extents: '%s'", s);
    return s;
}

static int get_object_setrequest(PGconn *_conn, struct object_info *item_list,
                                 int item_cnt, enum dss_set_action action,
                                 GString *request)
{
    int i;
    ENTRY;

    for (i = 0; i < item_cnt; i++) {
        struct object_info *p_object = &item_list[i];

        if (p_object->oid == NULL)
            LOG_RETURN(-EINVAL, "Object oid cannot be NULL");

        if (action == DSS_SET_DELETE) {
            g_string_append_printf(request, delete_query[DSS_OBJECT],
                                   p_object->oid);
        } else if (action == DSS_SET_INSERT) {
            g_string_append_printf(request, insert_query_values[DSS_OBJECT],
                                   p_object->oid, p_object->user_md,
                                   i < item_cnt-1 ? "," : ";");
        } else if (action == DSS_SET_UPDATE) {
            g_string_append_printf(request, update_query[DSS_OBJECT],
                                   p_object->user_md, p_object->oid);
        }
    }
    return 0;
}

static int get_layout_setrequest(PGconn *_conn, struct layout_info *item_list,
                                 int item_cnt, enum dss_set_action action,
                                 GString *request, int *error)
{
    int rc = 0;
    int i;
    ENTRY;

    for (i = 0; i < item_cnt && rc == 0; i++) {
        struct layout_info  *p_layout = &item_list[i];
        char                *layout = NULL;
        char                *pres = NULL;

        if (p_layout->oid == NULL)
            LOG_RETURN(-EINVAL, "Extent oid cannot be NULL");

        if (action == DSS_SET_DELETE) {
            g_string_append_printf(request, delete_query[DSS_LAYOUT],
                                   p_layout->oid);
            continue;
        }

        layout = dss_layout_extents_encode(p_layout->extents,
                                           p_layout->ext_count, error);
        if (!layout)
            LOG_GOTO(out_free, rc = -EINVAL, "JSON layout encoding error");

        pres = dss_layout_desc_encode(&p_layout->layout_desc);
        if (!pres)
            LOG_GOTO(out_free, rc = -EINVAL, "JSON layout desc encoding error");

        if (action == DSS_SET_INSERT)
            g_string_append_printf(request, insert_query_values[DSS_LAYOUT],
                                   p_layout->oid,
                                   extent_state2str(p_layout->state),
                                   pres, layout, i < item_cnt-1 ? "," : ";");
        else if (action == DSS_SET_UPDATE)
            g_string_append_printf(request, update_query[DSS_LAYOUT],
                                   extent_state2str(p_layout->state),
                                   pres, layout, p_layout->oid);
out_free:
        free(layout);
        free(pres);
    }

    return rc;
}

/**
 * Check if tape model is listed in config.
 * -EINVAL is returned if given model is not listed as supported in the conf.
 * Match between model and supported conf list is case insensitive.
 *
 * @param[in] model
 *
 * @return true if model is on supported config list, else return false.
 */
static bool dss_tape_model_check(const char *model)
{
    int index;

    assert(model);
    assert(supported_tape_models);

    /* if found return true as success value */
    for (index = 0; index < supported_tape_models->len; index++)
        if (strcasecmp(g_ptr_array_index(supported_tape_models, index),
                       model) == 0)
            return true;

    /* not found : return false */
    return false;
}

static int get_media_setrequest(PGconn *conn, struct media_info *item_list,
                                int item_cnt, enum dss_set_action action,
                                GString *request)
{
    int i;
    ENTRY;

    for (i = 0; i < item_cnt; i++) {
        struct media_info   *p_media = &item_list[i];

        if (action == DSS_SET_DELETE) {
            g_string_append_printf(request, delete_query[DSS_MEDIA],
                                   p_media->rsc.id.name);
        } else {
            char *medium_name = NULL;
            char *fs_label = NULL;
            char *model = NULL;
            char *stats = NULL;
            char *tags = NULL;
            char *tmp_stats = NULL;
            char *tmp_tags = NULL;

            /* check tape model validity */
            if (p_media->rsc.id.family == PHO_RSC_TAPE &&
                    !dss_tape_model_check(p_media->rsc.model))
                LOG_RETURN(-EINVAL, "invalid media tape model '%s'",
                           p_media->rsc.model);

            medium_name = dss_char4sql(conn, p_media->rsc.id.name);
            fs_label = dss_char4sql(conn, p_media->fs.label);
            model = dss_char4sql(conn, p_media->rsc.model);

            tmp_stats = dss_media_stats_encode(p_media->stats);
            stats = dss_char4sql(conn, tmp_stats);
            free(tmp_stats);

            tmp_tags = dss_tags_encode(&p_media->tags);
            tags = dss_char4sql(conn, tmp_tags);
            free(tmp_tags);

            if (!medium_name || !fs_label || !model || !stats || !tags) {
                free(medium_name);
                free(fs_label);
                free(model);
                free(stats);
                free(tags);
                LOG_RETURN(-ENOMEM, "memory allocation failed");
            }

            if (action == DSS_SET_INSERT) {
                g_string_append_printf(
                    request,
                    insert_query_values[DSS_MEDIA],
                    rsc_family2str(p_media->rsc.id.family),
                    model,
                    medium_name,
                    rsc_adm_status2str(p_media->rsc.adm_status),
                    fs_type2str(p_media->fs.type),
                    address_type2str(p_media->addr_type),
                    fs_status2str(p_media->fs.status),
                    fs_label,
                    stats,
                    tags,
                    i < item_cnt-1 ? "," : ";"
                );
            } else if (action == DSS_SET_UPDATE) {
                g_string_append_printf(
                    request,
                    update_query[DSS_MEDIA],
                    rsc_family2str(p_media->rsc.id.family),
                    model,
                    rsc_adm_status2str(p_media->rsc.adm_status),
                    fs_type2str(p_media->fs.type),
                    address_type2str(p_media->addr_type),
                    fs_status2str(p_media->fs.status),
                    fs_label,
                    stats,
                    tags,
                    p_media->rsc.id.name
                );
            }

            free(medium_name);
            free(fs_label);
            free(model);
            free(stats);
            free(tags);
        }
    }

    return 0;
}

static int get_device_setrequest(PGconn *conn, struct dev_info *item_list,
                                 int item_cnt, enum dss_set_action action,
                                 GString *request)
{
    int i;
    ENTRY;

    for (i = 0; i < item_cnt; i++) {
        struct dev_info *p_dev = &item_list[i];
        char            *model;

        if (p_dev->rsc.id.name == NULL)
            LOG_RETURN(-EINVAL, "Device serial cannot be NULL");

        if (action == DSS_SET_DELETE) {
            g_string_append_printf(request, delete_query[DSS_DEVICE],
                                   p_dev->rsc.id.name);
        } else if (action == DSS_SET_INSERT) {
            model = dss_char4sql(conn, p_dev->rsc.model);
            if (!model)
                LOG_RETURN(-ENOMEM, "memory allocation failed");

            g_string_append_printf(request, insert_query_values[DSS_DEVICE],
                                   rsc_family2str(p_dev->rsc.id.family), model,
                                   p_dev->rsc.id.name, p_dev->host,
                                   rsc_adm_status2str(p_dev->rsc.adm_status),
                                   p_dev->path, i < item_cnt-1 ? "," : ";");
            free(model);
        } else if (action == DSS_SET_UPDATE) {
            model = dss_char4sql(conn, p_dev->rsc.model);
            if (!model)
                LOG_RETURN(-ENOMEM, "memory allocation failed");

            g_string_append_printf(request, update_query[DSS_DEVICE],
                                   rsc_family2str(p_dev->rsc.id.family),
                                   model, p_dev->host,
                                   rsc_adm_status2str(p_dev->rsc.adm_status),
                                   p_dev->path, p_dev->rsc.id.name);
            free(model);
        }
    }

    return 0;
}

static int dss_build_uid_list(PGconn *conn, void *item_list, int item_cnt,
                              enum dss_type type, GString *ids)
{
    char         *escape_string;
    unsigned int  escape_len;
    int           i;

    switch (type) {
    case DSS_DEVICE:
        for (i = 0; i < item_cnt; i++) {
            struct dev_info *dev_ls = item_list;

            escape_len = strlen(dev_ls[i].rsc.id.name) * 2 + 1;
            escape_string = malloc(escape_len);
            if (!escape_string)
                LOG_RETURN(-ENOMEM, "Memory allocation failed");

            PQescapeStringConn(conn, escape_string, dev_ls[i].rsc.id.name,
                               escape_len, NULL);
            g_string_append_printf(ids, "%s'%s' %s",
                                   i ? "" : "(",
                                   escape_string,
                                   i < item_cnt-1 ? "," : ")");
            free(escape_string);
        }
        break;
    case DSS_MEDIA:
        for (i = 0; i < item_cnt; i++) {
            struct media_info *media_ls = item_list;

            escape_len = strlen(media_ls[i].rsc.id.name) * 2 + 1;
            escape_string = malloc(escape_len);
            if (!escape_string)
                LOG_RETURN(-ENOMEM, "Memory allocation failed");

            PQescapeStringConn(conn, escape_string,
                               media_ls[i].rsc.id.name,
                               escape_len, NULL);
            g_string_append_printf(ids, "%s'%s' %s",
                                   i ? "" : "(",
                                   escape_string,
                                   i < item_cnt-1 ? "," : ")");
            free(escape_string);
        }
        break;

    default:
        return -EINVAL;
    }
    return 0;
}

static inline bool is_type_supported(enum dss_type type)
{
    switch (type) {
    case DSS_OBJECT:
    case DSS_LAYOUT:
    case DSS_DEVICE:
    case DSS_MEDIA:
        return true;

    default:
        return false;
    }
}

/**
 * Unlike PQgetvalue that returns '' for NULL fields,
 * this function returns NULL for NULL fields.
 */
static inline char *get_str_value(PGresult *res, int row_number,
                                  int column_number)
{
    if (PQgetisnull(res, row_number, column_number))
        return NULL;

    return PQgetvalue(res, row_number, column_number);
}

static inline bool key_is_logical_op(const char *key)
{
    return !g_ascii_strcasecmp(key, "$AND") ||
           !g_ascii_strcasecmp(key, "$NOR") ||
           !g_ascii_strcasecmp(key, "$OR");
}

static int insert_string(GString *qry, const char *strval, bool is_idx)
{
    size_t   esc_len = strlen(strval) * 2 + 1;
    char    *esc_str;

    esc_str = malloc(esc_len);
    if (!esc_str)
        return -ENOMEM;

    /**
     * XXX This function has been deprecated in favor of PQescapeStringConn
     * but it is *safe* in this single-threaded single-DB case and it makes
     * the code slightly lighter here.
     */
    PQescapeString(esc_str, strval, esc_len);

    if (is_idx)
        g_string_append_printf(qry, "array['%s']", esc_str);
    else
        g_string_append_printf(qry, "'%s'", esc_str);

    free(esc_str);
    return 0;
}

static int json2sql_object_begin(struct saj_parser *parser, const char *key,
                                 json_t *value, void *priv)
{
    const char  *current_key = saj_parser_key(parser);
    const char  *field_impl;
    GString     *str = priv;
    bool         str_index = false;
    int          rc;

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
    } else if (!g_ascii_strcasecmp(current_key, "$INJSON")) {
        g_string_append(str, " @> ");
        str_index = true;
    } else if (!g_ascii_strcasecmp(current_key, "$XJSON")) {
        g_string_append(str, " ? ");
    } else {
        LOG_RETURN(-EINVAL, "Unexpected operator: '%s'", current_key);
    }

    switch (json_typeof(value)) {
    case JSON_STRING:
        rc = insert_string(str, json_string_value(value), str_index);
        if (rc)
            LOG_RETURN(rc, "Cannot insert string into SQL query");
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

static int clause_filter_convert(GString *qry, const struct dss_filter *filter)
{
    struct saj_parser   json2sql;
    int                 rc;

    if (!filter)
        return 0; /* nothing to do */

    if (!json_is_object(filter->df_json))
        LOG_RETURN(-EINVAL, "Filter is not a valid JSON object");

    g_string_append(qry, " WHERE ");

    rc = saj_parser_init(&json2sql, &json2sql_ops, qry);
    if (rc)
        LOG_RETURN(rc, "Cannot initialize JSON to SQL converter");

    rc = saj_parser_run(&json2sql, filter->df_json);
    if (rc)
        LOG_GOTO(out_free, rc, "Cannot convert filter into SQL query");

out_free:
    saj_parser_free(&json2sql);
    return rc;
}

/**
 * Fill a dev_info from the information in the `row_num`th row of `res`.
 */
static int dss_device_from_pg_row(void *void_dev, PGresult *res, int row_num)
{
    struct dev_info *dev = void_dev;

    dev->rsc.id.family  = str2rsc_family(PQgetvalue(res, row_num, 0));
    dev->rsc.model      = get_str_value(res, row_num, 1);
    pho_id_name_set(&dev->rsc.id, get_str_value(res, row_num, 2));
    dev->rsc.adm_status = str2rsc_adm_status(PQgetvalue(res, row_num, 3));
    dev->host           = get_str_value(res, row_num, 4);
    dev->path           = get_str_value(res, row_num, 5);
    dev->lock.lock      = get_str_value(res, row_num, 6);
    dev->lock.lock_ts   = strtoul(PQgetvalue(res, row_num, 7), NULL, 10);
    return 0;
}

/**
 * Free the resources associated with a dev_info built from a PGresult.
 */
static void dss_device_result_free(void *void_dev)
{
    (void)void_dev;
}

/**
 * Fill a media_info from the information in the `row_num`th row of `res`.
 */
static int dss_media_from_pg_row(void *void_media, PGresult *res, int row_num)
{
    struct media_info *medium = void_media;
    int rc;

    medium->rsc.id.family  = str2rsc_family(PQgetvalue(res, row_num, 0));
    medium->rsc.model      = get_str_value(res, row_num, 1);
    pho_id_name_set(&medium->rsc.id, PQgetvalue(res, row_num, 2));
    medium->rsc.adm_status = str2rsc_adm_status(PQgetvalue(res, row_num, 3));
    medium->addr_type      = str2address_type(PQgetvalue(res, row_num, 4));
    medium->fs.type        = str2fs_type(PQgetvalue(res, row_num, 5));
    medium->fs.status      = str2fs_status(PQgetvalue(res, row_num, 6));
    strncpy(medium->fs.label, PQgetvalue(res, row_num, 7),
            sizeof(medium->fs.label));
    /* make sure the label is zero-terminated */
    medium->fs.label[sizeof(medium->fs.label) - 1] = '\0';
    medium->lock.lock      = get_str_value(res, row_num, 10);
    medium->lock.lock_ts   = strtoul(PQgetvalue(res, row_num, 11), NULL, 10);

    /* No dynamic allocation here */
    rc = dss_media_stats_decode(&medium->stats, PQgetvalue(res, row_num, 8));
    if (rc) {
        pho_error(rc, "dss_media stats decode error");
        return rc;
    }

    rc = dss_tags_decode(&medium->tags, PQgetvalue(res, row_num, 9));
    if (rc) {
        pho_error(rc, "dss_media tags decode error");
        return rc;
    }
    pho_debug("Decoded %lu tags (%s)",
              medium->tags.n_tags, PQgetvalue(res, row_num, 9));

    return 0;
}

/**
 * Free the resources associated with media_info built from a PGresult.
 */
static void dss_media_result_free(void *void_media)
{
    struct media_info *media = void_media;

    if (!media)
        return;

    tags_free(&media->tags);
}

/**
 * Fill a layout_info from the information in the `row_num`th row of `res`.
 */
static int dss_layout_from_pg_row(void *void_layout, PGresult *res, int row_num)
{
    struct layout_info *layout = void_layout;
    int rc;

    layout->oid = PQgetvalue(res, row_num, 0);
    layout->state = str2extent_state(PQgetvalue(res, row_num, 1));
    rc = dss_layout_desc_decode(&layout->layout_desc,
                                PQgetvalue(res, row_num, 2));
    if (rc) {
        pho_error(rc, "dss_layout_desc decode error");
        return rc;
    }

    rc = dss_layout_extents_decode(&layout->extents,
                                   &layout->ext_count,
                                   PQgetvalue(res, row_num, 3));
    if (rc) {
        pho_error(rc, "dss_extent tags decode error");
        return rc;
    }

    return 0;
}

/**
 * Free the resources associated with layout_info built from a PGresult.
 */
static void dss_layout_result_free(void *void_layout)
{
    struct layout_info *layout = void_layout;

    if (!layout)
        return;

    /* Undo dss_layout_desc_decode */
    free(layout->layout_desc.mod_name);
    pho_attrs_free(&layout->layout_desc.mod_attrs);

    /* Undo dss_layout_extents_decode */
    layout_info_free_extents(layout);
}

/**
 * Fill a object_info from the information in the `row_num`th row of `res`.
 */
static int dss_object_from_pg_row(void *void_object, PGresult *res, int row_num)
{
    struct object_info *object = void_object;

    object->oid     = get_str_value(res, row_num, 0);
    object->user_md = get_str_value(res, row_num, 1);
    return 0;
}

/**
 * Free the resources associated with object_info built from a PGresult.
 */
static void dss_object_result_free(void *void_object)
{
    (void)void_object;
}

static int dss_generic_get(struct dss_handle *handle, enum dss_type type,
                           const struct dss_filter *filter, void **item_list,
                           int *item_cnt)
{
    PGconn              *conn = handle->dh_conn;
    PGresult            *res;
    GString             *clause;
    struct dss_result   *dss_res;
    size_t               dss_res_size;
    size_t               item_size;
    int                  rc = 0;
    int                  i = 0;
    ENTRY;

    if (conn == NULL || item_list == NULL || item_cnt == NULL)
        LOG_RETURN(-EINVAL, "dss - conn: %p, item_list: %p, item_cnt: %p",
                   conn, item_list, item_cnt);

    *item_list = NULL;
    *item_cnt  = 0;

    if (!is_type_supported(type))
        LOG_RETURN(-ENOTSUP, "Unsupported DSS request type %#x", type);

    /* get everything if no criteria */
    clause = g_string_new(select_query[type]);

    rc = clause_filter_convert(clause, filter);
    if (rc) {
        g_string_free(clause, true);
        return rc;
    }

    pho_debug("Executing request: '%s'", clause->str);

    res = PQexec(conn, clause->str);
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        rc = psql_state2errno(res);
        pho_error(rc, "Query '%s' failed: %s", clause->str,
                  PQresultErrorField(res, PG_DIAG_MESSAGE_PRIMARY));
        PQclear(res);
        g_string_free(clause, true);
        return rc;
    }

    g_string_free(clause, true);

    item_size = res_size[type];
    dss_res_size = sizeof(struct dss_result) + PQntuples(res) * item_size;
    dss_res = calloc(1, dss_res_size);
    if (dss_res == NULL) {
        PQclear(res);
        LOG_RETURN(-ENOMEM, "malloc of size %zu failed", dss_res_size);
    }

    dss_res->item_type = type;
    dss_res->pg_res = res;

    for (i = 0; i < PQntuples(res); i++) {
        void *item_ptr = (char *)&dss_res->items.raw + i * item_size;

        rc = res_pg_constructor[type](item_ptr, res, i);
        if (rc)
            goto out;
    }

    *item_list = &dss_res->items.raw;
    *item_cnt = PQntuples(res);

out:
    if (rc)
        /* Only free elements that were initialized, this also frees res */
        dss_res_free(dss_res, i);

    return rc;
}

static int dss_generic_set(struct dss_handle *handle, enum dss_type type,
                           void *item_list, int item_cnt,
                           enum dss_set_action action)
{
    PGconn      *conn = handle->dh_conn;
    GString     *request;
    PGresult    *res = NULL;
    int          error = 0;
    int          rc = 0;
    ENTRY;

    if (conn == NULL || item_list == NULL || item_cnt == 0)
        LOG_RETURN(-EINVAL, "conn: %p, item_list: %p, item_cnt: %d",
                   conn, item_list, item_cnt);

    request = g_string_new("BEGIN;");

    if (action == DSS_SET_INSERT)
        g_string_append(request, insert_query[type]);

    switch (type) {
    case DSS_DEVICE:
        rc = get_device_setrequest(conn, item_list, item_cnt, action, request);
        if (rc)
            LOG_GOTO(out_cleanup, rc, "SQL device request failed");
        break;
    case DSS_MEDIA:
        rc = get_media_setrequest(conn, item_list, item_cnt, action, request);
        if (rc)
            LOG_GOTO(out_cleanup, rc, "SQL media request failed");
        break;
    case DSS_LAYOUT:
        rc = get_layout_setrequest(conn, item_list, item_cnt, action, request,
                                   &error);
        if (rc)
            LOG_GOTO(out_cleanup, rc, "SQL extent request failed");
        break;
    case DSS_OBJECT:
        rc = get_object_setrequest(conn, item_list, item_cnt, action, request);
        if (rc)
            LOG_GOTO(out_cleanup, rc, "SQL object request failed");
        break;

    default:
        LOG_RETURN(-ENOTSUP, "unsupported DSS request type %#x", type);

    }

    if (error)
        LOG_GOTO(out_cleanup, rc = -EINVAL,
                 "JSON parsing failed: %d errors found", error);

    pho_debug("Executing request: '%s'", request->str);

    res = PQexec(conn, request->str);
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        rc = psql_state2errno(res);
        pho_error(rc, "Query '%s' failed: %s (%s)", request->str,
                  PQresultErrorField(res, PG_DIAG_MESSAGE_PRIMARY),
                  PQresultErrorField(res, PG_DIAG_SQLSTATE));
        PQclear(res);

        pho_info("Attempting to rollback after transaction failure");

        res = PQexec(conn, "ROLLBACK; ");
        if (PQresultStatus(res) != PGRES_COMMAND_OK)
            pho_error(rc, "Rollback failed: %s",
                      PQresultErrorField(res, PG_DIAG_MESSAGE_PRIMARY));

        goto out_cleanup;
    }
    PQclear(res);

    res = PQexec(conn, "COMMIT; ");
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        rc = psql_state2errno(res);
        pho_error(rc, "Request failed: %s",
                  PQresultErrorField(res, PG_DIAG_MESSAGE_PRIMARY));
    }

out_cleanup:
    PQclear(res);
    g_string_free(request, true);
    return rc;
}

static int dss_generic_lock(struct dss_handle *handle, enum dss_type type,
                            void *item_list, int item_cnt,
                            const char *lock_owner)
{
    PGconn      *conn = handle->dh_conn;
    GString     *ids;
    GString     *request;
    PGresult    *res = NULL;
    char        *lock_owner_sql;
    int          rc = 0;
    ENTRY;

    if (conn == NULL || item_list == NULL || item_cnt == 0
            || lock_owner == NULL)
        LOG_RETURN(-EINVAL,
                   "conn: %p, item_list: %p, item_cnt: %d, lock_owner=%s",
                   conn, item_list, item_cnt, lock_owner);

    if (strlen(lock_owner) > PHO_DSS_MAX_LOCK_OWNER_LEN)
        LOG_RETURN(-EINVAL,
                   "lock_owner must be at most %d characters long "
                   "(lock_owner: %s)",
                   PHO_DSS_MAX_LOCK_OWNER_LEN, lock_owner);

    ids = g_string_new("");
    request = g_string_new("");

    rc = dss_build_uid_list(conn, item_list, item_cnt, type, ids);
    if (rc)
        LOG_GOTO(out_cleanup, rc, "Ids list build failed");

    lock_owner_sql = dss_char4sql(conn, lock_owner);
    if (!lock_owner_sql)
        GOTO(out_cleanup, rc = -ENOMEM);

    if (item_cnt == 1)
        g_string_printf(request, simple_lock_query[DSS_LOCK_QUERY],
                        dss_type2str(type), lock_owner_sql, ids->str);
    else
        g_string_printf(request, lock_query[DSS_LOCK_QUERY], dss_type2str(type),
                        lock_owner_sql, ids->str, item_cnt,
                        dss_type2str(type),  ids->str);
    free(lock_owner_sql);

    pho_debug("Executing request: '%s'", request->str);

    res = PQexec(conn, request->str);
    if (PQresultStatus(res) != PGRES_COMMAND_OK)
            LOG_GOTO(out_cleanup, rc = psql_state2errno(res),
                     "Request failed: %s",
                     PQresultErrorField(res, PG_DIAG_MESSAGE_PRIMARY));

    if (atoi(PQcmdTuples(res)) != item_cnt)
        rc = -EEXIST;

out_cleanup:
    PQclear(res);
    g_string_free(request, true);
    g_string_free(ids, true);
    return rc;
}

static int dss_generic_unlock(struct dss_handle *handle, enum dss_type type,
                              void *item_list, int item_cnt,
                              const char *lock_owner)
{
    PGconn      *conn = handle->dh_conn;
    GString     *ids;
    GString     *request;
    PGresult    *res = NULL;
    int          rc = 0;
    ENTRY;

    if (conn == NULL || item_list == NULL || item_cnt == 0)
        LOG_RETURN(-EINVAL, "dss - conn: %p, item_list: %p,item_cnt: %d",
                   conn, item_list, item_cnt);

    ids = g_string_new("");
    request = g_string_new("");

    rc = dss_build_uid_list(conn, item_list, item_cnt, type, ids);
    if (rc)
        LOG_GOTO(out_cleanup, rc, "Ids list build failed");

    if (lock_owner != NULL) {
        char *lock_owner_sql = dss_char4sql(conn, lock_owner);

        if (!lock_owner_sql)
            GOTO(out_cleanup, rc = -ENOMEM);

        if (item_cnt == 1)
            g_string_printf(request, simple_lock_query[DSS_UNLOCK_QUERY],
                            dss_type2str(type), ids->str, lock_owner_sql);
        else
            g_string_printf(request, lock_query[DSS_UNLOCK_QUERY],
                            dss_type2str(type), ids->str, lock_owner_sql,
                            item_cnt, dss_type2str(type), ids->str,
                            lock_owner_sql);
        free(lock_owner_sql);
    } else {
        if (item_cnt == 1)
            g_string_printf(request, simple_lock_query[DSS_UNLOCK_ALWAYS_QUERY],
                            dss_type2str(type), ids->str);
        else
            g_string_printf(request, lock_query[DSS_UNLOCK_ALWAYS_QUERY],
                            dss_type2str(type), ids->str, item_cnt,
                            dss_type2str(type), ids->str);
    }

    pho_debug("Executing request: '%s'", request->str);

    res = PQexec(conn, request->str);
    if (PQresultStatus(res) != PGRES_COMMAND_OK)
        LOG_GOTO(out_cleanup, rc = psql_state2errno(res),
                "Request failed: %s",
                PQresultErrorField(res, PG_DIAG_MESSAGE_PRIMARY));

    if (atoi(PQcmdTuples(res)) != item_cnt)
        /* lock is not owned by caller */
        rc = -ENOLCK;

out_cleanup:
    PQclear(res);
    g_string_free(request, true);
    g_string_free(ids, true);
    return rc;
}

void dss_res_free(void *item_list, int item_cnt)
{
    struct dss_result *dss_res;
    res_destructor_t dtor;
    size_t item_size;
    int i;

    if (!item_list)
        return;

    dss_res = res_of_item_list(item_list);
    item_size = res_size[dss_res->item_type];
    dtor = res_destructor[dss_res->item_type];

    for (i = 0; i < item_cnt; i++)
        dtor((char *)&dss_res->items.raw + i * item_size);

    PQclear(dss_res->pg_res);
    free(dss_res);
}

int dss_device_get(struct dss_handle *hdl, const struct dss_filter *filter,
                   struct dev_info **dev_ls, int *dev_cnt)
{
    return dss_generic_get(hdl, DSS_DEVICE, filter, (void **)dev_ls, dev_cnt);
}

int dss_media_get(struct dss_handle *hdl, const struct dss_filter *filter,
                  struct media_info **med_ls, int *med_cnt)
{
    return dss_generic_get(hdl, DSS_MEDIA, filter, (void **)med_ls, med_cnt);
}

int dss_layout_get(struct dss_handle *hdl, const struct dss_filter *filter,
                   struct layout_info **lyt_ls, int *lyt_cnt)
{
    return dss_generic_get(hdl, DSS_LAYOUT, filter, (void **)lyt_ls, lyt_cnt);
}

int dss_object_get(struct dss_handle *hdl, const struct dss_filter *filter,
                   struct object_info **obj_ls, int *obj_cnt)
{
    return dss_generic_get(hdl, DSS_OBJECT, filter, (void **)obj_ls, obj_cnt);
}

int dss_device_set(struct dss_handle *hdl, struct dev_info *dev_ls, int dev_cnt,
                   enum dss_set_action action)
{
    return dss_generic_set(hdl, DSS_DEVICE, (void *)dev_ls, dev_cnt, action);
}

int dss_media_set(struct dss_handle *hdl, struct media_info *med_ls,
                  int med_cnt, enum dss_set_action action)
{
    return dss_generic_set(hdl, DSS_MEDIA, (void *)med_ls, med_cnt, action);
}

int dss_layout_set(struct dss_handle *hdl, struct layout_info *lyt_ls,
                   int lyt_cnt, enum dss_set_action action)
{
    return dss_generic_set(hdl, DSS_LAYOUT, (void *)lyt_ls, lyt_cnt, action);
}

int dss_object_set(struct dss_handle *hdl, struct object_info *obj_ls,
                   int obj_cnt, enum dss_set_action action)
{
    return dss_generic_set(hdl, DSS_OBJECT, (void *)obj_ls, obj_cnt, action);
}

int dss_device_lock(struct dss_handle *handle, struct dev_info *dev_ls,
                    int dev_cnt, const char *lock_owner)
{
    return dss_generic_lock(handle, DSS_DEVICE, dev_ls, dev_cnt, lock_owner);
}

int dss_device_unlock(struct dss_handle *handle, struct dev_info *dev_ls,
                      int dev_cnt, const char *lock_owner)
{
    return dss_generic_unlock(handle, DSS_DEVICE, dev_ls, dev_cnt, lock_owner);
}

int dss_media_lock(struct dss_handle *handle, struct media_info *media_ls,
                   int media_cnt, const char *lock_owner)
{
    return dss_generic_lock(handle, DSS_MEDIA, media_ls, media_cnt, lock_owner);
}

int dss_media_unlock(struct dss_handle *handle, struct media_info *media_ls,
                     int media_cnt, const char *lock_owner)
{
    return dss_generic_unlock(handle, DSS_MEDIA, media_ls, media_cnt,
                              lock_owner);
}
