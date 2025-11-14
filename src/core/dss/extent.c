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
 * \brief  Extent resource file of Phobos's Distributed State Service.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gmodule.h>
#include <libpq-fe.h>

#include "extent.h"
#include "dss_utils.h"

/**
 * Encode a buffer in hexadecimal notation
 *
 * @param[out] output  Encoded output buffer
 * @param[in]  input   Buffer to encode
 * @param[in]  size    Size in bytes of the input buffer
 */
static void encode_hex_buffer(char *output, const unsigned char *input,
                              size_t size)
{
    int j, k;

    for (j = 0, k = 0; j < size; j++, k += 2)
        sprintf(output + k, "%02x", input[j]);
}

/**
 * Encode the MD5 and XXH128 hashes as a json from a given \p extent.
 *
 * \param[in] extent      The extent with the MD5 and XXH128 to encode
 *
 * \return The json representation of the hashes on success (should be free by
 *         the caller)
 *         NULL on error
 */
static char *dss_extent_hash_encode(struct extent *extent)
{
    char *result = NULL;
    json_t *root;
    int rc = 0;

    ENTRY;

    root = json_object();
    if (!root) {
        pho_error(-ENOMEM, "Failed to create json object");
        return NULL;
    }

    if (extent->with_md5) {
        char buf[64];

        encode_hex_buffer(buf, extent->md5, sizeof(extent->md5));
        rc = json_object_set_new(root, PHO_HASH_MD5_KEY_NAME,
                                 json_string(buf));
        if (rc)
            LOG_GOTO(out_free, rc = -EINVAL, "Cannot set md5");
    }

    if (extent->with_xxh128) {
        char buf[64];

        encode_hex_buffer(buf, extent->xxh128, sizeof(extent->xxh128));
        rc = json_object_set_new(root, PHO_HASH_XXH128_KEY_NAME,
                                 json_string(buf));
        if (rc)
            LOG_GOTO(out_free, rc = -EINVAL, "Cannot set xxh128");
    }

    result = json_dumps(root, 0);

    pho_debug("Created json representation for hash: '%s'",
              result ? result : "(null)");

out_free:
    json_decref(root);

    if (rc) {
        free(result);
        return NULL;
    }

    return result;
}

static int extent_insert_query(PGconn *conn, void *void_extent, int item_cnt,
                               int64_t fields, GString *request)
{
    if (fields & INSERT_OBJECT)
        g_string_append(
            request,
            "INSERT INTO extent (extent_uuid, state, size, offsetof, "
            "medium_family, medium_id, medium_library, address, hash, info) "
            "VALUES "
        );
    else
        g_string_append(
            request,
            "INSERT INTO extent (extent_uuid, state, size, offsetof, "
            "medium_family, medium_id, medium_library, address, hash, info, "
            "creation_time) VALUES "
        );

    for (int i = 0; i < item_cnt; ++i) {
        struct extent *extent = ((struct extent *) void_extent) + i;
        GString *info;
        char *hash;

        info = g_string_new("");
        pho_attrs_to_json(&extent->info, info, JSON_COMPACT);

        hash = dss_extent_hash_encode(extent);
        if (hash == NULL) {
            g_string_free(info, TRUE);
            return -EINVAL;
        }

        if (fields & INSERT_OBJECT) {
            g_string_append_printf(request,
                                   "('%s', '%s', %ld, %ld, '%s', '%s', '%s', "
                                   "'%s', '%s', '%s')",
                                   extent->uuid,
                                   extent_state2str(extent->state),
                                   extent->size, extent->offset,
                                   rsc_family2str(extent->media.family),
                                   extent->media.name, extent->media.library,
                                   extent->address.buff, hash, info->str);
        } else {
            char creation_time_str[PHO_TIMEVAL_MAX_LEN] = "";

            timeval2str(&extent->creation_time, creation_time_str);
            g_string_append_printf(request,
                                   "('%s', '%s', %ld, %ld, '%s', '%s', '%s', "
                                   "'%s', '%s', '%s', '%s')",
                                   extent->uuid,
                                   extent_state2str(extent->state),
                                   extent->size, extent->offset,
                                   rsc_family2str(extent->media.family),
                                   extent->media.name, extent->media.library,
                                   extent->address.buff, hash, info->str,
                                   creation_time_str);
        }

        if (i < item_cnt - 1)
            g_string_append(request, ", ");

        g_string_free(info, TRUE);
        free(hash);
    }

    g_string_append(request, ";");

    return 0;
}

static int extent_update_query(PGconn *conn, void *src_extent, void *dst_extent,
                               int item_cnt, int64_t fields, GString *request)
{
    (void) fields;
    (void) conn;

    for (int i = 0; i < item_cnt; ++i) {
        struct extent *src = ((struct extent *) src_extent) + i;
        struct extent *dst = ((struct extent *) dst_extent) + i;

        g_string_append_printf(
            request,
            "UPDATE extent SET state = '%s', medium_family = '%s', "
            "medium_id = '%s', medium_library = '%s', address = '%s' "
            "WHERE extent_uuid = '%s';",
            extent_state2str(dst->state),
            rsc_family2str(dst->media.family),
            dst->media.name,
            dst->media.library,
            dst->address.buff,
            src->uuid
        );
    }

    return 0;
}

static int extent_select_query(GString **conditions, int n_conditions,
                               GString *request, struct dss_sort *sort)
{
    g_string_append(request,
                    "SELECT extent_uuid, size, offsetof, medium_family, state,"
                    "medium_id, medium_library, address, hash, info, "
                    "creation_time FROM "
                    "extent");

    if (n_conditions == 1)
        g_string_append(request, conditions[0]->str);
    else if (n_conditions >= 2)
        return -ENOTSUP;

    g_string_append(request, ";");

    return 0;
}

static int extent_delete_query(void *void_extent, int item_cnt,
                               GString *request)
{
    for (int i = 0; i < item_cnt; ++i) {
        struct extent *extent = ((struct extent *) void_extent) + i;

        g_string_append_printf(request,
                               "DELETE FROM extent WHERE extent_uuid = '%s';",
                               extent->uuid);
    }

    return 0;
}

/**
 * Read size bytes into digest from hexbuf buffer of hexadecimal characters
 *
 * @param[in,out]   digest  digest to fill
 * @param[in]       size    number of byte to fill in digest
 * @param[in]       hexbuf  buffer of at least 2*size hexadecimal characters
 *
 * @return 0 on success, negative error code on failure.
 */
static int read_hex_buffer(unsigned char *digest, size_t size,
                           const char *hexbuf)
{
    size_t i;
    int rc;

    for (i = 0; i < size; i++) {
        rc = sscanf(hexbuf + 2 * i, "%02hhx", &digest[i]);
        if (rc != 1) {
            rc = -errno;
            memset(&digest[0], 0, size);
            return rc;
        }
    }

    return 0;
}

int dss_extent_hash_decode(struct extent *extent, json_t *hash_field)
{
    const char *tmp;
    int rc = 0;

    ENTRY;

    if (!json_is_object(hash_field))
        LOG_RETURN(rc = -EINVAL, "Invalid JSON hash");

    tmp = json_dict2tmp_str(hash_field, "xxh128");
    if (tmp) {
        rc = read_hex_buffer(extent->xxh128, sizeof(extent->xxh128), tmp);
        if (rc)
            LOG_RETURN(rc, "Failed to decode xxh128 extent");
    }
    extent->with_xxh128 = (tmp != NULL);

    tmp = json_dict2tmp_str(hash_field, "md5");
    if (tmp) {
        rc = read_hex_buffer(extent->md5, sizeof(extent->md5), tmp);
        if (rc)
            LOG_RETURN(rc, "Failed to decode md5 extent");
    }
    extent->with_md5 = (tmp != NULL);

    return rc;
}

static int extent_from_pg_row(struct dss_handle *handle, void *void_extent,
                              PGresult *res, int row_num)
{
    struct extent *extent = void_extent;
    json_error_t json_error;
    json_t *root;
    int rc;

    (void) handle;

    extent->uuid = get_str_value(res, row_num, 0);
    extent->layout_idx = -1;
    extent->size = atoll(PQgetvalue(res, row_num, 1));
    extent->offset = atoll(PQgetvalue(res, row_num, 2));
    extent->media.family = str2rsc_family(PQgetvalue(res, row_num, 3));
    extent->state = str2extent_state(PQgetvalue(res, row_num, 4));
    pho_id_name_set(&extent->media, PQgetvalue(res, row_num, 5),
                    PQgetvalue(res, row_num, 6));
    extent->address.buff = get_str_value(res, row_num, 7);
    extent->address.size = strlen(extent->address.buff) + 1;

    root = json_loads(PQgetvalue(res, row_num, 8), JSON_REJECT_DUPLICATES,
                      &json_error);
    if (!root)
        LOG_RETURN(-EINVAL, "Failed to parse json data for hash values: %s",
                   json_error.text);

    rc = dss_extent_hash_decode(extent, root);
    json_decref(root);
    if (rc)
        return rc;

    rc = pho_json_to_attrs(&extent->info, PQgetvalue(res, row_num, 9));
    if (rc)
        LOG_RETURN(rc, "Failed to parse json data for extra attrs: %s",
                   json_error.text);

    rc = str2timeval(get_str_value(res, row_num, 10), &extent->creation_time);

    return 0;
}

static void extent_result_free(void *void_extent)
{
    struct extent *extent = void_extent;

    pho_attrs_free(&extent->info);
}

const struct dss_resource_ops extent_ops = {
    .insert_query = extent_insert_query,
    .update_query = extent_update_query,
    .select_query = extent_select_query,
    .delete_query = extent_delete_query,
    .create       = extent_from_pg_row,
    .free         = extent_result_free,
    .size         = sizeof(struct extent),
};
