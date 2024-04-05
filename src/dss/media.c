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
 * \brief  Media resource file of Phobos's Distributed State Service.
 */

#include <libpq-fe.h>
#include <jansson.h>

#include "pho_type_utils.h"

#include "dss_config.h"
#include "dss_utils.h"
#include "logs.h"
#include "media.h"
#include "resources.h"

/**
 * Encode media statistics to json
 *
 * \param[in]  stats  media stats to be encoded
 *
 * \return Return a string json object
 */
static char *dss_media_stats_encode(struct media_stats stats)
{
    char    *res = NULL;
    json_t  *root;

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
    JSON_INTEGER_SET_NEW(root, stats, nb_load);
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
 * Decode the stats of a medium from a given \p json.
 *
 * \param[out] stats  The stats in which to store the decoded JSON values
 * \param[in]  json   The JSON string to decode
 *
 * \return 0 on success, -EINVAL if \p json is not a valid JSON object
 *                       negative error code otherwise
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
    LOAD_CHECK32(rc, root, stats, nb_load, true);
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

    tags->tags = xcalloc(tags->n_tags, sizeof(*tags->tags));
    for (i = 0; i < tags->n_tags; i++) {
        array_entry = json_array_get(tag_array, i);
        tag = json_string_value(array_entry);
        if (tag) {
            tags->tags[i] = xstrdup(tag);
        } else {
            /* Fallback to empty string to avoid unexpected NULL */
            tags->tags[i] = xstrdup("");
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

static int media_insert_query(PGconn *conn, void *void_med, int item_cnt,
                              int64_t fields, GString *request)
{
    (void) fields;

    g_string_append(
        request,
        "INSERT INTO media (family, model, id, adm_status, fs_type, "
                           "address_type, fs_status, fs_label, stats, tags, "
                           "put, get, delete) VALUES "
    );

    for (int i = 0; i < item_cnt; ++i) {
        struct media_info *medium = ((struct media_info *) void_med) + i;
        GString *sub_request = g_string_new(NULL);
        char *medium_name = NULL;
        char *tmp_stats = NULL;
        char *fs_label = NULL;
        char *tmp_tags = NULL;
        char *model = NULL;
        char *stats = NULL;
        char *tags = NULL;

        /* check tape model validity */
        if (medium->rsc.id.family == PHO_RSC_TAPE &&
            !dss_tape_model_check(medium->rsc.model))
            LOG_RETURN(-EINVAL, "invalid media tape model '%s'",
                       medium->rsc.model);

        medium_name = dss_char4sql(conn, medium->rsc.id.name);
        if (medium_name == NULL)
            goto free_info;

        fs_label = dss_char4sql(conn, medium->fs.label);
        if (fs_label == NULL)
            goto free_info;

        model = dss_char4sql(conn, medium->rsc.model);
        if (model == NULL)
            goto free_info;

        tmp_stats = dss_media_stats_encode(medium->stats);
        if (tmp_stats == NULL)
            goto free_info;

        stats = dss_char4sql(conn, tmp_stats);
        if (stats == NULL)
            goto free_info;

        tmp_tags = dss_tags_encode(&medium->tags);
        if (tmp_tags == NULL)
            goto free_info;

        tags = dss_char4sql(conn, tmp_tags);
        if (tags == NULL)
            goto free_info;

        g_string_append_printf(
            sub_request,
            "('%s', %s, %s, '%s', '%s', '%s', '%s', '%s', %s, %s, %s, %s, %s)",
            rsc_family2str(medium->rsc.id.family),
            model,
            medium_name,
            rsc_adm_status2str(medium->rsc.adm_status),
            fs_type2str(medium->fs.type),
            address_type2str(medium->addr_type),
            fs_status2str(medium->fs.status),
            fs_label,
            stats,
            tags,
            bool2sqlbool(medium->flags.put),
            bool2sqlbool(medium->flags.get),
            bool2sqlbool(medium->flags.delete)
        );

        g_string_append(request, sub_request->str);
        if (i < item_cnt - 1)
            g_string_append(request, ", ");

free_info:
        free_dss_char4sql(medium_name);
        free_dss_char4sql(fs_label);
        free_dss_char4sql(model);
        free(stats);
        free(tags);
        free(tmp_stats);
        free(tmp_tags);
    }

    g_string_append(request, ";");

    return 0;
}

/**
 * Append the strings \p field and \p field_value to the GString \p request,
 * and adds an additional comma if there are other fields that will be inserted
 * later.
 *
 * \param[out] request      The request in which to append the key/values and
 *                          comma
 * \param[in]  field        The string to insert, should contain a '%s' for a
 *                          printf call
 * \param[in]  field_value  The string that will replace the format identifier
 *                          '%s' of \p field
 * \param[in]  add_comma    Whether a comma and space should be added in \p
 *                          request
 */
static void append_update_request(GString *request, const char *field,
                                  const char *field_value,
                                  bool add_comma)
{
    g_string_append_printf(request, field, field_value);
    if (add_comma)
        g_string_append(request, ", ");
}

/**
 * Append an escaped label update to the GString \p request.
 *
 * \param[in]  conn       The connection to the database, used to escape the
 *                        label
 * \param[out] request    The request in which to add the label update
 * \param[in]  medium     The medium to extract the label from
 * \param[in]  add_comma  Whether a comma and space should be added in \p
 *                        request
 *
 * \return 0 on success, -EINVAL if the label escaping fails
 */
static int append_label_update_request(PGconn *conn, GString *request,
                                       struct media_info *medium,
                                       bool add_comma)
{
    char *fs_label = NULL;

    fs_label = dss_char4sql(conn, medium->fs.label);
    if (!fs_label)
        LOG_RETURN(-EINVAL,
                   "Failed to build FS_LABEL (%s) media update SQL "
                   "request", medium->fs.label);

    append_update_request(request, "fs_label = %s", fs_label, add_comma);
    free_dss_char4sql(fs_label);

    return 0;
}

/**
 * Append an escaped tags update to the GString \p request.
 *
 * \param[in]  conn       The connection to the database, used to escape the
 *                        tags
 * \param[out] request    The request in which to add the tags update
 * \param[in]  medium     The medium to extract the tags from
 * \param[in]  add_comma  Whether a comma and space should be added in \p
 *                        request
 *
 * \return 0 on success, -EINVAL if the tags encoding or escaping fail
 */
static int append_tags_update_request(PGconn *conn, GString *request,
                                      struct media_info *medium, bool add_comma)
{
    char *tmp_tags = NULL;
    char *tags = NULL;

    tmp_tags = dss_tags_encode(&medium->tags);
    if (!tmp_tags)
        LOG_RETURN(-EINVAL,
                   "Failed to encode tags for media update");

    tags = dss_char4sql(conn, tmp_tags);
    free(tmp_tags);

    if (!tags)
        LOG_RETURN(-EINVAL,
                   "Failed to build tags media update SQL request");

    append_update_request(request, "tags = %s", tags, add_comma);
    free_dss_char4sql(tags);

    return 0;
}

/**
 * Append an escaped stats string update to the GString \p request.
 *
 * \param[in]  conn       The connection to the database, used to escape the
 *                        stats
 * \param[out] request    The request in which to add the stats update
 * \param[in]  medium     The medium to extract the stats from
 * \param[in]  add_comma  Whether a comma and space should be added in \p
 *                        request
 *
 * \return 0 on success, -EINVAL if the stats encoding or escaping fail
 */
static int append_stat_update_request(PGconn *conn, GString *request,
                                      struct media_info *medium, bool add_comma)
{
    char *tmp_stats = NULL;
    char *stats = NULL;

    tmp_stats = dss_media_stats_encode(medium->stats);
    if (!tmp_stats)
        LOG_RETURN(-EINVAL, "Failed to encode stats for media update");

    stats = dss_char4sql(conn, tmp_stats);
    free(tmp_stats);

    if (!stats)
        LOG_RETURN(-EINVAL,
                   "Failed to build stats media update SQL request");

    append_update_request(request, "stats = %s", stats, add_comma);
    free_dss_char4sql(stats);

    return 0;
}

// XXX: feels like updates could be managed the same as filters, with the caller
// specifying what to update and the value directly
static int media_update_query(PGconn *conn, void *void_med, int item_cnt,
                              int64_t update_fields, GString *request)
{
    if (update_fields == 0)
        return -EINVAL;

    for (int i = 0; i < item_cnt; ++i) {
        struct media_info *medium = ((struct media_info *) void_med) + i;
        GString *sub_request = g_string_new(NULL);
        int64_t fields = update_fields;
        int rc;

        g_string_append_printf(sub_request, "UPDATE media SET ");

        /* we check the fields parameter to select updated columns */
        if (ADM_STATUS & fields)
            append_update_request(sub_request,
                                  "adm_status = '%s'",
                                  rsc_adm_status2str(medium->rsc.adm_status),
                                  (fields ^= ADM_STATUS) != 0);

        if (FS_STATUS & fields)
            append_update_request(sub_request,
                                  "fs_status = '%s'",
                                  fs_status2str(medium->fs.status),
                                  (fields ^= FS_STATUS) != 0);

        if (FS_LABEL & fields) {
            rc = append_label_update_request(conn, sub_request, medium,
                                             (fields ^= FS_LABEL) != 0);
            if (rc)
                return rc;
        }

        if (TAGS & fields) {
            rc = append_tags_update_request(conn, sub_request, medium,
                                            (fields ^= TAGS) != 0);
            if (rc)
                return rc;
        }

        if (PUT_ACCESS & fields)
            append_update_request(sub_request,
                                  "put = %s",
                                  bool2sqlbool(medium->flags.put),
                                  (fields ^= PUT_ACCESS) != 0);

        if (GET_ACCESS & fields)
            append_update_request(sub_request,
                                  "get = %s",
                                  bool2sqlbool(medium->flags.get),
                                  (fields ^= GET_ACCESS) != 0);

        if (DELETE_ACCESS & fields)
            append_update_request(sub_request,
                                  "delete = %s",
                                  bool2sqlbool(medium->flags.delete),
                                  (fields ^= DELETE_ACCESS) != 0);

        if (IS_STAT(fields)) {
            rc = append_stat_update_request(conn, sub_request, medium, false);
            if (rc)
                return rc;

            fields = 0;
        }


        g_string_append_printf(sub_request,
                               " WHERE family = '%s' AND id = '%s';",
                               rsc_family2str(medium->rsc.id.family),
                               medium->rsc.id.name);

        g_string_append(request, sub_request->str);
        g_string_free(sub_request, true);
    }

    return 0;
}

static int media_select_query(GString **conditions, int n_conditions,
                              GString *request)
{
    g_string_append(request,
                    "SELECT family, model, id, adm_status, address_type,"
                    " fs_type, fs_status, fs_label, stats, tags, put, get, "
                    " delete FROM media");

    if (n_conditions == 1)
        g_string_append(request, conditions[0]->str);
    else if (n_conditions >= 2)
        return -ENOTSUP;

    g_string_append(request, ";");

    return 0;
}

static int media_delete_query(void *void_med, int item_cnt, GString *request)
{
    for (int i = 0; i < item_cnt; ++i) {
        struct media_info *medium = ((struct media_info *) void_med) + i;

        g_string_append_printf(request, "DELETE FROM media WHERE id = '%s'; ",
                               medium->rsc.id.name);
    }

    return 0;
}

static int media_from_pg_row(struct dss_handle *handle, void *void_media,
                             PGresult *res, int row_num)
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
    memcpy(medium->fs.label, PQgetvalue(res, row_num, 7),
            sizeof(medium->fs.label));
    /* make sure the label is zero-terminated */
    medium->fs.label[sizeof(medium->fs.label) - 1] = '\0';
    medium->flags.put      = psqlstrbool2bool(*PQgetvalue(res, row_num, 10));
    medium->flags.get      = psqlstrbool2bool(*PQgetvalue(res, row_num, 11));
    medium->flags.delete   = psqlstrbool2bool(*PQgetvalue(res, row_num, 12));
    medium->health         = 0;

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

    rc = dss_lock_status(handle, DSS_MEDIA, medium, 1, &medium->lock);
    if (rc == -ENOLCK) {
        medium->lock.hostname = NULL;
        medium->lock.owner = 0;
        medium->lock.timestamp.tv_sec = 0;
        medium->lock.timestamp.tv_usec = 0;
        rc = 0;
    }

    return rc;
}

static void media_result_free(void *void_media)
{
    struct media_info *media = void_media;

    pho_lock_clean(&media->lock);
    tags_free(&media->tags);
}

const struct dss_resource_ops media_ops = {
    .insert_query = media_insert_query,
    .update_query = media_update_query,
    .select_query = media_select_query,
    .delete_query = media_delete_query,
    .create       = media_from_pg_row,
    .free         = media_result_free,
    .size         = sizeof(struct media_info),
};
