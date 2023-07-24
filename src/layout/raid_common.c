/*
 *  All rights reserved (c) 2014-2023 CEA/DAM.
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

#ifdef HAVE_CONFIG_H

#include "config.h"
#endif

#include <assert.h>
#include <errno.h>
#include <glib.h>
#include <openssl/evp.h>
#include <string.h>
#include <unistd.h>

#include "pho_attrs.h"
#include "pho_cfg.h"
#include "pho_common.h"
#include "pho_dss.h"
#include "pho_io.h"
#include "pho_layout.h"
#include "pho_module_loader.h"
#include "pho_srl_common.h"
#include "pho_type_utils.h"
#include "pho_types.h"
#include "raid_common.h"

#define EXTENT_TAG_SIZE 128
#define PHO_ATTR_BACKUP_JSON_FLAGS (JSON_COMPACT | JSON_SORT_KEYS)
#define PHO_EA_ID_NAME      "id"
#define PHO_EA_UMD_NAME     "user_md"

bool no_more_alloc(struct pho_encoder *enc)
{
    const struct raid_io_context *io_context = enc->priv_enc;
    /* ended encoder */
    if (enc->done)
        return true;

    /* still something to write */
    if (io_context->to_write > 0)
        return false;

    /* decoder with no more to read */
    if (enc->is_decoder)
        return true;

    /* encoder with no more to write and at least one written extent */
    if (io_context->written_extents->len > 0)
        return true;

    /* encoder with no more to write but needing to write at least one extent */
    return false;
}

void raid_build_write_allocation_req(struct pho_encoder *enc, pho_req_t *req)
{
    struct raid_io_context *io_context = enc->priv_enc;
    size_t *n_tags;
    int i;
    int j;

    n_tags = xcalloc(io_context->repl_count, sizeof(*n_tags));

    for (i = 0; i < io_context->repl_count; ++i)
        n_tags[i] = enc->xfer->xd_params.put.tags.n_tags;

    pho_srl_request_write_alloc(req, io_context->repl_count, n_tags);
    free(n_tags);

    for (i = 0; i < io_context->repl_count; ++i) {
        req->walloc->media[i]->size = io_context->to_write;

        for (j = 0; j < enc->xfer->xd_params.put.tags.n_tags; ++j)
            req->walloc->media[i]->tags[j] =
                xstrdup(enc->xfer->xd_params.put.tags.tags[j]);
    }
}

/** Generate the next read allocation request for this decoder */
void raid_build_read_allocation_req(struct pho_encoder *dec, pho_req_t *req)
{
    struct raid_io_context *io_context = dec->priv_enc;
    int i;

    pho_srl_request_read_alloc(req, io_context->repl_count);

    req->ralloc->n_required = io_context->nb_needed_media;

    for (i = 0; i < io_context->repl_count; ++i) {
        unsigned int ext_idx;

        ext_idx = i + io_context->cur_extent_idx * io_context->repl_count;

        req->ralloc->med_ids[i]->family =
            dec->layout->extents[ext_idx].media.family;
        req->ralloc->med_ids[i]->name =
            strdup(dec->layout->extents[ext_idx].media.name);
    }
}

int raid_io_context_set_extent_info(struct raid_io_context *io_context,
                                    pho_resp_write_elt_t **medium,
                                    int extent_idx, size_t extent_size)
{
    int i;

    for (i = 0; i < io_context->repl_count; i++) {
        io_context->extent[i].uuid = generate_uuid();
        io_context->extent[i].layout_idx = extent_idx + i;
        io_context->extent[i].size = extent_size;
        io_context->extent[i].media.family =
                                (enum rsc_family)medium[i]->med_id->family;
        pho_id_name_set(&io_context->extent[i].media, medium[i]->med_id->name);
    }

    return 0;
}

int raid_io_context_write_init(struct pho_encoder *enc, pho_resp_write_t *wresp,
                               size_t *buffer_size, size_t extent_size)
{
    struct raid_io_context *io_context = enc->priv_enc;
    int rc;
    int i;
    GString *user_md_json_repr;

    /* setup ioa */
    io_context->ioa = calloc(io_context->repl_count, sizeof(*io_context->ioa));
    if (io_context->ioa == NULL)
        LOG_RETURN(-ENOMEM, "Unable to allocate ioa table in raid "
                   "encoder write");

    for (i = 0; i < io_context->repl_count; ++i) {
        rc = get_io_adapter((enum fs_type)wresp->media[i]->fs_type,
                            &io_context->ioa[i]);
        if (rc)
            LOG_RETURN(rc, "Unable to get io_adapter in raid encoder");
    }

    /* setup iod */
    io_context->iod = calloc(io_context->repl_count, sizeof(*io_context->iod));
    if (io_context->iod == NULL)
        LOG_RETURN(-ENOMEM,
                   "Unable to alloc iod table for raid encoder");

    io_context->extent_tag = calloc(io_context->repl_count,
                                    sizeof(*io_context->extent_tag) *
                                    EXTENT_TAG_SIZE);
    if (io_context->extent_tag == NULL)
        LOG_RETURN(rc = -ENOMEM,
                   "Unable to alloc extent_tag table in raid encoder");

    /* setup extents */
    io_context->extent = calloc(io_context->repl_count,
                                sizeof(*io_context->extent));

    if (io_context->extent == NULL)
        LOG_RETURN(-ENOMEM,
                   "Unable to alloc extent table in raid encoder");

    /* setup extent_tag */
    io_context->extent_tag = calloc(io_context->repl_count,
                                    sizeof(*io_context->extent_tag) *
                                    EXTENT_TAG_SIZE);

    if (io_context->extent_tag == NULL)
        LOG_RETURN(rc = -ENOMEM,
                   "Unable to alloc extent_tag table in raid write");

    /* setup extent location */
    io_context->ext_location = calloc(io_context->repl_count,
                                      sizeof(*io_context->ext_location));
    if (io_context->ext_location == NULL)
        LOG_RETURN(-ENOMEM,
                   "Unable to alloc loc table for raid encoder write");

    for (i = 0; i < io_context->repl_count; i++) {

        io_context->ext_location[i].root_path = wresp->media[i]->root_path;
        io_context->ext_location[i].extent = &io_context->extent[i];
        io_context->ext_location[i].addr_type =
                    (enum address_type)wresp->media[i]->addr_type;
        io_context->iod[i].iod_size = 0;
        io_context->iod[i].iod_loc = &io_context->ext_location[i];
    }

    /*
     * Build the extent attributes from the object ID and the user provided
     * attributes. This information will be attached to backend objects for
     * "self-description"/"rebuild" purpose.
     */

    user_md_json_repr = g_string_new(NULL);
    rc = pho_attrs_to_json(&enc->xfer->xd_attrs, user_md_json_repr,
                           PHO_ATTR_BACKUP_JSON_FLAGS);

    if (rc) {
        g_string_free(user_md_json_repr, TRUE);
        LOG_RETURN(rc, "Failed to convert attributes to JSON");
    }

    raid_io_context_setmd(io_context, enc->xfer->xd_objid,
                          user_md_json_repr);

    g_string_free(user_md_json_repr, TRUE);

    if (rc)
        return rc;

    /* prepare all extent */
    rc = raid_io_context_set_extent_info(io_context, wresp->media,
                                         io_context->cur_extent_idx *
                                         io_context->repl_count, extent_size);

    if (rc)
        LOG_RETURN(rc, "Failed to set extent information");

    rc = raid_io_context_open(io_context, enc);
    if (rc)
        return rc;

    /* io_size already specified in the configuration? */
    if (enc->io_block_size != 0)
        goto next;

    enc->io_block_size = ioa_preferred_io_size(io_context->ioa[0],
                                               &io_context->iod[0]);
    if (enc->io_block_size <= 0)
        /* fallback: get the system page size */
        enc->io_block_size = sysconf(_SC_PAGESIZE);

next:
    *buffer_size = enc->io_block_size;
    if (extent_size < *buffer_size)
        *buffer_size = extent_size;

    for (i = 0; i < io_context->repl_count; i++) {

        io_context->parts[i] = malloc(*buffer_size);
        if (io_context->parts[i] == NULL)
            LOG_RETURN(-ENOMEM, "Unable to alloc buffer for raid "
                                "encoder write");
    }

    return 0;
}

int raid_io_context_read_init(struct pho_encoder *dec,
                              pho_resp_read_elt_t **medium)
{
    struct raid_io_context *io_context = dec->priv_enc;
    int rc;

    io_context->ioa = calloc(1, sizeof(*io_context->ioa));
    if (io_context->ioa == NULL)
        return -ENOMEM;

    rc = get_io_adapter((enum fs_type)medium[0]->fs_type, io_context->ioa);
    if (rc)
        return rc;

    io_context->ext_location = xcalloc(2, sizeof(*io_context->ext_location));
    io_context->iod = xcalloc(2, sizeof(*io_context->iod));

    return 0;
}

int mark_written_medium_released(struct raid_io_context *io_context,
                                 const char *medium)
{
    size_t *to_release_refcount;

    to_release_refcount = g_hash_table_lookup(io_context->to_release_media,
                                              medium);

    if (to_release_refcount == NULL)
        return -EINVAL;

    /* media id with refcount of zero must be removed from the hash table */
    assert(*to_release_refcount > 0);

    /* one medium was released */
    io_context->n_released_media++;

    /* only one release was ongoing for this medium: remove from the table */
    if (*to_release_refcount == 1) {
        gboolean was_in_table;

        was_in_table = g_hash_table_remove(io_context->to_release_media,
                                           medium);
        assert(was_in_table);
        return 0;
    }

    /* several current releases: only decrement refcount */
    --(*to_release_refcount);
    return 0;
}

int raid_enc_handle_release_resp(struct pho_encoder *enc,
                                 pho_resp_release_t *rel_resp)
{
    struct raid_io_context *io_context = enc->priv_enc;
    int rc = 0;
    int i;

    for (i = 0; i < rel_resp->n_med_ids; i++) {
        int rc2;

        pho_debug("Marking medium %s as released", rel_resp->med_ids[i]->name);
        /* If the media_id is unexpected, -EINVAL will be returned */
        rc2 = mark_written_medium_released(io_context,
                                           rel_resp->med_ids[i]->name);
        if (rc2 && !rc)
            rc = rc2;
    }

    /*
     * If we wrote everything and all the releases have been received, mark the
     * encoder as done.
     */
    if (io_context->to_write == 0 && /* no more data to write */
            /* at least one extent is created, special test for null size put */
            io_context->written_extents->len > 0 &&
            /* we got releases of all extents */
            io_context->written_extents->len == io_context->n_released_media) {
        /* Fill the layout with the extents */
        enc->layout->ext_count = io_context->written_extents->len;
        enc->layout->extents =
            (struct extent *)g_array_free(io_context->written_extents, FALSE);
        io_context->written_extents = NULL;
        io_context->n_released_media = 0;
        g_hash_table_destroy(io_context->to_release_media);
        io_context->to_release_media = NULL;
        for (i = 0; i < enc->layout->ext_count; ++i)
            enc->layout->extents[i].state = PHO_EXT_ST_SYNC;

        /* Switch to DONE state */
        enc->done = true;
        return 0;
    }

    return rc;
}

int raid_enc_handle_write_resp(struct pho_encoder *enc,
                               pho_resp_t *resp, pho_req_t **reqs,
                               size_t *n_reqs)
{
    struct raid_io_context *io_context = enc->priv_enc;
    int rc;
    int i;

    /*
     * Build release req matching this allocation response, this release
     * request will be emitted after the IO has been performed. Any
     * allocated medium must be released.
     */
    pho_srl_request_release_alloc(*reqs + *n_reqs, resp->walloc->n_media);

    for (i = 0; i < resp->walloc->n_media; ++i) {
        rsc_id_cpy((*reqs)[*n_reqs].release->media[i]->med_id,
                   resp->walloc->media[i]->med_id);
        (*reqs)[*n_reqs].release->media[i]->to_sync = true;
    }

    /* Perform IO and populate release request with the outcome */
    rc = io_context->ops->write(enc, resp->walloc,
                                (*reqs)[*n_reqs].release);
    (*n_reqs)++;

    return rc;
}

int raid_enc_handle_read_resp(struct pho_encoder *enc, pho_resp_t *resp,
                               pho_req_t **reqs, size_t *n_reqs)
{
    struct raid_io_context *io_context = enc->priv_enc;
    int rc;
    int i;

    pho_srl_request_release_alloc(*reqs + *n_reqs, resp->ralloc->n_media);

    for (i = 0; i < io_context->nb_needed_media; i++)
        rsc_id_cpy((*reqs)[*n_reqs].release->media[i]->med_id,
                   resp->ralloc->media[i]->med_id);

    rc = io_context->ops->read(enc, resp->ralloc->media);

    for (i = 0; i < io_context->nb_needed_media; i++) {
        (*reqs)[*n_reqs].release->media[i]->rc = rc;
        (*reqs)[*n_reqs].release->media[i]->to_sync = false;
    }

    (*n_reqs)++;

    return rc;
}

int raid_enc_handle_resp(struct pho_encoder *enc, pho_resp_t *resp,
                         pho_req_t **reqs, size_t *n_reqs)
{
    struct raid_io_context *io_context = enc->priv_enc;
    int rc = 0;

    if (pho_response_is_error(resp)) {
        enc->xfer->xd_rc = resp->error->rc;
        enc->done = true;
        LOG_RETURN(enc->xfer->xd_rc,
                   "%s for objid:'%s' received error %s to last request",
                   enc->is_decoder ? "Decoder" : "Encoder", enc->xfer->xd_objid,
                   pho_srl_error_kind_str(resp->error));

    } else if (pho_response_is_write(resp)) {

        io_context->requested_alloc = false;
        if (enc->is_decoder)
            return -EINVAL;

        if (resp->walloc->n_media != io_context->repl_count)
            return -EINVAL;

        return raid_enc_handle_write_resp(enc, resp, reqs, n_reqs);

    } else if (pho_response_is_read(resp)) {

        io_context->requested_alloc = false;
        if (!enc->is_decoder)
            return -EINVAL;

        if (resp->ralloc->n_media != io_context->nb_needed_media)
            return -EINVAL;

        return raid_enc_handle_read_resp(enc, resp, reqs, n_reqs);

    } else if (pho_response_is_release(resp)) {
        if (!enc->is_decoder)
            return raid_enc_handle_release_resp(enc, resp->release);
        else
            return 0;
    } else {
        LOG_RETURN(rc = -EPROTO, "Invalid response type");
    }
}

void raid_encoder_destroy(struct pho_encoder *enc)
{
    struct raid_io_context *io_context = enc->priv_enc;

    if (io_context == NULL)
        return;

    if (io_context->written_extents != NULL) {
        g_array_free(io_context->written_extents, TRUE);
        io_context->written_extents = NULL;
    }

    if (io_context->to_release_media != NULL) {
        g_hash_table_destroy(io_context->to_release_media);
        io_context->to_release_media = NULL;
    }

    free(io_context);
    enc->priv_enc = NULL;
}

int raid_encoder_step(struct pho_encoder *enc, pho_resp_t *resp,
                       pho_req_t **reqs, size_t *n_reqs)
{
    struct raid_io_context *io_context = enc->priv_enc;
    int rc = 0;

    /* At most 2 requests will be emitted, allocate optimistically */
    *reqs = xcalloc(2, sizeof(**reqs));
    *n_reqs = 0;

    /* Handle a possible response */
    if (resp != NULL)
        rc = raid_enc_handle_resp(enc, resp, reqs, n_reqs);

    /* Do we need to generate a new alloc ? */
    if (rc || /* an error happened */
        io_context->requested_alloc || /* an alloc was already requested */
        no_more_alloc(enc))
        goto out;

    /* Build next request */
    if (enc->is_decoder)
        raid_build_read_allocation_req(enc, *reqs + *n_reqs);
    else
        raid_build_write_allocation_req(enc, *reqs + *n_reqs);

    (*n_reqs)++;
    io_context->requested_alloc = true;

out:
    if (*n_reqs == 0) {
        free(*reqs);
        *reqs = NULL;
    }

    /* For now, orphaned extents are not cleaned up on failure */
    return rc;
}

void raid_io_context_setmd(struct raid_io_context *io_context, char *xd_objid,
                           const GString *str)
{
     int i;

     for (i = 0; i < io_context->repl_count; ++i) {
        pho_attr_set(&io_context->iod[i].iod_attrs, PHO_EA_ID_NAME, xd_objid);

        if (!gstring_empty(str))
            pho_attr_set(&io_context->iod[i].iod_attrs, PHO_EA_UMD_NAME,
                         str->str);
    }
}

int raid_io_context_open(struct raid_io_context *io_context,
                         struct pho_encoder *enc)
{
    int i;

    for (i = 0; i < io_context->repl_count; ++i) {
        int rc;

        rc = ioa_open(io_context->ioa[i], enc->xfer->xd_objid,
                      &io_context->iod[i], true);
        if (rc)
            LOG_RETURN(rc, "Unable to open extent %s in raid write",
                       &io_context->extent_tag[i * EXTENT_TAG_SIZE]);

        pho_debug("I/O size for replicate %d: %zu", i, enc->io_block_size);
    }

    return 0;
}

int add_new_to_release_media(struct raid_io_context *io_context,
                             const char *media_id)
{
    size_t *new_ref_count;
    gboolean was_not_in;
    char *new_media_id;

    /* alloc and set new ref count */
    new_ref_count = malloc(sizeof(*new_ref_count));
    if (new_ref_count == NULL)
        return -ENOMEM;

    *new_ref_count = 1;

    /* alloc new media_id */
    new_media_id = strdup(media_id);
    if (new_media_id == NULL) {
        free(new_ref_count);
        return -ENOMEM;
    }

    was_not_in = g_hash_table_insert(io_context->to_release_media, new_media_id,
                                     new_ref_count);
    assert(was_not_in);
    return 0;
}

int raid_io_add_written_extent(struct raid_io_context *io_context,
                                struct extent *extent)
{
    size_t *to_release_refcount;
    const char *media_id;
        /* add extent to written ones */
    g_array_append_val(io_context->written_extents, *extent);

    /* add medium to be released */
    media_id = extent->media.name;
    to_release_refcount = g_hash_table_lookup(io_context->to_release_media,
                                              media_id);
    /* existing media_id to release */
    if (to_release_refcount != NULL) {
        ++(*to_release_refcount);
        return 0;
    }

    /* new media_id to release */
    return add_new_to_release_media(io_context, media_id);
}

void raid_io_context_fini(struct raid_io_context *io_context)
{
    free(io_context->ext_location);
    free(io_context->iod);
    free(io_context->extent_tag);
    free(io_context->extent);
    free(io_context->ioa);
}
