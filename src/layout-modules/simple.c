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
 * \brief  Phobos Simple Layout plugin
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <glib.h>
#include <string.h>
#include "pho_attrs.h"
#include "pho_cfg.h"
#include "pho_common.h"
#include "pho_io.h"
#include "pho_layout.h"
#include "pho_srl_common.h"
#include "pho_type_utils.h"

#define PLUGIN_NAME     "simple"
#define PLUGIN_MAJOR    0
#define PLUGIN_MINOR    1

static struct module_desc SIMPLE_MODULE_DESC = {
    .mod_name  = PLUGIN_NAME,
    .mod_major = PLUGIN_MAJOR,
    .mod_minor = PLUGIN_MINOR,
};

/**
 * Simple layout specific data.
 *
 * A simple layout just writes the data once, potentially splitting it on
 * multiple media if necessary.
 *
 * @FIXME: a part of simple data and logic may be very similar to raid1, a
 * factorization of the two modules will probably happen when refactoring raid1.
 */
struct simple_encoder {
    size_t to_write;                /**< Amount of data to read/write */
    unsigned int cur_extent_idx;    /**< Current extent index */
    bool requested_alloc;           /**< Whether an unanswer medium allocation
                                      *  has been requested by the encoder
                                      *  or not
                                      */

    /* The following two fields are only used when writing */
    /** Extents written (appended as they are written) */
    GArray *written_extents;
    /**
     * Set of already released media (key: str, value: NULL), used to ensure
     * that all written media have also been released (and therefore flushed)
     * when writing.
     */
    GHashTable *released_media;
};

/* @FIXME: taken from store.c, will be needed in raid1 too */
#define PHO_ATTR_BACKUP_JSON_FLAGS (JSON_COMPACT | JSON_SORT_KEYS)
#define PHO_EA_ID_NAME      "id"
#define PHO_EA_UMD_NAME     "user_md"

/**
 * Build the extent attributes from the object ID and the user provided
 * attributes. This information will be attached to backend objects for
 * "self-description"/"rebuild" purpose.
 */
static int build_extent_xattr(const char *objid,
                              const struct pho_attrs *user_md,
                              struct pho_attrs *extent_xattrs)
{
    GString *str;
    int      rc;

    rc = pho_attr_set(extent_xattrs, PHO_EA_ID_NAME, objid);
    if (rc)
        return rc;

    str = g_string_new(NULL);
    /*
     * TODO This conversion is done several times. Consider caching the result
     * and pass it to the functions that need it.
     */
    rc = pho_attrs_to_json(user_md, str, PHO_ATTR_BACKUP_JSON_FLAGS);
    if (rc)
        goto free_values;

    if (!gstring_empty(str)) {
        rc = pho_attr_set(extent_xattrs, PHO_EA_UMD_NAME, str->str);
        if (rc)
            goto free_values;
    }

free_values:
    if (rc != 0)
        pho_attrs_free(extent_xattrs);

    g_string_free(str, TRUE);
    return rc;
}

/** True if an encoder or decoder has finished writing. */
static bool simple_finished(struct pho_encoder *enc)
{
    struct simple_encoder *simple = enc->priv_enc;

    if (enc->done)
        return true;

    if (simple->to_write > 0)
        return false;

    /* Ensure that even a zero-sized PUT creates at least one extent */
    if (!enc->is_decoder && simple->written_extents->len == 0)
        return false;

    return true;
}

static int write_all_chunks(int input_fd, const struct io_adapter *ioa,
                            struct pho_io_descr *iod, size_t buffer_size,
                            size_t count)
{
#define MAX_NULL_READ_TRY 10
    int nb_null_read_try = 0;
    size_t to_write = count;
    char *buffer;
    int rc = 0;

    buffer = malloc(buffer_size);
    if (buffer == NULL)
        LOG_RETURN(-ENOMEM, "Unable to alloc buffer for simple encoder write");

    while (to_write > 0) {
        ssize_t buf_size;

        buf_size = read(input_fd, buffer,
                        to_write > buffer_size ? buffer_size : to_write);
        if (buf_size < 0)
            LOG_GOTO(out, rc = -errno, "Error on loading buffer in simple "
                                       "write, %zu remaning bytes", to_write);

        if (buf_size == 0) {
            ++nb_null_read_try;
            if (nb_null_read_try > MAX_NULL_READ_TRY)
                LOG_GOTO(out, rc = -EIO, "Too many null read in simple write, "
                                         "%zu remaining bytes", to_write);
            continue;
        }

        rc = ioa_write(ioa, iod, buffer, buf_size);
        if (rc)
            LOG_GOTO(out, rc, "Unable to write %zu bytes in simple write, "
                              "%zu remaining bytes", buf_size, to_write);

        iod->iod_size += buf_size;
        to_write -= buf_size;
    }

out:
    free(buffer);
    return rc;
}

/*
 * Write data from current offset to medium, filling \a extent according to the
 * data written.
 */
static int simple_enc_write_chunk(struct pho_encoder *enc,
                                  const pho_resp_write_elt_t *medium,
                                  struct extent *extent)
{
    struct simple_encoder *simple = enc->priv_enc;
    struct pho_io_descr iod = {0};
    struct pho_ext_loc loc = {0};
    char *extent_key = NULL;
    struct io_adapter ioa;
    char extent_tag[128];
    int rc, rc2;

    ENTRY;

    rc = get_io_adapter((enum fs_type)medium->fs_type, &ioa);
    if (rc)
        return rc;

    /* Start with field initializations that may fail */
    iod.iod_fd = enc->xfer->xd_fd;
    if (iod.iod_fd < 0)
        return iod.iod_fd;

    extent->layout_idx = simple->cur_extent_idx++;
    extent->size = min(simple->to_write, medium->avail_size);
    extent->media.family = (enum rsc_family)medium->med_id->family;
    pho_id_name_set(&extent->media, medium->med_id->name);
    extent->addr_type = (enum address_type)medium->addr_type;
    /* and extent.address will be filled by ioa_open */

    loc.root_path = medium->root_path;
    loc.extent = extent;

    iod.iod_flags = PHO_IO_REPLACE | PHO_IO_NO_REUSE;
    iod.iod_size = extent->size;
    iod.iod_loc = &loc;
    rc = build_extent_xattr(enc->xfer->xd_objid, &enc->xfer->xd_attrs,
                            &iod.iod_attrs);
    if (rc)
        return rc;

    pho_debug("Writing %ld bytes to medium %s", extent->size,
              extent->media.name);
    /* Build extent tag */
    rc = snprintf(extent_tag, sizeof(extent_tag), "s%d", extent->layout_idx);

    /* If the tag was more than 128 bytes long, it is a programming error */
    assert(rc < sizeof(extent_tag));

    rc = build_extent_key(enc->xfer->xd_objuuid, enc->xfer->xd_version,
                          extent_tag, &extent_key);
    if (rc)
        LOG_GOTO(err_free, rc, "Extent key build failed");

    rc = ioa_open(&ioa, extent_key, enc->xfer->xd_objid, &iod, true);
    free(extent_key);
    if (rc)
        LOG_GOTO(err_free, rc, "Unable to open extent %s in simple write",
                 extent_tag);

    rc = write_all_chunks(enc->xfer->xd_fd, &ioa, &iod, enc->io_block_size,
                          extent->size);
    if (rc)
        pho_error(rc, "Unable to write in simple encoder");
    else
        simple->to_write -= extent->size;

    rc2 = ioa_close(&ioa, &iod);
    rc = rc ? : rc2;

err_free:
    pho_attrs_free(&iod.iod_attrs);

    return rc;
}

/**
 * Handle the write allocation response by writing data on the allocated medium
 * and filling the associated release request with relevant information (rc and
 * size_written).
 */
static int simple_enc_write_all_chunks(struct pho_encoder *enc,
                                       pho_resp_write_t *wresp,
                                       pho_req_release_t *rreq)
{
    struct simple_encoder *simple = enc->priv_enc;
    struct extent cur_extent = {0};
    int rc;

    if (wresp->n_media != 1)
        LOG_RETURN(-EPROTO,
                   "Received %ld medium allocation but only 1 was requested",
                   wresp->n_media);

    /* Perform IO */
    rc = simple_enc_write_chunk(enc, wresp->media[0], &cur_extent);
    rreq->media[0]->rc = rc;
    rreq->media[0]->size_written = cur_extent.size;
    if (rc)
        return rc;

    g_array_append_val(simple->written_extents, cur_extent);

    return 0;
}

/**
 * Read the data specified by \a extent from \a medium into the output fd of
 * dec->xfer.
 */
static int simple_dec_read_chunk(struct pho_encoder *dec,
                                 const pho_resp_read_elt_t *medium)
{
    struct simple_encoder *simple = dec->priv_enc;
    struct extent *extent = &dec->layout->extents[simple->cur_extent_idx];
    struct pho_io_descr iod = {0};
    struct pho_ext_loc loc = {0};
    struct io_adapter ioa;
    char *extent_key = NULL;
    int rc;

    /*
     * NOTE: fs_type is not stored as an extent attribute in db, therefore it
     * is not retrieved when retrieving a layout either. It is currently a field
     * of a medium, this is why the LRS provides it in its response. This may be
     * intentional, or to be fixed later.
     */
    rc = get_io_adapter((enum fs_type)medium->fs_type, &ioa);
    if (rc)
        return rc;

    extent->addr_type = (enum address_type)medium->addr_type;
    loc.root_path = medium->root_path;
    loc.extent = extent;

    iod.iod_fd = dec->xfer->xd_fd;
    if (iod.iod_fd < 0)
        return iod.iod_fd;

    iod.iod_size = loc.extent->size;
    iod.iod_loc = &loc;

    pho_debug("Reading %ld bytes from medium %s", extent->size,
              extent->media.name);

    rc = build_extent_key(dec->xfer->xd_objuuid, dec->xfer->xd_version, NULL,
                          &extent_key);
    if (rc)
        LOG_RETURN(rc, "Extent key build failed");

    rc = ioa_get(&ioa, extent_key, dec->xfer->xd_objid, &iod);
    free(extent_key);
    if (rc == 0) {
        simple->to_write -= extent->size;
        simple->cur_extent_idx++;
    }

    /* Nothing more to write: the decoder is done */
    if (simple->to_write <= 0) {
        pho_debug("Decoder for '%s' is now done", dec->xfer->xd_objid);
        dec->done = true;
    }

    return rc;
}

/**
 * When receiving a release response, check that we expected this response and
 * save in simple->released_media the fact that this media_id was released.
 */
static int mark_written_media_released(struct simple_encoder *simple,
                                       const char *media)
{
    size_t i;

    for (i = 0; i < simple->written_extents->len; i++) {
        struct extent *extent;

        extent = &g_array_index(simple->written_extents, struct extent, i);
        if (strcmp(extent->media.name, media) == 0) {
            char *media_id = strdup(media);

            if (media_id == NULL)
                return -ENOMEM;

            g_hash_table_insert(simple->released_media, media_id, NULL);
            return 0;
        }
    }

    return -EINVAL;
}

/**
 * Handle a release response for an encoder (unrelevent for a decoder) by
 * remembering that these particular media have been released. If all data has
 * been written and all written media have been released, mark the encoder as
 * done.
 */
static int simple_enc_handle_release_resp(struct pho_encoder *enc,
                                          pho_resp_release_t *rel_resp)
{
    struct simple_encoder *simple = enc->priv_enc;
    size_t n_released_media;
    size_t n_written_media;
    int rc = 0;
    int i;

    for (i = 0; i < rel_resp->n_med_ids; i++) {
        int rc2;

        pho_debug("Marking medium %s as released", rel_resp->med_ids[i]->name);
        /* If the media_id is unexpected, -EINVAL will be returned */
        rc2 = mark_written_media_released(simple, rel_resp->med_ids[i]->name);
        if (rc2 && !rc)
            rc = rc2;
    }

    n_released_media = g_hash_table_size(simple->released_media);
    n_written_media = simple->written_extents->len;

    /*
     * If we wrote everything and all the releases have been received, mark the
     * encoder as done.
     */
    if (simple->to_write == 0 && n_written_media == n_released_media) {
        /* Fill the layout with the extents */
        enc->layout->ext_count = simple->written_extents->len;
        enc->layout->extents =
            (struct extent *)g_array_free(simple->written_extents, FALSE);
        simple->written_extents = NULL;
        enc->layout->state = PHO_EXT_ST_SYNC;

        /* Switch to DONE state */
        enc->done = true;
        return 0;
    }

    return rc;
}

/** Generate the next write allocation request for this encoder */
static int simple_enc_next_write_req(struct pho_encoder *enc, pho_req_t *req)
{
    struct simple_encoder *simple = enc->priv_enc;
    int rc = 0, i;

    /* Otherwise, generate the next request */
    rc = pho_srl_request_write_alloc(req, 1,
                                     &enc->xfer->xd_params.put.tags.n_tags);
    if (rc)
        return rc;

    req->walloc->media[0]->size = simple->to_write;

    for (i = 0; i < enc->xfer->xd_params.put.tags.n_tags; ++i)
        req->walloc->media[0]->tags[i] =
            strdup(enc->xfer->xd_params.put.tags.tags[i]);

    return rc;
}

/** Generate the next read allocation request for this decoder */
static int simple_dec_next_read_req(struct pho_encoder *dec, pho_req_t *req)
{
    int rc = 0;
    struct simple_encoder *simple = dec->priv_enc;
    unsigned int cur_ext_idx = simple->cur_extent_idx;

    rc = pho_srl_request_read_alloc(req, 1);
    if (rc)
        return rc;

    pho_debug("Requesting medium %s to read extent %d",
              dec->layout->extents[cur_ext_idx].media.name,
              simple->cur_extent_idx);

    req->ralloc->n_required = 1;

    req->ralloc->med_ids[0]->family =
        dec->layout->extents[cur_ext_idx].media.family;
    req->ralloc->med_ids[0]->name =
        strdup(dec->layout->extents[cur_ext_idx].media.name);

    return 0;
}

/**
 * Handle one response from the LRS and potentially generate one response.
 */
static int simple_enc_handle_resp(struct pho_encoder *enc, pho_resp_t *resp,
                                  pho_req_t **reqs, size_t *n_reqs)
{
    struct simple_encoder *simple = enc->priv_enc;
    int rc = 0, i;

    if (pho_response_is_error(resp)) {
        enc->xfer->xd_rc = resp->error->rc;
        enc->done = true;
        pho_error(enc->xfer->xd_rc,
                  "%s for objid:'%s' received error to last %s request",
                  enc->is_decoder ? "Decoder" : "Encoder", enc->xfer->xd_objid,
                  pho_srl_error_kind_str(resp->error));
    } else if (pho_response_is_write(resp)) {
        /* Last requested allocation has now been fulfilled */
        simple->requested_alloc = false;
        if (enc->is_decoder)
            return -EINVAL;

        /*
         * Build release req matching this allocation response, this release
         * request will be emitted after the IO has been performed. Any
         * allocated medium must be released.
         */
        rc = pho_srl_request_release_alloc(*reqs + *n_reqs,
                                           resp->walloc->n_media);
        if (rc)
            return rc;

        for (i = 0; i < resp->walloc->n_media; ++i)
            rsc_id_cpy((*reqs)[*n_reqs].release->media[i]->med_id,
                       resp->walloc->media[i]->med_id);

        /* Perform IO and populate release request with the outcome */
        rc = simple_enc_write_all_chunks(
                enc, resp->walloc, (*reqs)[*n_reqs].release);
        (*n_reqs)++;
    } else if (pho_response_is_read(resp)) {
        /* Last requested allocation has now been fulfilled */
        simple->requested_alloc = false;
        if (!enc->is_decoder)
            return -EINVAL;

        /* Build release req matching this allocation response */
        rc = pho_srl_request_release_alloc(*reqs + *n_reqs,
                                           resp->ralloc->n_media);
        if (rc)
            return rc;

        for (i = 0; i < resp->ralloc->n_media; ++i)
            rsc_id_cpy((*reqs)[*n_reqs].release->media[i]->med_id,
                       resp->ralloc->media[i]->med_id);

        /* Perform IO and populate release request with the outcome */
        rc = simple_dec_read_chunk(enc, resp->ralloc->media[0]);
        (*reqs)[*n_reqs].release->media[0]->rc = rc;
        (*n_reqs)++;
    } else if (pho_response_is_release(resp)) {
        /* Decoders don't need to keep track of medium releases */
        if (!enc->is_decoder)
            rc = simple_enc_handle_release_resp(enc, resp->release);

    } else {
        LOG_RETURN(rc = -EINVAL, "Invalid response type");
    }

    return rc;
}

/**
 * Simple layout implementation of the `step` method.
 * (See `layout_step` doc)
 */
static int simple_encoder_step(struct pho_encoder *enc, pho_resp_t *resp,
                               pho_req_t **reqs, size_t *n_reqs)
{
    struct simple_encoder *simple = enc->priv_enc;
    int rc = 0;

    /* At max 2 requests will be emitted, allocate optimistically */
    *reqs = calloc(2, sizeof(**reqs));
    if (*reqs == NULL)
        return -ENOMEM;
    *n_reqs = 0;

    /* Handle a possible response */
    if (resp != NULL)
        rc = simple_enc_handle_resp(enc, resp, reqs, n_reqs);

    /*
     * If an error happened or we finished writing / reading, no need to go
     * further and generate a new allocation request.
     */
    if (rc || simple_finished(enc))
        goto out;

    /* If an allocation is already waiting unanswered, don't request another */
    if (simple->requested_alloc)
        goto out;

    /* Build next request */
    if (enc->is_decoder)
        rc = simple_dec_next_read_req(enc, *reqs + *n_reqs);
    else
        rc = simple_enc_next_write_req(enc, *reqs + *n_reqs);

    if (rc)
        return rc;

    (*n_reqs)++;
    simple->requested_alloc = true;

out:
    if (*n_reqs == 0) {
        free(*reqs);
        *reqs = NULL;
    }

    /* For now, orphaned extents are not cleaned up on failure */
    return rc;
}


static void free_extent_address_buff(void *void_extent)
{
    struct extent *extent = void_extent;

    free(extent->address.buff);
}

/**
 * Simple layout implementation of the `destroy` method.
 * (See `layout_destroy` doc)
 */
static void simple_encoder_destroy(struct pho_encoder *enc)
{
    struct simple_encoder *simple = enc->priv_enc;

    if (simple == NULL)
        return;

    if (simple->written_extents != NULL)
        g_array_free(simple->written_extents, TRUE);

    if (simple->released_media != NULL)
        g_hash_table_destroy(simple->released_media);

    free(simple);
    enc->priv_enc = NULL;
}

static const struct pho_enc_ops SIMPLE_ENCODER_OPS = {
    .step       = simple_encoder_step,
    .destroy    = simple_encoder_destroy,
};

/**
 * Create an encoder or decoder, based on the enc->is_decoder field.
 *
 * This function initializes the internal simple_encoder based on enc->xfer and
 * enc->layout.
 *
 * Implements the layout_encode and layout_decode layout module methods.
 */
static int layout_simple_encode(struct pho_encoder *enc)
{
    struct simple_encoder *simple = calloc(1, sizeof(*simple));
    size_t i;

    if (simple == NULL)
        return -ENOMEM;

    /*
     * The ops field is set early to allow the caller to call the destroy
     * functionb on error.
     */
    enc->ops = &SIMPLE_ENCODER_OPS;
    enc->priv_enc = simple;

    /* Initialize simple-specific state */
    simple->cur_extent_idx = 0;
    simple->requested_alloc = false;
    if (enc->is_decoder) {
        /*
         * Size is the sum of the extent sizes, enc->layout->wr_size is not
         * positioned properly by the dss
         */
        simple->to_write = 0;
        for (i = 0; i < enc->layout->ext_count; i++)
            simple->to_write += enc->layout->extents[i].size;
    } else {
        ssize_t to_write = enc->xfer->xd_params.put.size;

        if (to_write < 0)
            return to_write;

        simple->to_write = to_write;
    }

    /* Empty GET does not need any IO */
    if (enc->is_decoder && simple->to_write == 0) {
        int fd = enc->xfer->xd_fd;

        enc->done = true;
        if (fd < 0)
            return fd;
    }

    /* Fill out the encoder appropriately */
    if (enc->is_decoder) {
        simple->written_extents = NULL;
        simple->released_media = NULL;
    } else {
        /* Allocate the extent array */
        simple->written_extents = g_array_new(FALSE, TRUE,
                                              sizeof(struct extent));
        simple->released_media = g_hash_table_new_full(g_str_hash, g_str_equal,
                                                       free, NULL);
        g_array_set_clear_func(simple->written_extents,
                               free_extent_address_buff);
        /*
         * Only set the layout description when encoding, it has to be
         * positionned on encoding
         */
        enc->layout->layout_desc = SIMPLE_MODULE_DESC;
    }

    return 0;
}

static const struct pho_layout_module_ops LAYOUT_SIMPLE_OPS = {
    .encode = layout_simple_encode,
    .decode = layout_simple_encode, /* Same as encoder */
};

/** Layout module registration entry point */
int pho_layout_mod_register(struct layout_module *self)
{
    self->desc = SIMPLE_MODULE_DESC;
    self->ops = &LAYOUT_SIMPLE_OPS;

    return 0;
}
