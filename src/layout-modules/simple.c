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
#include "pho_lrs.h"
#include "pho_common.h"
#include "pho_type_utils.h"
#include "pho_cfg.h"
#include "pho_layout.h"
#include "pho_io.h"
#include "pho_store_utils.h"

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
 * multiple medias if necessary.
 *
 * @FIXME: a part of simple data and logic may be very similar to raid1, a
 * factorization of the two modules will probably happen when refactoring raid1.
 */
struct simple_encoder {
    size_t to_write;                /**< Amount of data to read/write */
    unsigned int cur_extent_idx;    /**< Current extent index */
    bool pending_alloc;             /**< Whether a pending unanswer media
                                      *  allocation request has been emitted or
                                      *  not
                                      */

    /* The following two fields are only used when writing */
    /** Extents written (appended as they are written) */
    GArray *written_extents;
    /**
     * Set of already released medias (key: str, value: NULL), used to ensure
     * that all written medias have also been released (and therefore flushed)
     * when writing.
     */
    GHashTable *released_medias;
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

/*
 * Write data from current offset to media, filling \a extent according to the
 * data written.
 */
static int simple_enc_write_chunk(struct pho_encoder *enc,
                                  const struct media_write_resp_elt *media,
                                  struct extent *extent)
{
    struct simple_encoder *simple = enc->priv_enc;
    struct pho_ext_loc loc = {0};
    struct pho_io_descr iod = {0};
    struct io_adapter ioa;
    char extent_tag[128];
    int rc;

    ENTRY;

    rc = get_io_adapter(media->fs_type, &ioa);
    if (rc)
        return rc;

    /* Start with field initializations that may fail */
    iod.iod_fd = pho_xfer_desc_get_fd(enc->xfer);
    if (iod.iod_fd < 0)
        return iod.iod_fd;

    extent->layout_idx = simple->cur_extent_idx++;
    /*
     * @TODO: replace extent size with the actual size written (which is not
     * returned by ioa_put as of yet)
     */
    extent->size = min(simple->to_write, media->avail_size);
    extent->media = media->media_id;
    extent->addr_type = media->addr_type;
    /* and extent.address will be filled by ioa_put */

    loc.root_path = media->root_path;
    loc.extent = extent;

    iod.iod_flags = PHO_IO_REPLACE | PHO_IO_NO_REUSE;
    iod.iod_size = extent->size;
    iod.iod_loc = &loc;
    rc = build_extent_xattr(enc->xfer->xd_objid, &enc->xfer->xd_attrs,
                            &iod.iod_attrs);
    if (rc)
        return rc;

    pho_debug("Writing %ld bytes to media %s", extent->size,
              media_id_get(&extent->media));
    /* Build extent tag */
    rc = snprintf(extent_tag, sizeof(extent_tag), "s%d", extent->layout_idx);

    /* If the tag was more than 128 bytes long, it is a programming error */
    assert(rc < sizeof(extent_tag));

    rc = ioa_put(&ioa, enc->xfer->xd_objid, extent_tag, &iod);
    pho_attrs_free(&iod.iod_attrs);
    if (rc == 0)
        simple->to_write -= extent->size;

    return rc;
}

/**
 * Handle the write allocation response by writing data on the allocated media
 * and filling the associated release request with relevant information (rc and
 * size_written).
 */
static int simple_enc_write_all_chunks(struct pho_encoder *enc,
                                       struct media_write_alloc_resp *wresp,
                                       struct media_release_req *rreq)
{
    struct simple_encoder *simple = enc->priv_enc;
    struct extent cur_extent = {0};
    int rc;

    if (wresp->n_medias != 1)
        LOG_RETURN(-EPROTO,
                   "Received %d media allocation but only 1 was requested",
                   wresp->n_medias);

    /* Perform IO */
    rc = simple_enc_write_chunk(enc, &wresp->medias[0], &cur_extent);
    rreq->medias[0].rc = rc;
    rreq->medias[0].size_written = cur_extent.size;
    if (rc)
        return rc;

    g_array_append_val(simple->written_extents, cur_extent);

    return 0;
}

/**
 * Read the data specified by \a extent from \a media into the output fd of
 * dec->xfer.
 */
static int simple_dec_read_chunk(struct pho_encoder *dec,
                                 const struct media_read_resp_elt *media)
{
    struct simple_encoder *simple = dec->priv_enc;
    struct extent *extent = &dec->layout->extents[simple->cur_extent_idx];
    struct pho_ext_loc loc = {0};
    struct pho_io_descr iod = {0};
    struct io_adapter ioa;
    int rc;

    /*
     * NOTE: fs_type is not stored as an extent attribute in db, therefore it
     * is not retrieved when retrieving a layout either. It is currently a field
     * of a media, this is why the LRS provides it in its response. This may be
     * intentional, or to be fixed later.
     */
    rc = get_io_adapter(media->fs_type, &ioa);
    if (rc)
        return rc;

    extent->addr_type = media->addr_type;
    loc.root_path = media->root_path;
    loc.extent = extent;

    iod.iod_fd = pho_xfer_desc_get_fd(dec->xfer);
    if (iod.iod_fd < 0)
        return iod.iod_fd;

    iod.iod_size = loc.extent->size;
    iod.iod_loc = &loc;

    pho_debug("Reading %ld bytes from media %s", extent->size,
              media_id_get(&extent->media));

    rc = ioa_get(&ioa, dec->xfer->xd_objid, NULL, &iod);

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
 * save in simple->released_medias the fact that this media_id was released.
 */
static int mark_written_media_released(struct simple_encoder *simple,
                                       const struct media_id *media)
{
    size_t i;

    for (i = 0; i < simple->written_extents->len; i++) {
        struct extent *extent;

        extent = &g_array_index(simple->written_extents, struct extent, i);
        if (strcmp(media_id_get(&extent->media), media_id_get(media)) == 0) {
            char *media_id = strdup(media_id_get(media));

            if (media_id == NULL)
                return -ENOMEM;

            g_hash_table_insert(simple->released_medias, media_id, NULL);
            return 0;
        }
    }

    return -EINVAL;
}

/**
 * Handle a release response for an encoder (unrelevent for a decoder) by
 * remembering that these particular media have been released. If all data has
 * been written and all written media has been released, mark the encoder as
 * done.
 */
static int simple_enc_handle_release_resp(struct pho_encoder *enc,
                                          struct media_release_resp *rel_resp)
{
    struct simple_encoder *simple = enc->priv_enc;
    size_t n_released_media;
    size_t n_written_media;
    int rc = 0;
    int i;

    for (i = 0; i < rel_resp->n_medias; i++) {
        struct media_id *id = &rel_resp->media_ids[i];
        int rc2;

        pho_debug("Marking media %s as released", media_id_get(id));
        /* If the media_id is unexpected, -EINVAL will be returned */
        rc2 = mark_written_media_released(simple, id);
        if (rc2 && !rc)
            rc = rc2;
    }

    n_released_media = g_hash_table_size(simple->released_medias);
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
static int simple_enc_next_write_req(struct pho_encoder *enc,
                                     struct pho_lrs_req *req)
{
    struct simple_encoder *simple = enc->priv_enc;
    int rc = 0;

    /* Otherwise, generate the next request */
    req->kind = LRS_REQ_MEDIA_WRITE_ALLOC;
    req->body.walloc.n_medias = 1;
    req->body.walloc.medias = malloc(sizeof(*req->body.walloc.medias));
    if (req->body.walloc.medias == NULL)
        return -ENOMEM;

    req->body.walloc.medias[0].size = simple->to_write;
    rc = tags_dup(&req->body.walloc.medias[0].tags, &enc->xfer->xd_tags);
    if (rc) {
        free(req->body.walloc.medias);
        req->body.walloc.medias = NULL;
    }

    return rc;
}

/** Generate the next read allocation request for this decoder */
static int simple_dec_next_read_req(struct pho_encoder *dec,
                                    struct pho_lrs_req *req)
{
    struct simple_encoder *simple = dec->priv_enc;
    unsigned int cur_ext_idx = simple->cur_extent_idx;

    req->kind = LRS_REQ_MEDIA_READ_ALLOC;
    req->body.ralloc.media_ids =
        malloc(sizeof(*req->body.ralloc.media_ids));
    if (req->body.ralloc.media_ids == NULL)
        return -ENOMEM;

    pho_debug("Requesting media %s to read extent %d",
              media_id_get(&dec->layout->extents[cur_ext_idx].media),
              simple->cur_extent_idx);

    req->body.ralloc.n_medias = 1;
    req->body.ralloc.n_required = 1;
    req->body.ralloc.media_ids[0] = dec->layout->extents[cur_ext_idx].media;

    return 0;
}

/** Build the next release request from a write or read alloc response */
#define BUILD_MATCHING_RELEASE_REQ(_req_ptr, _n_medias, _resp_array, \
                                   _resp_media_id_field, _rc) \
    do { \
        size_t _i; \
        (_req_ptr)->kind = LRS_REQ_MEDIA_RELEASE; \
        (_req_ptr)->body.release.n_medias = (_n_medias); \
        (_req_ptr)->body.release.medias = \
            calloc((_n_medias), sizeof(*(_req_ptr)->body.release.medias)); \
        if ((_req_ptr)->body.release.medias == NULL) { \
            _rc = -ENOMEM; \
            break; \
        } \
        for (_i = 0; _i < (_n_medias); _i++) \
            memcpy(&(_req_ptr)->body.release.medias[_i].id, \
                   &(_resp_array)[_i]._resp_media_id_field, \
                   sizeof(struct media_id)); \
        _rc = 0; \
    } while (0)

/**
 * Handle one response from the LRS and potentially generate one response.
 */
static int simple_enc_handle_resp(struct pho_encoder *enc,
                                  struct pho_lrs_resp *resp,
                                  struct pho_lrs_req **reqs, size_t *n_reqs)
{
    struct simple_encoder *simple = enc->priv_enc;
    int rc = 0;

    if (resp->protocol_version != PHO_LRS_PROTOCOL_VERSION)
        return -EPROTONOSUPPORT;

    switch (resp->kind) {
    case LRS_RESP_ERROR:
        enc->xfer->xd_rc = resp->body.error.rc;
        enc->done = true;
        pho_error(enc->xfer->xd_rc,
                  "%s for objid:'%s' received error to last %s request",
                  enc->is_decoder ? "Decoder" : "Encoder", enc->xfer->xd_objid,
                  lrs_req_kind_str(resp->body.error.req_kind));
        break;

    case LRS_RESP_MEDIA_WRITE_ALLOC:
        /* Last emitted allocation has now been fulfilled */
        simple->pending_alloc = false;
        if (enc->is_decoder)
            return -EINVAL;

        /*
         * Build release req matching this allocation response, this release
         * request will be emitted after the IO has been performed. Any
         * allocated medium must be released.
         */
        BUILD_MATCHING_RELEASE_REQ(&(*reqs)[*n_reqs],
                                   resp->body.walloc.n_medias,
                                   resp->body.walloc.medias, media_id, rc);
        if (rc)
            return rc;

        /* Perform IO and populate release request with the outcome */
        rc = simple_enc_write_all_chunks(
                enc, &resp->body.walloc, &(*reqs)[*n_reqs].body.release);
        (*n_reqs)++;
        break;

    case LRS_RESP_MEDIA_READ_ALLOC:
        /* Last emitted allocation has now been fulfilled */
        simple->pending_alloc = false;
        if (!enc->is_decoder)
            return -EINVAL;

        /* Build release req matching this allocation response */
        BUILD_MATCHING_RELEASE_REQ(&(*reqs)[*n_reqs],
                                   resp->body.ralloc.n_medias,
                                   resp->body.ralloc.medias, media_id, rc);
        if (rc)
            return rc;

        /* Perform IO and populate release request with the outcome */
        rc = simple_dec_read_chunk(enc, &resp->body.ralloc.medias[0]);
        (*reqs)[*n_reqs].body.release.medias[0].rc = rc;
        (*n_reqs)++;
        break;

    case LRS_RESP_MEDIA_RELEASE:
        /* Decoders don't need to keep track of media releases */
        if (!enc->is_decoder)
            rc = simple_enc_handle_release_resp(enc, &resp->body.release);
        break;

    default:
        LOG_RETURN(rc = -EINVAL, "Invalid response type");
    }

    return rc;
}

/**
 * Simple layout implementation of the `step` method.
 * (See `layout_step` doc)
 */
static int simple_encoder_step(struct pho_encoder *enc,
                               struct pho_lrs_resp *resp,
                               struct pho_lrs_req **reqs, size_t *n_reqs)
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
    if (simple->pending_alloc)
        goto out;

    /* Build next request */
    if (enc->is_decoder)
        rc = simple_dec_next_read_req(enc, &(*reqs)[*n_reqs]);
    else
        rc = simple_enc_next_write_req(enc, &(*reqs)[*n_reqs]);

    if (rc)
        return rc;

    (*n_reqs)++;
    simple->pending_alloc = true;

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

    if (simple->released_medias != NULL)
        g_hash_table_destroy(simple->released_medias);

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
    simple->pending_alloc = false;
    if (enc->is_decoder) {
        /*
         * Size is the sum of the extent sizes, enc->layout->wr_size is not
         * positioned properly by the dss
         */
        simple->to_write = 0;
        for (i = 0; i < enc->layout->ext_count; i++)
            simple->to_write += enc->layout->extents[i].size;
    } else {
        ssize_t to_write = pho_xfer_desc_get_size(enc->xfer);

        if (to_write < 0)
            return to_write;

        simple->to_write = to_write;
    }

    /* Empty GET does not need any IO */
    if (enc->is_decoder && simple->to_write == 0) {
        int fd = pho_xfer_desc_get_fd(enc->xfer);

        enc->done = true;
        if (fd < 0)
            return fd;
    }

    /* Fill out the encoder appropriately */
    if (enc->is_decoder) {
        simple->written_extents = NULL;
        simple->released_medias = NULL;
    } else {
        /* Allocate the extent array */
        simple->written_extents = g_array_new(FALSE, TRUE,
                                              sizeof(struct extent));
        simple->released_medias = g_hash_table_new_full(g_str_hash, g_str_equal,
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
