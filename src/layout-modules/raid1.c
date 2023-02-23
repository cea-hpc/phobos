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
 * \brief  Phobos Raid1 Layout plugin
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <errno.h>
#include <glib.h>
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
#include "raid1.h"

/* @FIXME: taken from store.c, will be needed in raid1 too */
#define PHO_ATTR_BACKUP_JSON_FLAGS (JSON_COMPACT | JSON_SORT_KEYS)
#define PHO_EA_ID_NAME      "id"
#define PHO_EA_UMD_NAME     "user_md"

#define PLUGIN_NAME     "raid1"
#define PLUGIN_MAJOR    0
#define PLUGIN_MINOR    2

static struct module_desc RAID1_MODULE_DESC = {
    .mod_name  = PLUGIN_NAME,
    .mod_major = PLUGIN_MAJOR,
    .mod_minor = PLUGIN_MINOR,
};

/**
 * Raid1 layout specific data.
 *
 * A raid1 layout writes repl_count copies of the data.
 *
 * It potentially splits it on several extents if there is no convenient
 * available space on media provided by lrs. There are repl_count copies of each
 * extent.
 *
 * In the layout all extents copies are flattened as different extent :
 * - extents with index from 0 to repl_count - 1 are the repl_count copies of
 *   the first extent,
 * - extents with index from repl_count to 2*repl_count - 1 are the
 *   repl_count copies of the second extent,
 * - ...
 *
 * With replica_id from 0 to repl_count - 1, the flattened layout extent index
 * is : raid1_encoder.cur_extent_idx * raid1_encoder.repl_count + repl_id .
 *
 * To put an object of a written size of 0, we create an extent of null size to
 * really have a residual null size object on media.
 */
struct raid1_encoder {
    unsigned int repl_count;
    size_t       to_write;          /**< Amount of data to read/write */
    unsigned int cur_extent_idx;    /**< Current extent index */
    bool         requested_alloc;   /**< Whether an unanswer medium allocation
                                      *  has been requested by the encoder
                                      *  or not
                                      */

    /* The following two fields are only used when writing */
    /** Extents written (appended as they are written) */
    GArray *written_extents;

    /**
     * Set of media to release (key: media_id str, value: refcount), used to
     * ensure that all written media have also been released (and therefore
     * flushed) when writing.
     *
     * We use a refcount as value to manage multiple extents written on same
     * medium.
     */
    GHashTable *to_release_media;

    /**
     * Nb media released
     *
     * We increment for each medium release response. Same medium used two
     * different times for two different extents will increment two times this
     * counter.
     *
     * Except from null sized put, the end of the write is checked by
     * nb_released_media == written_extents->len .
     */
    size_t n_released_media;
};

/**
 * List of configuration parameters for this module
 */
enum pho_cfg_params_raid1 {
    /* Actual parameters */
    PHO_CFG_LYT_RAID1_repl_count,

    /* Delimiters, update when modifying options */
    PHO_CFG_LYT_RAID1_FIRST = PHO_CFG_LYT_RAID1_repl_count,
    PHO_CFG_LYT_RAID1_LAST  = PHO_CFG_LYT_RAID1_repl_count,
};

const struct pho_config_item cfg_lyt_raid1[] = {
    [PHO_CFG_LYT_RAID1_repl_count] = {
        .section = "layout_raid1",
        .name    = REPL_COUNT_ATTR_KEY,
        .value   = "2"  /* Total # of copies (default) */
    },
};

/**
 * Add a media to release with an initial refcount of 1
 */
static int add_new_to_release_media(struct raid1_encoder *raid1,
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

    was_not_in = g_hash_table_insert(raid1->to_release_media, new_media_id,
                                     new_ref_count);
    assert(was_not_in);
    return 0;
}

/**
 * Add a written extent to the raid1 encoder and add the medium to release
 *
 * @return 0 if success, else a negative error code if a failure occurs
 */
static int add_written_extent(struct raid1_encoder *raid1,
                              struct extent *extent)
{
    size_t *to_release_refcount;
    const char *media_id;

    /* add extent to written ones */
    g_array_append_val(raid1->written_extents, *extent);

    /* add medium to be released */
    media_id = extent->media.name;
    to_release_refcount = g_hash_table_lookup(raid1->to_release_media,
                                              media_id);
    /* existing media_id to release */
    if (to_release_refcount != NULL) {
        ++(*to_release_refcount);
        return 0;
    }

    /* new media_id to release */
    return add_new_to_release_media(raid1, media_id);
}

/**
 * Set unsigned int replica count value from char * layout attr
 *
 * 0 is not a valid replica count, -EINVAL will be returned.
 *
 * @param[in]  layout     layout with a REPL_COUNT_ATTR_KEY
 * @param[out] repl_count replica count value to set
 *
 * @return 0 if success,
 *         -error_code if failure and \p repl_count value is irrelevant
 */
int layout_repl_count(struct layout_info *layout, unsigned int *repl_count)
{
    const char *string_repl_count = pho_attr_get(&layout->layout_desc.mod_attrs,
                                                 REPL_COUNT_ATTR_KEY);
    if (string_repl_count == NULL)
        LOG_RETURN(-EINVAL, "Unable to get replica count from layout attrs");

    errno = 0;
    *repl_count = strtoul(string_repl_count, NULL, REPL_COUNT_ATTR_VALUE_BASE);
    if (errno != 0)
        return -errno;

    if (!*repl_count)
        LOG_RETURN(-EINVAL, "invalid 0 replica count");

    return 0;
}

/**
 * Fill an extent structure, except the adress field, which is usually set by
 * a future call to ioa_open.
 *
 * @param[out] extent      extent to fill
 * @param[in]  media       media on which the extent is written
 * @param[in]  layout_idx  index of this extent in the layout
 * @param[in]  extent_size size of the extent in byte
 */
static void set_extent_info(struct extent *extent,
                            const pho_resp_write_elt_t *medium,
                            int layout_idx, ssize_t extent_size)
{
    extent->layout_idx = layout_idx;
    extent->size = extent_size;
    extent->media.family = (enum rsc_family)medium->med_id->family;
    pho_id_name_set(&extent->media, medium->med_id->name);
}

/**
 * Write count bytes from input_fd in each iod.
 *
 * Bytes are read from input_fd and stored to an intermediate buffer before
 * being written into each iod.
 *
 * @return 0 if success, else a negative error code
 */
static int write_all_chunks(int input_fd, struct io_adapter_module **ioa,
                            struct pho_io_descr *iod,
                            unsigned int replica_count, size_t buffer_size,
                            size_t count)
{
#define MAX_NULL_READ_TRY 10
    int nb_null_read_try = 0;
    size_t to_write = count;
    char *buffer;
    int rc = 0;

    /* alloc buffer */
    buffer = malloc(buffer_size);
    if (buffer == NULL)
        LOG_RETURN(-ENOMEM, "Unable to alloc buffer for raid1 encoder write");

    while (to_write > 0) {
        ssize_t buf_size;
        int i;

        buf_size = read(input_fd, buffer,
                        to_write > buffer_size ? buffer_size : to_write);
        if (buf_size < 0)
            LOG_GOTO(out, rc = -errno, "Error on loading buffer in raid1 write,"
                                       " %zu remaning bytes", to_write);

        if (buf_size == 0) {
            ++nb_null_read_try;
            if (nb_null_read_try > MAX_NULL_READ_TRY)
                LOG_GOTO(out, rc = -EIO, "Too many null read in raid1 write, "
                                         "%zu remaining bytes", to_write);

            continue;
        }

        /* TODO manage as async/parallel IO */
        for (i = 0; i < replica_count; ++i) {
            rc = ioa_write(ioa[i], &iod[i], buffer, buf_size);
            if (rc)
                LOG_GOTO(out, rc, "Unable to write %zu bytes in replica %d "
                                  "in raid1 write, %zu remaining bytes",
                                  buf_size, i, to_write);

            /* update written iod size */
            iod[i].iod_size += buf_size;
        }

        to_write -= buf_size;
    }

out:
    free(buffer);
    return rc;
}

/**
 * Retrieve the preferred IO size from the backend storage
 * if it was not set in the global "io" configuration.
 *
 * @param[in,out] io_size   The IO size to be used (in: value from
 *                          configuration).
 * @param[in]     ioa       IO adapter to access the current storage backend.
 * @param[in,out] iod       IO decriptor to access the current object.
 */
static void set_block_io_size(size_t *io_size,
                              const struct io_adapter_module *ioa,
                              struct pho_io_descr *iod)
{
    ssize_t sz;

    /* io_size already specified in the configuration? */
    if (*io_size != 0)
        return;

    sz = ioa_preferred_io_size(ioa, iod);
    if (sz > 0) {
        *io_size = sz;
        return;
    }

    /* fallback: get the system page size */
    *io_size = sysconf(_SC_PAGESIZE);
}

/**
 * Write extents in media provided by \a wresp and fill \a rreq release requests
 *
 * As many extents as \a enc repl_count. One per medium.
 * All written extents will have the same size limited by the minimum of
 * \a enc size to write and the minimum available size of media.
 *
 * @param[in/out] enc   raid1 encoder
 * @param[in]     wresp write media allocation response
 * @param[in/out] rreq  release requests
 *
 * @return 0 if success, else a negative error code
 */
static int multiple_enc_write_chunk(struct pho_encoder *enc,
                                    pho_resp_write_t *wresp,
                                    pho_req_release_t *rreq)
{
    struct raid1_encoder *raid1 = enc->priv_enc;
#define EXTENT_TAG_SIZE 128
    struct io_adapter_module **ioa = NULL;
    struct pho_io_descr *iod = NULL;
    struct pho_ext_loc *loc = NULL;
    struct extent *extent = NULL;
    char *extent_tag = NULL;
    char *extent_key = NULL;
    size_t extent_size;
    GString *str;
    int rc = 0;
    int i;


    /* initial checks */
    if (wresp->n_media != raid1->repl_count)
        LOG_RETURN(-EINVAL, "Received %zu media but %u were needed in write "
                            "raid1 encoder",
                            wresp->n_media, raid1->repl_count);

    if (enc->xfer->xd_fd < 0)
        LOG_RETURN(-EBADF, "Invalid encoder xfer file descriptor in write "
                            "raid1 encoder");

    /* get all ioa */
    ioa = calloc(raid1->repl_count, sizeof(*ioa));
    if (ioa == NULL)
        LOG_RETURN(-ENOMEM, "Unable to alloc ioa table in raid1 encoder write");

    for (i = 0; i < raid1->repl_count; ++i) {
        rc = get_io_adapter((enum fs_type)wresp->media[i]->fs_type, &ioa[i]);
        if (rc)
            LOG_GOTO(out, rc,
                     "Unable to get io_adapter in raid1 encoder write");
    }

    /* write size is limited by the smallest available place on all media */
    extent_size = raid1->to_write;
    for (i = 0; i < raid1->repl_count; ++i)
        if (wresp->media[i]->avail_size < extent_size)
            extent_size = wresp->media[i]->avail_size;

    /* prepare all extents */
    extent = calloc(raid1->repl_count, sizeof(*extent));
    if (extent == NULL)
        LOG_GOTO(out, rc = -ENOMEM,
                 "Unable to alloc extent table in raid1 encode write");

    for (i = 0; i < raid1->repl_count; ++i)
        set_extent_info(&extent[i], wresp->media[i],
                        raid1->cur_extent_idx * raid1->repl_count + i,
                        extent_size);
        /* extent[i]->address will be filled by ioa_open */

    /* prepare all extent_tag */
    extent_tag = calloc(raid1->repl_count,
                        sizeof(*extent_tag) * EXTENT_TAG_SIZE);
    if (extent_tag == NULL)
        LOG_GOTO(out, rc = -ENOMEM,
                 "Unable to alloc extent_tag table in raid1 write");

    for (i = 0; i < raid1->repl_count; ++i) {
        rc = snprintf(&extent_tag[i * EXTENT_TAG_SIZE], EXTENT_TAG_SIZE,
                      "r1-%u_%d", raid1->repl_count, extent[i].layout_idx);
        assert(rc < EXTENT_TAG_SIZE);
    }

    /* prepare all iod and loc */
    /* alloc iod */
    iod = calloc(raid1->repl_count, sizeof(*iod));
    if (iod == NULL)
        LOG_GOTO(out, rc = -ENOMEM,
                 "Unable to alloc iod table in raid1 encoder write");

    /* alloc loc */
    loc = calloc(raid1->repl_count, sizeof(*loc));
    if (loc == NULL)
        LOG_GOTO(out, rc = -ENOMEM,
                 "Unable to alloc loc table in raid1 encoder write");

    /**
     * Build the extent attributes from the object ID and the user provided
     * attributes. This information will be attached to backend objects for
     * "self-description"/"rebuild" purpose.
     */
    str = g_string_new(NULL);
    rc = pho_attrs_to_json(&enc->xfer->xd_attrs, str,
                           PHO_ATTR_BACKUP_JSON_FLAGS);
    if (rc)
        goto free_values;

    for (i = 0; i < raid1->repl_count; ++i) {
        /* set loc */
        loc[i].root_path = wresp->media[i]->root_path;
        loc[i].extent = &extent[i];
        loc[i].addr_type = (enum address_type)wresp->media[i]->addr_type;
        /* set iod */
        iod[i].iod_flags = PHO_IO_REPLACE | PHO_IO_NO_REUSE;
        /* iod[i].iod_fd is replaced by a buffer in open/write/close api */
        /* iod[i].iod_size starts from 0 and will be updated by each write */
        iod[i].iod_size = 0;
        iod[i].iod_loc = &loc[i];

        rc = pho_attr_set(&iod[i].iod_attrs, PHO_EA_ID_NAME,
                          enc->xfer->xd_objid);
        if (rc)
            LOG_GOTO(attrs, rc, "Unable to set iod_attrs for extent %d", i);

        if (!gstring_empty(str)) {
            rc = pho_attr_set(&iod[i].iod_attrs, PHO_EA_UMD_NAME, str->str);
            if (rc)
                LOG_GOTO(attrs, rc, "Unable to set iod_attrs for extent %d", i);
        }

        /* iod_ctx will be set by open */
    }

    /* open all iod */
    for (i = 0; i < raid1->repl_count; ++i) {
        rc = build_extent_key(enc->xfer->xd_objuuid, enc->xfer->xd_version,
                              &extent_tag[i * EXTENT_TAG_SIZE], &extent_key);
        if (rc)
            LOG_GOTO(close, rc, "Extent key build failed");

        rc = ioa_open(ioa[i], extent_key, enc->xfer->xd_objid, &iod[i], true);
        free(extent_key);
        if (rc)
            LOG_GOTO(close, rc, "Unable to open extent %s in raid1 write",
                     &extent_tag[i * EXTENT_TAG_SIZE]);

        set_block_io_size(&enc->io_block_size, ioa[i], &iod[i]);
        pho_debug("I/O size for replicate %d: %zu", i, enc->io_block_size);
    }

    /* write all extents by chunk of buffer size*/
    rc = write_all_chunks(enc->xfer->xd_fd, ioa, iod,
                          raid1->repl_count, enc->io_block_size,
                          extent_size);
    if (rc)
        LOG_GOTO(close, rc, "Unable to write in raid1 encoder write");

close:
    for (i = 0; i < raid1->repl_count; ++i) {
        int rc2;

        rc2 = ioa_close(ioa[i], &iod[i]);
        if (!rc && rc2)
            rc = rc2;
    }

    /* update size in write encoder */
    if (rc == 0) {
        raid1->to_write -= extent_size;
        raid1->cur_extent_idx++;
    }

    /* update all release requests */
    for (i = 0; i < raid1->repl_count; ++i) {
        rreq->media[i]->rc = rc;
        rreq->media[i]->size_written = iod[i].iod_size;
    }

    /* add all written extents */
    if (!rc)
        for (i = 0; i < raid1->repl_count; ++i)
            add_written_extent(raid1, &extent[i]);

attrs:
    for (i = 0; i < raid1->repl_count; ++i)
        pho_attrs_free(&iod[i].iod_attrs);

free_values:
    g_string_free(str, TRUE);

out:
    free(loc);
    free(iod);
    free(extent_tag);
    free(extent);
    free(ioa);
    return rc;
}

/**
 * Read the data specified by \a extent from \a medium into the output fd of
 * dec->xfer.
 */
static int simple_dec_read_chunk(struct pho_encoder *dec,
                                 const pho_resp_read_elt_t *medium)
{
    struct raid1_encoder *raid1 = dec->priv_enc;
    struct io_adapter_module *ioa;
    struct pho_io_descr iod = {0};
    struct extent *extent = NULL;
    struct pho_ext_loc loc = {0};
    char *extent_key = NULL;
    int rc;
    int i;


    /* find good extent among replica count */
    for (i = 0; i < raid1->repl_count ; i++) {
        int extent_index = raid1->cur_extent_idx * raid1->repl_count + i;
        struct extent *candidate_extent = &dec->layout->extents[extent_index];

        /* layout extents should be well ordered */
        if (candidate_extent->layout_idx != extent_index)
            LOG_RETURN(-EINVAL, "In raid1 layout decoder read, layout extents "
                                "must be ordered, layout extent %d has "
                                "layout_idx %d",
                                extent_index, candidate_extent->layout_idx);
        assert(candidate_extent->layout_idx == extent_index);
        if (strcmp(medium->med_id->name, candidate_extent->media.name) == 0) {
            extent = candidate_extent;
            break;
        }
    }

    /* No matching extent ? */
    if (extent == NULL)
        LOG_RETURN(-EINVAL, "raid1 layout received a medium to read not in "
                            "layout extents list");

    /*
     * NOTE: fs_type is not stored as an extent attribute in db, therefore it
     * is not retrieved when retrieving a layout either. It is currently a field
     * of a medium, this is why the LRS provides it in its response. This may be
     * intentional, or to be fixed later.
     */
    rc = get_io_adapter((enum fs_type)medium->fs_type, &ioa);
    if (rc)
        return rc;

    loc.root_path = medium->root_path;
    loc.extent = extent;
    loc.addr_type = (enum address_type)medium->addr_type;

    iod.iod_fd = dec->xfer->xd_fd;
    if (iod.iod_fd < 0)
        LOG_RETURN(rc = -EBADF, "Invalid decoder xfer file descriptor");

    iod.iod_size = loc.extent->size;
    iod.iod_loc = &loc;

    pho_debug("Reading %ld bytes from medium %s", extent->size,
              extent->media.name);

    rc = build_extent_key(dec->xfer->xd_objuuid, dec->xfer->xd_version, NULL,
                          &extent_key);
    if (rc)
        LOG_RETURN(rc, "Extent key build failed");

    rc = ioa_get(ioa, extent_key, dec->xfer->xd_objid, &iod);
    free(extent_key);
    if (rc == 0) {
        raid1->to_write -= extent->size;
        raid1->cur_extent_idx++;
    }

    /* Nothing more to write: the decoder is done */
    if (raid1->to_write <= 0) {
        pho_debug("Decoder for '%s' is now done", dec->xfer->xd_objid);
        dec->done = true;
    }

    return rc;
}

/**
 * When receiving a release response, check from raid1->to_release_media that
 * we expected this response. Decrement refcount and increment
 * raid1->n_released_media.
 */
static int mark_written_medium_released(struct raid1_encoder *raid1,
                                       const char *medium)
{
    size_t *to_release_refcount;

    to_release_refcount = g_hash_table_lookup(raid1->to_release_media, medium);

    if (to_release_refcount == NULL)
        return -EINVAL;

    /* media id with refcount of zero must be removed from the hash table */
    assert(*to_release_refcount > 0);

    /* one medium was released */
    raid1->n_released_media++;

    /* only one release was ongoing for this medium: remove from the table */
    if (*to_release_refcount == 1) {
        gboolean was_in_table;

        was_in_table = g_hash_table_remove(raid1->to_release_media, medium);
        assert(was_in_table);
        return 0;
    }

    /* several current releases: only decrement refcount */
    --(*to_release_refcount);
    return 0;
}

/**
 * Handle a release response for an encoder (unrelevent for a decoder) by
 * remembering that these particular media have been released. If all data has
 * been written and all written media have been released, mark the encoder as
 * done.
 */
static int raid1_enc_handle_release_resp(struct pho_encoder *enc,
                                         pho_resp_release_t *rel_resp)
{
    struct raid1_encoder *raid1 = enc->priv_enc;
    int rc = 0;
    int i;

    for (i = 0; i < rel_resp->n_med_ids; i++) {
        int rc2;

        pho_debug("Marking medium %s as released", rel_resp->med_ids[i]->name);
        /* If the media_id is unexpected, -EINVAL will be returned */
        rc2 = mark_written_medium_released(raid1, rel_resp->med_ids[i]->name);
        if (rc2 && !rc)
            rc = rc2;
    }

    /*
     * If we wrote everything and all the releases have been received, mark the
     * encoder as done.
     */
    if (raid1->to_write == 0 && /* no more data to write */
            /* at least one extent is created, special test for null size put */
            raid1->written_extents->len > 0 &&
            /* we got releases of all extents */
            raid1->written_extents->len == raid1->n_released_media) {
        /* Fill the layout with the extents */
        enc->layout->ext_count = raid1->written_extents->len;
        enc->layout->extents =
            (struct extent *)g_array_free(raid1->written_extents, FALSE);
        raid1->written_extents = NULL;
        raid1->n_released_media = 0;
        g_hash_table_destroy(raid1->to_release_media);
        raid1->to_release_media = NULL;
        enc->layout->state = PHO_EXT_ST_SYNC;

        /* Switch to DONE state */
        enc->done = true;
        return 0;
    }

    return rc;
}

/** Generate the next write allocation request for this encoder */
static int raid1_enc_next_write_req(struct pho_encoder *enc, pho_req_t *req)
{
    struct raid1_encoder *raid1 = enc->priv_enc;
    size_t *n_tags;
    int rc = 0, i, j;

    /* n_tags array */
    n_tags = calloc(raid1->repl_count, sizeof(*n_tags));
    if (n_tags == NULL)
        LOG_RETURN(-ENOMEM, "unable to alloc n_tags array in raid1 layout "
                            "write alloc");

    for (i = 0; i < raid1->repl_count; ++i)
        n_tags[i] = enc->xfer->xd_params.put.tags.n_tags;

    rc = pho_srl_request_write_alloc(req, raid1->repl_count, n_tags);
    free(n_tags);
    if (rc)
        return rc;

    for (i = 0; i < raid1->repl_count; ++i) {
        req->walloc->media[i]->size = raid1->to_write;

        for (j = 0; j < enc->xfer->xd_params.put.tags.n_tags; ++j)
            req->walloc->media[i]->tags[j] =
                strdup(enc->xfer->xd_params.put.tags.tags[j]);
    }

    return rc;
}

/** Generate the next read allocation request for this decoder */
static int raid1_dec_next_read_req(struct pho_encoder *dec, pho_req_t *req)
{
    struct raid1_encoder *raid1 = dec->priv_enc;
    int rc = 0;
    int i;

    rc = pho_srl_request_read_alloc(req, raid1->repl_count);
    if (rc)
        return rc;

    /* To read, raid1 needs only one among all copies */
    req->ralloc->n_required = 1;

    for (i = 0; i < raid1->repl_count; ++i) {
        unsigned int ext_idx = raid1->cur_extent_idx * raid1->repl_count + i;

        pho_debug("Requesting medium %s to read copy %d of extent %d",
                  dec->layout->extents[ext_idx].media.name,
                  i, raid1->cur_extent_idx);
        req->ralloc->med_ids[i]->family =
            dec->layout->extents[ext_idx].media.family;
        req->ralloc->med_ids[i]->name =
            strdup(dec->layout->extents[ext_idx].media.name);
    }

    return 0;
}

/**
 * Handle one response from the LRS and potentially generate one response.
 */
static int raid1_enc_handle_resp(struct pho_encoder *enc, pho_resp_t *resp,
                                 pho_req_t **reqs, size_t *n_reqs)
{
    struct raid1_encoder *raid1 = enc->priv_enc;
    int rc = 0, i;

    if (pho_response_is_error(resp)) {
        enc->xfer->xd_rc = resp->error->rc;
        enc->done = true;
        pho_error(enc->xfer->xd_rc,
                  "%s for objid:'%s' received error %s to last request",
                  enc->is_decoder ? "Decoder" : "Encoder", enc->xfer->xd_objid,
                  pho_srl_error_kind_str(resp->error));
    } else if (pho_response_is_write(resp)) {
        /* Last requested allocation has now been fulfilled */
        raid1->requested_alloc = false;
        if (enc->is_decoder)
            return -EINVAL;

        if (resp->walloc->n_media != raid1->repl_count)
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

        for (i = 0; i < resp->walloc->n_media; ++i) {
            rsc_id_cpy((*reqs)[*n_reqs].release->media[i]->med_id,
                       resp->walloc->media[i]->med_id);
            (*reqs)[*n_reqs].release->media[i]->to_sync = true;
        }

        /* XXX we can set to_sync to false when an error occurs here */
        /* Perform IO and populate release request with the outcome */
        rc = multiple_enc_write_chunk(enc, resp->walloc,
                                      (*reqs)[*n_reqs].release);
        (*n_reqs)++;
    } else if (pho_response_is_read(resp)) {
        /* Last requested allocation has now been fulfilled */
        raid1->requested_alloc = false;
        if (!enc->is_decoder)
            return -EINVAL;

        if (resp->ralloc->n_media != 1)
            return -EINVAL;

        /* Build release req matching this allocation response */
        rc = pho_srl_request_release_alloc(*reqs + *n_reqs,
                                           resp->ralloc->n_media);
        if (rc)
            return rc;

        /* copy medium id from allocation response to release request */
        rsc_id_cpy((*reqs)[*n_reqs].release->media[0]->med_id,
                   resp->ralloc->media[0]->med_id);

        /* Perform IO and populate release request with the outcome */
        rc = simple_dec_read_chunk(enc, resp->ralloc->media[0]);
        (*reqs)[*n_reqs].release->media[0]->rc = rc;
        (*reqs)[*n_reqs].release->media[0]->to_sync = false;
        (*n_reqs)++;
    } else if (pho_response_is_release(resp)) {
        /* Decoders don't need to keep track of medium releases */
        if (!enc->is_decoder)
            rc = raid1_enc_handle_release_resp(enc, resp->release);

    } else {
        LOG_RETURN(rc = -EPROTO, "Invalid response type");
    }

    return rc;
}

static bool no_more_alloc(const struct pho_encoder *enc)
{
    const struct raid1_encoder *raid1 = enc->priv_enc;

    /* ended encoder */
    if (enc->done)
        return true;

    /* still something to write */
    if (raid1->to_write > 0)
        return false;

    /* decoder with no more to read */
    if (enc->is_decoder)
        return true;

    /* encoder with no more to write and at least one written extent */
    if (raid1->written_extents->len > 0)
        return true;

    /* encoder with no more to write but needing to write at least one extent */
    return false;
}

/**
 * Raid1 layout implementation of the `step` method.
 * (See `layout_step` doc)
 */
static int raid1_encoder_step(struct pho_encoder *enc, pho_resp_t *resp,
                              pho_req_t **reqs, size_t *n_reqs)
{
    struct raid1_encoder *raid1 = enc->priv_enc;
    int rc = 0;

    /* At max 2 requests will be emitted, allocate optimistically */
    *reqs = calloc(2, sizeof(**reqs));
    if (*reqs == NULL)
        return -ENOMEM;
    *n_reqs = 0;

    /* Handle a possible response */
    if (resp != NULL)
        rc = raid1_enc_handle_resp(enc, resp, reqs, n_reqs);

    /* Do we need to generate a new alloc ? */
    if (rc || /* an error happened */
        raid1->requested_alloc || /* an alloc was already requested */
        no_more_alloc(enc))
        goto out;

    /* Build next request */
    if (enc->is_decoder)
        rc = raid1_dec_next_read_req(enc, *reqs + *n_reqs);
    else
        rc = raid1_enc_next_write_req(enc, *reqs + *n_reqs);

    if (rc)
        return rc;

    (*n_reqs)++;
    raid1->requested_alloc = true;

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
static void raid1_encoder_destroy(struct pho_encoder *enc)
{
    struct raid1_encoder *raid1 = enc->priv_enc;

    if (raid1 == NULL)
        return;

    if (raid1->written_extents != NULL) {
        g_array_free(raid1->written_extents, TRUE);
        raid1->written_extents = NULL;
    }

    if (raid1->to_release_media != NULL) {
        g_hash_table_destroy(raid1->to_release_media);
        raid1->to_release_media = NULL;
    }

    free(raid1);
    enc->priv_enc = NULL;
}

static const struct pho_enc_ops RAID1_ENCODER_OPS = {
    .step       = raid1_encoder_step,
    .destroy    = raid1_encoder_destroy,
};

/**
 * Create an encoder.
 *
 * This function initializes the internal raid1_encoder based on enc->xfer and
 * enc->layout.
 *
 * Implements the layout_encode layout module methods.
 */
static int layout_raid1_encode(struct pho_encoder *enc)
{
    struct raid1_encoder *raid1 = calloc(1, sizeof(*raid1));
    const char *string_repl_count = NULL;
    int rc;

    if (raid1 == NULL)
        return -ENOMEM;

    /*
     * The ops field is set early to allow the caller to call the destroy
     * function on error.
     */
    enc->ops = &RAID1_ENCODER_OPS;
    enc->priv_enc = raid1;

    /* Initialize raid1-specific state */
    raid1->cur_extent_idx = 0;
    raid1->requested_alloc = false;

    /* The layout description has to be set on encoding */
    enc->layout->layout_desc = RAID1_MODULE_DESC;

    if (!pho_attrs_is_empty(&enc->xfer->xd_params.put.lyt_params))
        string_repl_count = pho_attr_get(&enc->xfer->xd_params.put.lyt_params,
                                         REPL_COUNT_ATTR_KEY);

    if (string_repl_count == NULL) {
        /* get repl_count from conf */
        string_repl_count = PHO_CFG_GET(cfg_lyt_raid1, PHO_CFG_LYT_RAID1,
                                        repl_count);
        if (string_repl_count == NULL)
            LOG_RETURN(-EINVAL, "Unable to get replica count from conf to "
                                "build a raid1 encoder");
    }

    /* set repl_count as char * in layout */
    rc = pho_attr_set(&enc->layout->layout_desc.mod_attrs,
                      REPL_COUNT_ATTR_KEY, string_repl_count);
    if (rc)
        LOG_RETURN(rc, "Unable to set raid1 layout repl_count attr in "
                       "encoder built");

    /* set repl_count in encoder */
    rc = layout_repl_count(enc->layout, &raid1->repl_count);
    if (rc)
        LOG_RETURN(rc, "Invalid replica count from layout to build raid1 "
                       "encoder");

    /* set write size */
    if (raid1->repl_count <= 0)
        LOG_RETURN(-EINVAL, "Invalid # of replica (%d)", raid1->repl_count);

    if (enc->xfer->xd_params.put.size < 0)
        LOG_RETURN(-EINVAL, "bad input encoder size to write when building "
                            "raid1 encoder");

    raid1->to_write = enc->xfer->xd_params.put.size;

    /* Allocate the extent array */
    raid1->written_extents = g_array_new(FALSE, TRUE,
                                         sizeof(struct extent));
    g_array_set_clear_func(raid1->written_extents,
                           free_extent_address_buff);
    raid1->to_release_media = g_hash_table_new_full(g_str_hash, g_str_equal,
                                                    free, free);
    raid1->n_released_media = 0;

    return 0;
}

/**
 * Create a decoder.
 *
 * This function initializes the internal raid1_encoder based on enc->xfer and
 * enc->layout.
 *
 * Implements layout_decode layout module methods.
 */
static int layout_raid1_decode(struct pho_encoder *enc)
{
    struct raid1_encoder *raid1;
    int rc;
    int i;

    if (!enc->is_decoder)
        LOG_RETURN(-EINVAL, "ask to create a decoder on an encoder");

    raid1 = calloc(1, sizeof(*raid1));
    if (raid1 == NULL)
        return -ENOMEM;

    /*
     * The ops field is set early to allow the caller to call the destroy
     * function on error.
     */
    enc->ops = &RAID1_ENCODER_OPS;
    enc->priv_enc = raid1;

    /* Initialize raid1-specific state */
    raid1->cur_extent_idx = 0;
    raid1->requested_alloc = false;
    raid1->written_extents = NULL;
    raid1->to_release_media = NULL;
    raid1->n_released_media = 0;

    /* Fill out the encoder appropriately */
    /* set decoder repl_count */
    rc = layout_repl_count(enc->layout, &raid1->repl_count);
    if (rc)
        LOG_RETURN(rc, "Invalid replica count from layout to build raid1 "
                       "decoder");

    /*
     * Size is the sum of the extent sizes, enc->layout->wr_size is not
     * positioned properly by the dss
     */
    if (enc->layout->ext_count % raid1->repl_count != 0)
        LOG_RETURN(-EINVAL, "layout extents count (%d) is not a multiple "
                   "of replica count (%u)",
                   enc->layout->ext_count, raid1->repl_count);

    /* set read size  : badly named "to_write" */
    raid1->to_write = 0;
    for (i = 0; i < enc->layout->ext_count / raid1->repl_count; i++)
        raid1->to_write += enc->layout->extents[i * raid1->repl_count].size;

    /* Empty GET does not need any IO */
    if (raid1->to_write == 0) {
        enc->done = true;
        if (enc->xfer->xd_fd < 0)
            LOG_RETURN(-EBADF, "Invalid encoder xfer file descriptor in empty "
                               "GET decode create");
    }

    return 0;
}

/** Stores the possible locations of an object split */
struct split_access_info {
    unsigned int repl_count;
    unsigned int nb_hosts;
    /** The following fields are arrays of maximum length repl_count, currently
     * filled with nb_hosts values, with each element corresponding to the
     * same extent (usable[0], hostnames[0] and tape_model[0] all concern the
     * same extent)
     */
    bool *usable;      /**< false if the extent is on an unusable medium
                         * (administrative lock, forbidden get access, ...),
                         * true otherwise
                         */
    char **hostnames;  /**< The hostname of the medium on which the extent is
                         * if it has a concurrency lock, otherwise NULL to
                         * indicate the medium is unlocked
                         */
    char **tape_model; /**< The tape model if the extent is on a tape medium,
                         * NULL if not
                         */
};

/** Host resource access info, stores all the hosts which have access to a
 * medium that contains an extent of the object we're trying to locate
 */
struct host_rsc_access_info {
    char *hostname; /**< hostname of the host */
    unsigned int nb_locked_splits; /**< nb split with extent locked on the
                                     * host
                                     */
    unsigned int nb_unreachable_split; /**< splits that this host can't access:
                                         * locked by someone else, admin lock,
                                         * no get access, ...
                                         */
    int dev_count; /**< number of administratively unlocked devices the host has
                     * access to
                     */
    char **dev_models; /**< array of dev_count administratively unlocked device
                         * models the host has access to. Allocated by
                         * dss_device_get and must be freed by dss_res_free.
                         */
};

/** Stores the possible locations of an object */
struct object_location {
    unsigned int split_count;
    unsigned int repl_count;
    unsigned int nb_hosts;
    /**< array of maximum (split_count * repl_count) + 1 candidates, first
     * nb_hosts are filled.
     *
     * This array contains all different hosts that own at least one unlocked
     * devices of the same type as the object, with their hostname and their
     * score.
     *
     * focus_host is set as first location with an initial nb_locked_splits of
     * 0.
     *
     * Each hostname is present only once, even if it owns locks on several
     * media containing extents.
     */
    struct host_rsc_access_info *hosts;
    /**< array of split_count split_access_infos
     *
     * Hostnames are listed by split to compute their scores.
     */
    struct split_access_info *split_accesses;
};

static int init_split_access_info(struct split_access_info *split,
                                  unsigned int repl_count)
{
    split->repl_count = repl_count;
    split->nb_hosts = 0;
    split->usable = calloc(repl_count, sizeof(*split->usable));
    if (!split->usable)
        return -ENOMEM;

    split->hostnames = calloc(repl_count, sizeof(*split->hostnames));
    if (!split->hostnames) {
        free(split->usable);
        return -ENOMEM;
    }

    split->tape_model = calloc(repl_count, sizeof(*split->tape_model));
    if (!split->tape_model) {
        free(split->usable);
        free(split->hostnames);
        return -ENOMEM;
    }

    return 0;
}

/*
 * We don't free each hostname entry of the split->hostnames array because a
 * split_access_info does not own these strings. They are owned by each
 * host_rsc_access_info of an object_location.
 *
 * We only free the split->hostnames array which was dynamically allocated.
 */
static void clean_split_access_info(struct split_access_info *split)
{
    int i;

    free(split->usable);
    split->usable = NULL;
    free(split->hostnames);
    split->hostnames = NULL;
    for (i = 0; i < split->repl_count; ++i)
        free(split->tape_model[i]);
    free(split->tape_model);
    split->tape_model = NULL;
    split->repl_count = 0;
    split->nb_hosts = 0;
}

static void split_access_info_add_host(struct split_access_info *split,
                                       char *hostname, char *tape_model,
                                       bool usable)
{
    assert(split->nb_hosts < split->repl_count);
    split->usable[split->nb_hosts] = usable;
    split->hostnames[split->nb_hosts] = hostname;
    split->tape_model[split->nb_hosts] = tape_model;
    split->nb_hosts++;
}

static void clean_host_rsc_access_info(struct host_rsc_access_info *host)
{
    int i;

    free(host->hostname);
    host->hostname = NULL;
    host->nb_locked_splits = 0;
    host->nb_unreachable_split = 0;
    for (i = 0; i < host->dev_count; ++i)
        free(host->dev_models[i]);
    free(host->dev_models);
    host->dev_models = NULL;
    host->dev_count = 0;
}

static void clean_object_location(struct object_location *object_location)
{
    unsigned int i;

    object_location->repl_count = 0;
    if (object_location->hosts) {
        for (i = 0; i < object_location->nb_hosts; i++)
            clean_host_rsc_access_info(&object_location->hosts[i]);

        free(object_location->hosts);
        object_location->hosts = NULL;
    }

    object_location->nb_hosts = 0;

    if (object_location->split_accesses) {
        for (i = 0; i < object_location->split_count; i++)
            clean_split_access_info(&object_location->split_accesses[i]);

        free(object_location->split_accesses);
        object_location->split_accesses = NULL;
    }

    object_location->split_count = 0;
}

static int init_object_location(struct dss_handle *dss,
                                struct object_location *object_location,
                                unsigned int split_count,
                                unsigned int repl_count,
                                const char *focus_host,
                                enum rsc_family family)
{
    struct dev_info *devs;
    unsigned int i;
    unsigned int j;
    int dev_count;
    int rc;

    /* in case of early clean on error: ensure a NULL pointer value */
    object_location->hosts = NULL;
    object_location->split_accesses = NULL;

    object_location->split_count = split_count;
    object_location->repl_count = repl_count;

    object_location->split_accesses =
        calloc(split_count, sizeof(*object_location->split_accesses));
    if (!object_location->split_accesses)
        GOTO(clean, rc = -ENOMEM);

    for (i = 0; i < object_location->split_count; i++) {
        rc = init_split_access_info(&object_location->split_accesses[i],
                                    object_location->repl_count);
        if (rc)
            GOTO(clean, rc = -ENOMEM);
    }

    /* Retrieve all the unlocked devices of the correct family in the DB */
    rc = dss_get_usable_devices(dss, family, NULL, &devs, &dev_count);
    if (rc)
        GOTO(clean, rc);

    /* Init the number of hosts known to 1 to account for focus_host */
    object_location->nb_hosts = 1;

    for (i = 0; i < dev_count; ++i) {
        bool seen = false;

        /* If the host of the device is focus_host, skip it as we already
         * accounted for it
         */
        if (!strcmp(focus_host, devs[i].host))
            continue;

        /* Check if the host has been seen before in the device list */
        for (j = 0; j < i; ++j) {
            if (!strcmp(devs[j].host, devs[i].host)) {
                seen = true;
                break;
            }
        }

        /* If the host was not seen, increase the number of known host */
        if (!seen)
            object_location->nb_hosts++;
    }

    /* Allocate as many hosts as we found */
    object_location->hosts = calloc(object_location->nb_hosts,
                                    sizeof(*object_location->hosts));
    if (!object_location->hosts)
        GOTO(clean, rc = -ENOMEM);

    /* Consider the first known host to be focus_host */
    object_location->hosts[0].hostname = strdup(focus_host);
    if (!object_location->hosts[0].hostname)
        GOTO(clean, rc = -ENOMEM);
    object_location->hosts[0].dev_count = 0;
    object_location->hosts[0].dev_models = NULL;

    /* focus_host is already added to the list, so start adding hosts at the
     * first index
     */
    object_location->nb_hosts = 1;

    /* For each drive, we check its host and increase its device count, so
     * that at the end of this loop, we know how many usable devices each host
     * has
     */
    for (i = 0; i < dev_count; ++i) {
        struct host_rsc_access_info *host_to_add =
            &object_location->hosts[object_location->nb_hosts];
        char *drive_host = devs[i].host;
        bool seen = false;

        /* Check if the host of the device has been seen before, and if so,
         * increase its dev_count by 1
         */
        for (j = 0; j < object_location->nb_hosts; ++j) {
            if (!strcmp(object_location->hosts[j].hostname, drive_host)) {
                object_location->hosts[j].dev_count += 1;
                seen = true;
                break;
            }
        }

        /* If the device's host was not seen in the list of hosts, add it to
         * the latter, and consider it has one device
         */
        if (!seen) {
            host_to_add->hostname = strdup(drive_host);
            if (host_to_add == NULL)
                GOTO(clean, rc = -errno);

            host_to_add->dev_count = 1;
            object_location->nb_hosts++;
        }
    }

    /* At this point, we know every host that has an unlocked device of the
     * correct family, and for each of them, their number of devices.
     */

    for (i = 0; i < object_location->nb_hosts; ++i) {
        struct host_rsc_access_info *host_to_update =
            &object_location->hosts[i];

        /* If a known host doesn't have any device, skip it (this can only be
         * the case for focus_host, as we are not sure it has any device
         * available
         */
        if (host_to_update->dev_count == 0)
            continue;

        /* Allocate the correct amount of devices for that host */
        host_to_update->dev_models =
            malloc(host_to_update->dev_count *
                   sizeof(*host_to_update->dev_models));
        if (!host_to_update->dev_models)
            GOTO(clean, rc = -ENOMEM);

        /* Reset their number of devices just for proper filling of devices in
         * the next loop
         */
        host_to_update->dev_count = 0;
    }

    /* For each device found in the DB, associate it to its host and fill
     * the corresponding dev_models
     */
    for (i = 0; i < dev_count; ++i) {
        struct host_rsc_access_info *host_to_update = NULL;
        char *drive_host = devs[i].host;

        /* Find the host the current device belongs to */
        for (j = 0; j < object_location->nb_hosts; ++j) {
            if (!strcmp(object_location->hosts[j].hostname, drive_host)) {
                host_to_update = &object_location->hosts[j];
                break;
            }
        }

        /* For that host, if the device has a known model, duplicate it */
        if (devs[i].rsc.model == NULL) {
            host_to_update->dev_models[host_to_update->dev_count] = NULL;
        } else {
            host_to_update->dev_models[host_to_update->dev_count] =
                strdup(devs[i].rsc.model);
            if (!host_to_update->dev_models[host_to_update->dev_count])
                GOTO(clean, rc = -ENOMEM);
        }

        /* And increase its device counter by 1 */
        host_to_update->dev_count++;
    }

    /* Free the list of devices found in the DB, as it isn't used anymore */
    dss_res_free(devs, dev_count);

    /* success */
    return 0;

clean:
    dss_res_free(devs, dev_count);
    clean_object_location(object_location);
    return rc;
}

static bool object_location_contains_host(
    struct object_location *object_location, const char *hostname,
    unsigned int *index)
{
    int i;

    if (hostname == NULL)
        return false;

    for (i = 0; i < object_location->nb_hosts; i++) {
        if (!strcmp(object_location->hosts[i].hostname, hostname)) {
            *index = i;
            return true;
        }
    }

    return false;
}

/**
 * Add a new extent record to the split_access_info at \p split_index in
 * \p object_location.
 * This function takes the ownership of the allocated char *hostname.
 *
 * Three different cases:
 * 1) If \p hostname is NULL, that means the medium holding that extent doesn't
 * have a concurrency lock, so it is recorded but not attributed to any host.
 * 2) \p hostname is not NULL, and already has a corresponding
 * host_rsc_access_info structure in use, in which case we record the extent,
 * increase the number of fitted_split for that host, and free \p hostname.
 * 3) \p hostname is not NULL and no corresponding host_rsc_access_info
 * structure is in use, in which case we record the extent and create a
 * new host_rsc_access_info in \p object_location for it, which takes ownership
 * of \p hostname.
 */
static int add_host_to_object_location(struct object_location *object_location,
                                       struct dss_handle *dss, char *hostname,
                                       char *tape_model, bool usable,
                                       unsigned int split_index,
                                       bool *split_known_by_host)
{
    struct split_access_info *split =
        &object_location->split_accesses[split_index];
    unsigned int i;

    /* If NULL, simply add the extent to the current split */
    if (hostname == NULL) {
        split_access_info_add_host(split, hostname, tape_model, usable);
        return 0;
    }

    /* If not NULL, check whether it is already a known host or not. If so,
     * increase the number of already locked split for that host, and add the
     * extent to the current split.
     */
    if (object_location_contains_host(object_location, hostname, &i)) {
        if (!split_known_by_host[i]) {
            object_location->hosts[i].nb_locked_splits += 1;
            split_known_by_host[i] = true;
        }
        split_access_info_add_host(split, object_location->hosts[i].hostname,
                                   tape_model, usable);
        free(hostname);
        return 0;
    }

    return -EINVAL;
}

/**
* Find the best location to get an object if any or NULL.
*
* The choice is made following two criteria:
* - first, the most important one, being the location with the minimum number
*   of splits that cannot be accessed (all extents of this split are locked by
*   other hostnames) or have no compatible drive for any media with unlocked
*   extents,
* - second, being the location with the maximum number of splits that can be
*   efficiently accessed (with at least one medium locked by this hostname).
*
* Focus host is first evaluated among candidates and is always returned in case
* of "equality" with an other candidate.
*
* XXX: there is currently no consideration for which host has the most available
* devices to use. If there are 2 hosts that can access the object in an equal
* fashion, but one has more devices usable, it is not assured that it will be
* returned as the best host for this locate.
*/
static void get_best_object_location(
    struct object_location *object_location,
    struct host_rsc_access_info **best_location)
{
    bool *has_access;
    unsigned int i;
    unsigned int j;
    unsigned int k;
    unsigned int l;

    /* This bool array will keep track of which host can access the current
     * split.
     */
    has_access = malloc(object_location->nb_hosts * sizeof(*has_access));

    /* The following loop aims to properly update the nb_unreachable_splits of
     * each host by going through each split, and checking which host can access
     * any extent. All the hosts that do not have access to any medium
     * containing an extent of the split will have their nb_unreachable_splits
     * increased.
     */
    for (i = 0; i < object_location->split_count; i++) {
        struct split_access_info *split = &object_location->split_accesses[i];

        /* Initialize the array to false, meaning no host can access that split
         * currently.
         */
        for (j = 0; j < object_location->nb_hosts; j++)
            has_access[j] = false;

        /* For each replica of the split */
        for (j = 0; j < split->repl_count; j++) {
            /* If the replica is unusable (administrative lock for instance),
             * skip it.
             */
            if (!split->usable[j])
                continue;

            /* If the replica has a concurrency lock, indicate the corresponding
             * host has access to that split, and continue to the next replica.
             */
            if (object_location_contains_host(object_location,
                                              split->hostnames[j], &k)) {
                has_access[k] = true;
                continue;
            }

            /* If the replica is NOT a tape and has no concurrency lock (as
             * checked by the above if), consider no host has access to
             * it.
             *
             * XXX: This behaviour only works because currently, dir and rados
             * pools cannot be locked by another host, only tapes can. This will
             * have to be changed in the future when other media can be locked
             * by any hosts.
             */
            if (split->tape_model[j] == NULL)
                continue;

            /* If the replica is unlocked, for every host that does not have
             * access to the current split, check if they have any drive
             * compatible with the tape the replica is on. If they have, flag
             * the host as having access to the split
             */
            for (k = 0; k < object_location->nb_hosts; k++) {
                if (has_access[k])
                    continue;

                for (l = 0; l < object_location->hosts[k].dev_count; l++) {
                    bool compat = false;
                    int rc;

                    rc = tape_drive_compat_models(split->tape_model[j],
                            object_location->hosts[k].dev_models[l],
                            &compat);
                    if (rc)
                        pho_error(rc,
                                  "Failed to determine compatibility between "
                                  "drive '%s' and tape '%s' for host '%s'",
                                  object_location->hosts[k].dev_models[l],
                                  split->tape_model[j],
                                  object_location->hosts[k].hostname);

                    if (compat)
                        has_access[k] = true;
                }
            }
        }

        /* Finally, for each host known by the object, check if it has access
         * to the current split. If not, increase its nb_unreachable_split
         * value.
         */
        for (j = 0; j < object_location->nb_hosts; j++)
            if (has_access[j] == false)
                object_location->hosts[j].nb_unreachable_split++;
    }

    free(has_access);

    /* default best is focus_host which is the first host */
    *best_location = object_location->hosts;
    /* get best one */
    for (i = 1; i < object_location->nb_hosts; i++) {
        struct host_rsc_access_info *candidate = object_location->hosts+i;

        /*
         * First, we compare nb_unreachable_split. Then, only if
         * nb_unreachable_split are equal, we compare nb_locked_splits.
         */
        if ((*best_location)->nb_unreachable_split >
             candidate->nb_unreachable_split ||
             ((*best_location)->nb_unreachable_split ==
              candidate->nb_unreachable_split &&
              (*best_location)->nb_locked_splits < candidate->nb_locked_splits))
            *best_location = candidate;
    }
}

/**
 * Take lock on \a layout for \a best_location
 *
 * We ensure that \a best_location has at least one lock on one extent of each
 * split of the \a layout.
 * If \a best_location already has a lock on one of the extent of one split, we
 * don't take an other lock on an other extent of the same split.
 *
 * If we can't successfully take a lock on each split of the object, this
 * function does not take any new lock.
 *
 * @param[in]   dss             dss handle
 * @param[in]   layout          layout on the object to locate
 * @param[in]   repl_count      replica count of \a layout
 * @param[in]   nb_split        number of split of \a layout
 * @param[in]   object_location current object_location of this locate call
 * @param[in]   best_location   best_location of this current locate call
 * @param[out]  nb_new_lock     number of lock taken by this function
 *                              (unrelevant if -EAGAIN is returned)
 *
 * @return 0 if success, -EAGAIN if we can't successfully take a lock on each
 *         split of the object.
 */
static int raid1_lock_at_locate(struct dss_handle *dss,
                                struct layout_info *layout,
                                unsigned int repl_count,
                                unsigned int nb_split,
                                struct object_location *object_location,
                                struct host_rsc_access_info *best_location,
                                int *nb_new_lock)
{
    unsigned int new_lock_extent_index[layout->ext_count];
    unsigned int nb_already_locked;
    unsigned int extent_index;
    unsigned int split_index;

    /* early locking of each split */
    nb_already_locked = best_location->nb_locked_splits;
    *nb_new_lock = 0;
    for (split_index = 0;
         best_location->nb_locked_splits < nb_split && split_index < nb_split;
         split_index++) {
        /* check if this split is already a locked one for this best_location */
        if (nb_already_locked > 0) {
            struct split_access_info *split =
                &object_location->split_accesses[split_index];
            unsigned int host_index;

            for (host_index = 0; host_index < split->nb_hosts; host_index++)
                if (split->hostnames[host_index] != NULL &&
                    !strcmp(split->hostnames[host_index],
                            best_location->hostname))
                    break;

            if (host_index < split->nb_hosts) {
                /*
                 * Because if we already come across all locked media we don't
                 * need to check it anymore, we sustain the remaining number of
                 * already locked media.
                 */
                nb_already_locked--;
                continue;
            }
        }

        /* try to lock an available medium */
        for (extent_index = split_index * repl_count;
             extent_index < (split_index + 1) * repl_count &&
                 extent_index < layout->ext_count;
             extent_index++) {
            struct pho_id *medium_id = &layout->extents[extent_index].media;
            char *extent_hostname = NULL;
            int rc2;

            rc2 = dss_medium_locate(dss, medium_id, &extent_hostname, NULL);
            if (rc2) {
                pho_warn("Error %d (%s) at early locking when trying to dss "
                         "locate medium at early lock (family %s, name %s) of "
                         "with extent %d raid1 layout early locking leans on "
                         "other extents", -rc2, strerror(-rc2),
                         rsc_family2str(medium_id->family), medium_id->name,
                         extent_index);
                continue;
            }

            if (!extent_hostname) {
                struct media_info target_medium;

                target_medium.rsc.id = *medium_id;
                rc2 = dss_lock_hostname(dss, DSS_MEDIA, &target_medium, 1,
                                        best_location->hostname);
                if (rc2 == -EEXIST) {
                    /* lock was concurrently taken */
                    continue;
                } else if (rc2) {
                    pho_warn("Error (%d) %s when trying to lock the medium "
                             "(family %s, name %s) at locate on extent %d "
                             "raid1 layout, early locking leans on other "
                             "extents", -rc2, strerror(-rc2),
                             rsc_family2str(medium_id->family),
                             medium_id->name, extent_index);
                    continue;
                }

                best_location->nb_locked_splits++;
                new_lock_extent_index[*nb_new_lock] = extent_index;
                (*nb_new_lock)++;
                break;
            } else {
                if (!strcmp(extent_hostname, best_location->hostname)) {
                    /* a new medium is now locked to best_location */
                    best_location->nb_locked_splits++;
                    free(extent_hostname);
                    break;
                }

                free(extent_hostname);
            }
        }

        /* stop if we failed to lock one split */
        if (extent_index >= (split_index + 1) * repl_count)
            break;
    }

    if (best_location->nb_locked_splits < nb_split) {
        unsigned int new_lock_index;

        for (new_lock_index = 0; new_lock_index < *nb_new_lock;
             new_lock_index++) {
            struct media_info target_medium;
            int rc2;

            target_medium.rsc.id =
                layout->extents[new_lock_extent_index[new_lock_index]].media;
            rc2 = dss_unlock(dss, DSS_MEDIA, &target_medium, 1, false);
            if (rc2 == -ENOLCK || rc2 == -EACCES) {
                pho_warn("Early lock was concurrently updated %d (%s) before "
                         "we try to unlock it when dealing with an early lock "
                         "locate split starvation, medium (family %s, name %s) "
                         "with extent %d raid1 layout",
                         -rc2, strerror(-rc2),
                         rsc_family2str(target_medium.rsc.id.family),
                         target_medium.rsc.id.name,
                         new_lock_extent_index[new_lock_index]);
            } else {
                pho_warn("Unlock error %d (%s) when dealing with an early lock "
                         "locate split starvation, medium (family %s, name %s) "
                         "with extent %d raid1 layout",
                         -rc2, strerror(-rc2),
                         rsc_family2str(target_medium.rsc.id.family),
                         target_medium.rsc.id.name,
                         new_lock_extent_index[new_lock_index]);
            }
        }

        LOG_RETURN(-EAGAIN,
                   "%d splits of the raid1 object (oid: '%s', uuid: '%s', "
                   "version: %d) are not reachable by any host.\n",
                   best_location->nb_locked_splits - nb_split,
                   layout->oid, layout->uuid, layout->version);
    }

    return 0;
}

int layout_raid1_locate(struct dss_handle *dss, struct layout_info *layout,
                        const char *focus_host, char **hostname,
                        int *nb_new_lock)
{
    struct object_location object_location;
    struct host_rsc_access_info *best_location;
    bool *split_known_by_host = NULL;
    const char *focus_host_secured;
    unsigned int split_index;
    unsigned int repl_count;
    unsigned int nb_split;
    int rc;
    int i;

    *hostname = NULL;
    if (focus_host) {
        focus_host_secured = focus_host;
    } else {
        focus_host_secured = get_hostname();
        if (!focus_host_secured)
            LOG_RETURN(-EADDRNOTAVAIL,  "Unable to get self hostname");
    }

    /* get repl_count from layout */
    rc = layout_repl_count(layout, &repl_count);
    if (rc)
        LOG_RETURN(rc, "Invalid replica count from layout to locate");

    assert((layout->ext_count % repl_count) == 0);
    nb_split = layout->ext_count / repl_count;

    /* init object_location */
    rc = init_object_location(dss, &object_location, nb_split, repl_count,
                              focus_host_secured,
                              layout->extents[0].media.family);
    if (rc)
        LOG_RETURN(rc, "Unable to allocate first object_location");

    split_known_by_host = malloc(object_location.nb_hosts *
                                 sizeof(*split_known_by_host));
    if (split_known_by_host == NULL)
        LOG_GOTO(clean, rc = -errno,
                 "Failed to allocate split_known_by_host");

    /* update object_location for each split */
    for (split_index = 0; split_index < nb_split; split_index++) {
        bool enodev = true;

        for (i = 0; i < object_location.nb_hosts; ++i)
            split_known_by_host[i] = false;

        /* each extent of this split */
        for (i = split_index * repl_count;
             i < (split_index + 1) * repl_count && i < layout->ext_count;
             i++) {
            struct pho_id *medium_id = &layout->extents[i].media;
            struct media_info *medium_info = NULL;
            char *extent_hostname = NULL;
            char *tape_model = NULL;
            bool usable = true;
            int rc2;

            /* Retrieve the host and additionnal information about the medium */
            rc2 = dss_medium_locate(dss, medium_id, &extent_hostname,
                                    &medium_info);
            if (rc2) {
                pho_warn("Error %d (%s) when trying to dss locate medium "
                         "(family %s, name %s) of with extent %d raid1 layout "
                         "locate leans on other extents", -rc2, strerror(-rc2),
                         rsc_family2str(medium_id->family), medium_id->name, i);
                /* If the locate failed, consider the medium unusable for that
                 * locate.
                 */
                usable = false;
            } else {
                enodev = false;

                if (medium_info->rsc.id.family == PHO_RSC_TAPE &&
                    medium_info->rsc.model != NULL) {
                    tape_model = strdup(medium_info->rsc.model);
                    if (tape_model == NULL) {
                        dss_res_free(medium_info, 1);
                        continue;
                    }
                }
            }

            rc2 = add_host_to_object_location(&object_location, dss,
                                              extent_hostname, tape_model,
                                              usable, split_index,
                                              split_known_by_host);
            if (medium_info != NULL)
                media_info_free(medium_info);

            if (rc2)
                continue;
        }

        if (enodev)
            LOG_GOTO(clean, rc = -ENODEV,
                     "No medium exists to locate the split %d", split_index);
    }

    /* get best location */
    get_best_object_location(&object_location, &best_location);

    /* return -EAGAIN if at least one split is unreachable even on the best */
    if (best_location->nb_unreachable_split > 0)
        LOG_GOTO(clean, rc = -EAGAIN,
                 "%d splits of the raid1 object (oid: \"%s\", uuid: \"%s\", "
                 "version: %d) are not reachable by any host.\n",
                 best_location->nb_unreachable_split,
                 layout->oid, layout->uuid, layout->version);

    /* early locks at locate for the found best location */
    rc = raid1_lock_at_locate(dss, layout, repl_count, nb_split,
                              &object_location, best_location, nb_new_lock);
    if (rc)
        LOG_GOTO(clean, rc,
                 "failed to early locks at locate object (oid: '%s', uuid: "
                 "'%s', version: %d) on best location %s",
                 layout->oid, layout->uuid, layout->version,
                 best_location->hostname);

    /* allocate the returned best_location hostname */
    *hostname = strdup(best_location->hostname);
    if (!*hostname)
        LOG_GOTO(clean, rc = -errno,
                 "Unable to duplicate best locate hostname %s",
                 best_location->hostname);

clean:
    free(split_known_by_host);
    clean_object_location(&object_location);
    return rc;
}

static const struct pho_layout_module_ops LAYOUT_RAID1_OPS = {
    .encode = layout_raid1_encode,
    .decode = layout_raid1_decode,
    .locate = layout_raid1_locate,
};

/** Layout module registration entry point */
int pho_module_register(void *module, void *context)
{
    struct layout_module *self = (struct layout_module *) module;

    phobos_module_context_set(context);

    self->desc = RAID1_MODULE_DESC;
    self->ops = &LAYOUT_RAID1_OPS;

    return 0;
}
