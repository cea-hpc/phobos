/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2022 CEA/DAM.
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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "dss_utils.h"
#include "pho_common.h"

#include <errno.h>
#include <libpq-fe.h>

struct sqlerr_map_item {
    const char *smi_prefix;  /**< SQL error code or class (prefix) */
    int         smi_errcode; /**< Corresponding negated errno code */
};

/**
 * Map errors from SQL to closest errno.
 * The list is traversed from top to bottom and stops at first match, so make
 * sure that new items are inserted in most-specific first order.
 * See: http://www.postgresql.org/docs/9.4/static/errcodes-appendix.html
 */
static const struct sqlerr_map_item sqlerr_map[] = {
    /* Class 00 - Succesful completion */
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
    /* Class PH - Phobos custom errors */
    {"PHLK1", -ENOLCK},
    {"PHLK2", -EACCES},
    /* Catch all -- KEEP LAST -- */
    {"", -ECOMM}
};

int execute(PGconn *conn, const char *request, PGresult **res,
            ExecStatusType tested)
{
    pho_debug("Executing request: '%s'", request);

    *res = PQexec(conn, request);
    if (PQresultStatus(*res) != tested)
        LOG_RETURN(psql_state2errno(*res), "Request failed: %s",
                   PQresultErrorField(*res, PG_DIAG_MESSAGE_PRIMARY));

    return 0;
}

int psql_state2errno(const PGresult *res)
{
    char *sqlstate = PQresultErrorField(res, PG_DIAG_SQLSTATE);
    int i;

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

int execute_and_commit_or_rollback(PGconn *conn, GString *request,
                                   PGresult **res, ExecStatusType tested)
{
    PGresult *tmp_res = NULL;
    int rc = 0;

    if (res) {
        rc = execute(conn, request->str, res, tested);
    } else {
        rc = execute(conn, request->str, &tmp_res, tested);
        PQclear(tmp_res);
    }

    if (rc) {
        if (res)
            PQclear(*res);

        pho_info("Attempting to rollback after transaction failure");

        execute(conn, "ROLLBACK;", &tmp_res, PGRES_COMMAND_OK);

        goto cleanup;
    }

    rc = execute(conn, "COMMIT;", &tmp_res, PGRES_COMMAND_OK);

cleanup:
    PQclear(tmp_res);
    return rc;
}

void update_fields(void *resource, int64_t fields_to_update,
                   struct dss_field *fields, int fields_count, GString *request)
{
    for (int j = 0; j < fields_count; ++j) {
        struct dss_field *field = &fields[j];

        if (fields_to_update & field->byte_value) {

            g_string_append_printf(request, field->query_value,
                                   field->get_value(resource));
            fields_to_update ^= field->byte_value;
            if (fields_to_update != 0)
                g_string_append(request, ",");
        }
    }
}

const char *json_dict2tmp_str(const struct json_t *obj, const char *key)
{
    struct json_t *current_obj;

    current_obj = json_object_get(obj, key);
    if (!current_obj) {
        pho_debug("Cannot retrieve object '%s'", key);
        return NULL;
    }

    return json_string_value(current_obj);
}

char *json_dict2str(const struct json_t *obj, const char *key)
{
    const char *res = json_dict2tmp_str(obj, key);

    return xstrdup_safe(res);
}

int json_dict2int(const struct json_t *obj, const char *key)
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

long long json_dict2ll(const struct json_t *obj, const char *key)
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

char *dss_char4sql(PGconn *conn, const char *s)
{
    char *ns;

    if (s == NULL || s[0] == '\0')
        return "NULL";

    ns = PQescapeLiteral(conn, s, strlen(s));
    if (ns == NULL)
        pho_error(EINVAL,
                  "Cannot escape litteral %s: %s", s, PQerrorMessage(conn));

    return ns;
}

void free_dss_char4sql(char *s)
{
    if (s != NULL && strcmp(s, "NULL"))
        PQfreemem(s);
}

void dss_sort2sql(GString *request, struct dss_sort *sort)
{
    if (sort != NULL && sort->psql_sort == true) {
        g_string_append(request, " ORDER BY ");
        g_string_append(request, sort->attr);
        if (sort->reverse)
            g_string_append(request, " DESC ");
    }
}

static size_t
compute_size(void *item)
{
    struct layout_info *layout = (struct layout_info *) item;
    size_t size = 0;

    for (int i = 0; i < layout->ext_count; i++)
        size += layout->extents[i].size;
    return size;
}

int
cmp_size(void *first_extent, void *second_extent)
{
    size_t second_size = compute_size(second_extent);
    size_t first_size = compute_size(first_extent);

    if (first_size < second_size)
        return -1;
    if (first_size > second_size)
        return 1;

    return 0;
}

static void
swap_list(void **list, int a, int b, size_t item_size)
{
    void *buffer = xmalloc(item_size);

    memcpy(buffer, *list + a * item_size, item_size);
    memcpy(*list + a * item_size, *list + b * item_size, item_size);
    memcpy(*list + b * item_size, buffer, item_size);
    free(buffer);
}

static int
partition(void **list, int low, int high, size_t item_size, bool reverse,
          cmp_func func)
{
    void *pivot_item = *list + high * item_size;
    void *j_item;
    int i = low;
    int rc;

    for (int j = low; j <= high - 1; j++) {
        j_item = *list + j * item_size;
        rc = func(j_item, pivot_item);
        if ((rc == -1 && !reverse) || (rc == 1 && reverse)) {
            swap_list(list, i, j, item_size);
            i++;
        }
    }
    swap_list(list, i, high, item_size);
    return i;
}

void
quicksort(void **list, int low, int high, size_t item_size, bool reverse,
          cmp_func func)
{
    if (low >= high || low < 0)
        return;

    int partitionIdx = partition(list, low, high, item_size, reverse, func);

    quicksort(list, low, partitionIdx - 1, item_size, reverse, func);
    quicksort(list, partitionIdx + 1, high, item_size, reverse, func);
}
