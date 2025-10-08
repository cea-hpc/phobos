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
 * \brief  Phobos Distributed State Service API for utilities.
 */
#ifndef _PHO_DSS_UTILS_H
#define _PHO_DSS_UTILS_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdbool.h>
#include <glib.h>
#include <jansson.h>
#include <libpq-fe.h>

#include "pho_type_utils.h"
#include "pho_types.h"
#include "pho_dss.h"

#define INSERT_OBJECT (1 << 0)
#define INSERT_FULL_OBJECT (1 << 1)

/**
 * Escape a string for use in a database query.
 *
 * The escape is done using PSQL's "PQescapeLiteral" function.
 * If \p is NULL or empty, return the string "NULL".
 *
 * \param[in] conn  The connection to the database
 * \param[in] s     The string to escape, can be NULL or empty
 *
 * \return NULL on error, the string "NULL" if \p s is NULL or empty
 *                        the escaped string otherwise
 */
char *dss_char4sql(PGconn *conn, const char *s);

/**
 * Free a string that was escaped using "dss_char4sql".
 *
 * If \p s is NULL or the string "NULL", does nothing.
 *
 * \param[in] s     The string to free, can be NULL or the string "NULL"
 */
void free_dss_char4sql(char *s);

/**
 * Convert dss_sort structure to a SQL query.
 *
 *  If \p sort is NULL, does nothing.
 *
 * \param request[in/out]
 * \param sort[in]
 */
void dss_sort2sql(GString *request, struct dss_sort *sort);

/**
 * Execute a PSQL \p request, verify the result is as expected with \p tested
 * and put the result in \p res.
 *
 * \param conn[in]    The connection to the database
 * \param request[in] Request to execute
 * \param res[out]    Result holder of the request
 * \param tested[in]  The expected result of the request
 *
 * \return            0 on success, or the error as returned by PSQL
 */
int execute(PGconn *conn, const char *request, PGresult **res,
            ExecStatusType tested);

/**
 * Convert PostgreSQL status codes to meaningful errno values.
 * \param   res[in]         Failed query result descriptor
 * \return                  Negated errno value corresponding to the error
 */
int psql_state2errno(const PGresult *res);

/**
 * Execute a PSQL \p request, verify the result is as expected with \p tested,
 * and put the result in \p res if not NULL.
 *
 * In case the request failed, a 'rollback' request is sent to the database.
 *
 * \param conn[in]      The connection to the database
 * \param request[in]   Request to execute
 * \param res[out]      If not NULL, result of the request
 * \param tested[in]    The expected result of the request
 *
 * \return              0 on success, or the error as returned by PSQL
 */
int execute_and_commit_or_rollback(PGconn *conn, GString *request,
                                   PGresult **res, ExecStatusType tested);

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

struct dss_field {
    int byte_value;
    const char *query_value;
    const char *(*get_value)(void *resource);
};

static inline const char *get_access_time(void *copy)
{
    static __thread char str[PHO_TIMEVAL_MAX_LEN] = "";

    timeval2str(&((struct copy_info *) copy)->access_time, str);

    return (const char *)str;
}

static inline const char *get_copy_status(void *copy)
{
    return copy_status2str(((struct copy_info *) copy)->copy_status);
}

static inline const char *get_oid(void *object)
{
    return ((struct object_info *) object)->oid;
}

void update_fields(void *resource, int64_t fields_to_update,
                   struct dss_field *fields, int fields_count,
                   GString *request);

/**
 * Retrieve a string contained in a JSON object under a given key.
 *
 * The result string should not be kept in a data structure. To keep a
 * copy of the targeted string, use json_dict2str() instead.
 *
 * \return The targeted string value on success, or NULL on error.
 */
const char *json_dict2tmp_str(const struct json_t *obj, const char *key);

/**
 * Retrieve a copy of a string contained in a JSON object under a given key.
 * The caller is responsible for freeing the result after use.
 *
 * \return a newly allocated copy of the string on success or NULL on error.
 */
char *json_dict2str(const struct json_t *obj, const char *key);

/**
 * Retrieve a signed but positive integer contained in a JSON object under a
 * given key.
 * An error is returned if no integer was found at this location.
 *
 * \retval the value on success and -1 on error.
 */
int json_dict2int(const struct json_t *obj, const char *key);

/**
 * Retrieve a signed but positive long long integer contained in a JSON object
 * under a given key.
 * An error is returned if no long long integer was found at this location.
 *
 * \retval the value on success and -1LL on error.
 */
long long json_dict2ll(const struct json_t *obj, const char *key);

#define JSON_INTEGER_SET_NEW(_j, _s, _f)                        \
    do {                                                        \
        json_t  *_tmp = json_integer((_s)._f);                  \
        if (!_tmp)                                              \
            pho_error(-ENOMEM, "Failed to encode '%s'", #_f);   \
        else                                                    \
            json_object_set_new((_j), #_f, _tmp);               \
    } while (0)

static inline const char *bool2sqlbool(bool b)
{
    return b ? "TRUE" : "FALSE";
}

static inline bool psqlstrbool2bool(char psql_str_bool)
{
    return psql_str_bool == 't';
}

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

typedef int (*cmp_func)(void *, void *);

/**
 * Sort the list in ascending or descending order.
 *
 * \param list[in/out]      The data to sort
 * \param low[in]           First index of list
 * \param high[out]         Last index of list
 * \param item_size[in]     The size of elements in list
 * \param reverse[in]       Boolean to indicate the sorting direction
 * \param func[in]          Comparison function used
 */
void
quicksort(void **list, int low, int high, size_t item_size, bool reverse,
          cmp_func func);

/**
 * Comparison function to compare the size of extents, the size is the sum of
 * all the extents's size
 *
 * \param first_extent[in]      The first extent
 * \param second_extent[in]     The second extent
 *
 * \return -1, 0 or 1 if the size of first_extent is smaller, equal of bigger
 *  than the size of second_extent
 */
int
cmp_size(void *first_extent, void *second_extent);

#endif
