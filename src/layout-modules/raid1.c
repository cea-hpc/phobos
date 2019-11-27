/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2019 CEA/DAM.
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
#include "pho_io.h"
#include "pho_layout.h"
#include "pho_srl_common.h"
#include "pho_type_utils.h"

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
 *
 * @FIXME: raid1 layout with a repl_count of 1 behaves exactly as the simple
 * layout. We could remove the simple layout from the code and replace it by
 * this raid1 code with a repl_count of 1.
 */
struct raid1_encoder {
    unsigned int repl_count;
    size_t       to_write;          /**< Amount of data to read/write */
    unsigned int cur_extent_idx;    /**< Current extent index */
    bool         pending_alloc;     /**< Whether a pending unanswer medium
                                      *  allocation request has been emitted or
                                      *  not
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
 * Replica count parameter comes from configuration.
 * It is saved in layout REPL_COUNT_ATTR_KEY attr in a char * value and in the
 * private raid1 encoder unsigned int repl_count value.
 */
#define REPL_COUNT_ATTR_KEY "repl_count"
#define REPL_COUNT_ATTR_VALUE_BASE 10

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
    media_id =  media_id_get(&extent->media);
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
 * Set unsigned int decoder/encoder value from char * layout attr
 *
 * 0 is not a valid replica count, -EINVAL will be returned.
 *
 * @param[in]  layout     layout with a REPL_COUNT_ATTR_KEY
 * @param[out] repl_count unsigned int to set
 *
 * @return 0 if success, -error_code if failure
 */
static int layout_repl_count(struct layout_info *layout,
                             unsigned int *repl_count)
{
        const char *string_repl_count =
            pho_attr_get(&layout->layout_desc.mod_attrs,
                         REPL_COUNT_ATTR_KEY);

        if (string_repl_count == NULL)
            LOG_RETURN(-EINVAL,
                       "Unable to get replica count from layout attrs");

        errno = 0;
        *repl_count = strtoul(string_repl_count, NULL,
                              REPL_COUNT_ATTR_VALUE_BASE);
        if (errno != 0)
            return -errno;

        if (*repl_count == 0)
            LOG_RETURN(-EINVAL, "invalid 0 replica count");

        return 0;
}

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

/**
 * Fill an extent structure, except the adress field, which is usually set by
 * a future call to ioa_open or ioa_put.
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
    extent->media.type = medium->med_id->type;
    strncpy(extent->media.id, medium->med_id->id, sizeof(extent->media.id));
    extent->addr_type = medium->addr_type;
}

/**
 * Write data from current offset to medium, filling \a extent according to the
 * data written.
 */
static int simple_write_chunk(struct pho_encoder *enc,
                              const pho_resp_write_elt_t *medium,
                              struct extent *extent)
{
    struct raid1_encoder *raid1 = enc->priv_enc;
    struct pho_ext_loc loc = {0};
    struct pho_io_descr iod = {0};
    struct io_adapter ioa;
    char extent_tag[128];
    int rc;

    ENTRY;

    rc = get_io_adapter(medium->fs_type, &ioa);
    if (rc)
        return rc;

    /* Start with field initializations that may fail */
    iod.iod_fd = enc->xfer->xd_fd;
    if (iod.iod_fd < 0)
        LOG_RETURN(-EBADF, "Invalid encoder xfer file descriptor in simple "
                           "write raid1 encoder");

    /*
     * @TODO: replace extent size with the actual size written (which is not
     * returned by ioa_put as of yet)
     */
    /* extent->address will be later filled by ioa_put */
    set_extent_info(extent, medium, raid1->cur_extent_idx++,
                    min(raid1->to_write, medium->avail_size));

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
              media_id_get(&extent->media));
    /* Build extent tag */
    rc = snprintf(extent_tag, sizeof(extent_tag), "s%d", extent->layout_idx);

    /* If the tag was more than 128 bytes long, it is a programming error */
    assert(rc < sizeof(extent_tag));

    rc = ioa_put(&ioa, enc->xfer->xd_objid, extent_tag, &iod);
    pho_attrs_free(&iod.iod_attrs);
    if (rc == 0)
        raid1->to_write -= extent->size;

    return rc;
}

/**
 * Handle the write allocation response by writing data on the allocated medium
 * and filling the associated release request with relevant information (rc and
 * size_written).
 */
static int simple_enc_write_chunk(struct pho_encoder *enc,
                                  pho_resp_write_t *wresp,
                                  pho_req_release_t *rreq)
{
    struct raid1_encoder *raid1 = enc->priv_enc;
    struct extent cur_extent = {0};
    int rc;

    if (wresp->n_media != 1)
        LOG_RETURN(-EPROTO,
                   "Received %zu medium allocation but only 1 was requested",
                   wresp->n_media);

    /* Perform IO */
    rc = simple_write_chunk(enc, wresp->media[0], &cur_extent);
    rreq->media[0]->rc = rc;
    rreq->media[0]->size_written = cur_extent.size;
    if (rc)
        return rc;

    add_written_extent(raid1, &cur_extent);

    return 0;
}

/**
 * Write count bytes from input_fd in each iod.
 *
 * Bytes are read from input_fd and stored to an intermediate buffer before
 * being written into each iod.
 *
 * @return 0 if success, else a negative error code
 */
static int write_all_chunks(int input_fd, const struct io_adapter *ioa,
                            struct pho_io_descr *iod,
                            unsigned int replica_count, size_t count)
{
    /** TODO
     * Better than fixing it to PAGE_SIZE, we could get buffer size from
     * an ioa_preferred_read_size/write_size function.
     */
    size_t buffer_size = sysconf(_SC_PAGESIZE);
    char *buffer;
#define MAX_NULL_READ_TRY 10
    int nb_null_read_try = 0;
    size_t to_write = count;
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
            rc = ioa_write(&ioa[i], &iod[i], buffer, buf_size);
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
    struct pho_io_descr *iod = NULL;
    struct pho_ext_loc *loc = NULL;
    struct io_adapter *ioa = NULL;
    struct extent *extent = NULL;
    char *extent_tag = NULL;
    size_t extent_size;
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
        rc = get_io_adapter(wresp->media[i]->fs_type, &ioa[i]);
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

    for (i = 0; i < raid1->repl_count; ++i) {
        /* set loc */
        loc[i].root_path = wresp->media[i]->root_path;
        loc[i].extent = &extent[i];
        /* set iod */
        iod[i].iod_flags = PHO_IO_REPLACE | PHO_IO_NO_REUSE;
        /* iod[i].iod_fd is replaced by a buffer in open/write/close api */
        /* iod[i].iod_size starts from 0 and will be updated by each write */
        iod[i].iod_size = 0;
        iod[i].iod_loc = &loc[i];
        rc = build_extent_xattr(enc->xfer->xd_objid, &enc->xfer->xd_attrs,
                                &(iod[i].iod_attrs));
        if (rc)
            LOG_GOTO(attrs, rc, "Unable to set iod_attrs for extent %d", i);

        /* iod_ctx will be set by open */
    }

    /* open all iod */
    for (i = 0; i < raid1->repl_count; ++i) {
        rc = ioa_open(&ioa[i], enc->xfer->xd_objid,
                      &extent_tag[i * EXTENT_TAG_SIZE], &iod[i], true);
        if (rc)
            LOG_GOTO(close, rc, "Unable to open extent %s in raid1 write",
                     &extent_tag[i * EXTENT_TAG_SIZE]);
   }

    /* write all extents by chunk of buffer size*/
    rc = write_all_chunks(enc->xfer->xd_fd, ioa, iod, raid1->repl_count,
                          extent_size);
    if (rc)
        LOG_GOTO(close, rc, "Unable to write in raid1 encoder write");

close:
    for (i = 0; i < raid1->repl_count; ++i) {
        int rc2;

        rc2 = ioa_close(&ioa[i], &iod[i]);
        if (!rc && rc2)
            rc = rc2;
    }

    /* update size in write encoder */
    if (rc == 0)
        raid1->to_write -= extent_size;

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
    struct extent *extent = NULL;
    struct pho_ext_loc loc = {0};
    struct pho_io_descr iod = {0};
    struct io_adapter ioa;
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
        if (strcmp(medium->med_id->id, candidate_extent->media.id) == 0) {

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
    rc = get_io_adapter(medium->fs_type, &ioa);
    if (rc)
        return rc;

    extent->addr_type = medium->addr_type;
    loc.root_path = medium->root_path;
    loc.extent = extent;

    iod.iod_fd = dec->xfer->xd_fd;
    if (iod.iod_fd < 0)
        LOG_RETURN(rc = -EBADF, "Invalid decoder xfer file descriptor");

    iod.iod_size = loc.extent->size;
    iod.iod_loc = &loc;

    pho_debug("Reading %ld bytes from medium %s", extent->size,
              media_id_get(&extent->media));

    rc = ioa_get(&ioa, dec->xfer->xd_objid, NULL, &iod);
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

        pho_debug("Marking medium %s as released", rel_resp->med_ids[i]->id);
        /* If the media_id is unexpected, -EINVAL will be returned */
        rc2 = mark_written_medium_released(raid1, rel_resp->med_ids[i]->id);
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
        n_tags[i] = enc->xfer->xd_tags.n_tags;

    rc = pho_srl_request_write_alloc(req, raid1->repl_count, n_tags);
    free(n_tags);
    if (rc)
        return rc;

    for (i = 0; i < raid1->repl_count; ++i) {
        req->walloc->media[i]->size = raid1->to_write;

        for (j = 0; j < enc->xfer->xd_tags.n_tags; ++j)
            req->walloc->media[i]->tags[j] = strdup(enc->xfer->xd_tags.tags[j]);
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
                  media_id_get(&dec->layout->extents[ext_idx].media),
                  i, raid1->cur_extent_idx);
        req->ralloc->med_ids[i]->type =
            dec->layout->extents[ext_idx].media.type;
        req->ralloc->med_ids[i]->id =
            strdup(dec->layout->extents[ext_idx].media.id);
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
        /* Last emitted allocation has now been fulfilled */
        raid1->pending_alloc = false;
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

        for (i = 0; i < resp->walloc->n_media; ++i)
            med_info_cpy((*reqs)[*n_reqs].release->media[i]->med_id,
                         resp->walloc->media[i]->med_id);

        /* Perform IO and populate release request with the outcome */
        if (raid1->repl_count == 1) {
            rc = simple_enc_write_chunk(enc, resp->walloc,
                                             (*reqs)[*n_reqs].release);
        } else { /* repl_count > 1 */
            rc = multiple_enc_write_chunk(enc, resp->walloc,
                                             (*reqs)[*n_reqs].release);
        }

        (*n_reqs)++;
    } else if (pho_response_is_read(resp)) {
        /* Last emitted allocation has now been fulfilled */
        raid1->pending_alloc = false;
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
        med_info_cpy((*reqs)[*n_reqs].release->media[0]->med_id,
                     resp->ralloc->media[0]->med_id);

        /* Perform IO and populate release request with the outcome */
        rc = simple_dec_read_chunk(enc, resp->ralloc->media[0]);
        (*reqs)[*n_reqs].release->media[0]->rc = rc;
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
        raid1->pending_alloc || /* a pending alloc already exists */
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
    raid1->pending_alloc = true;
    /* TODO : alloc is noticed pending here but it is not already sent */

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
    const char *string_repl_count;
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
    raid1->pending_alloc = false;

    /* The layout description has to be set on encoding */
    enc->layout->layout_desc = RAID1_MODULE_DESC;

    /* get repl_count from conf */
    string_repl_count = PHO_CFG_GET(cfg_lyt_raid1, PHO_CFG_LYT_RAID1,
                                    repl_count);
    if (string_repl_count == NULL)
        LOG_RETURN(-EINVAL, "Unable to get replica count from conf to "
                            "built a raid1 encoder");

    /* set repl_count as char * in layout */
    rc = pho_attr_set(&enc->layout->layout_desc.mod_attrs,
                      REPL_COUNT_ATTR_KEY, string_repl_count);
    if (rc)
        LOG_RETURN(rc, "Unable to set raid1 layout repl_count attr in "
                       "encoder built");

    /* set repl_count as unsigned int in encoder */
    rc = layout_repl_count(enc->layout, &raid1->repl_count);
    if (rc)
        LOG_RETURN(rc, "Invalid replica count from layout to build raid1 "
                       "encoder");

    /* set write size */
    if (raid1->repl_count <= 0)
        LOG_RETURN(-EINVAL, "Invalid # of replica (%d)", raid1->repl_count);

    if (enc->xfer->xd_size < 0)
        LOG_RETURN(-EINVAL, "bad input encoder size to write when building "
                            "raid1 encoder");

    raid1->to_write = enc->xfer->xd_size;

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
    struct raid1_encoder *raid1 = calloc(1, sizeof(*raid1));
    int rc;
    int i;

    if (raid1 == NULL)
        return -ENOMEM;

    if (!enc->is_decoder)
        LOG_RETURN(-EINVAL, "ask to create a decoder on an encoder");

    /*
     * The ops field is set early to allow the caller to call the destroy
     * function on error.
     */
    enc->ops = &RAID1_ENCODER_OPS;
    enc->priv_enc = raid1;

    /* Initialize raid1-specific state */
    raid1->cur_extent_idx = 0;
    raid1->pending_alloc = false;
    raid1->written_extents = NULL;
    raid1->to_release_media = NULL;
    raid1->n_released_media = 0;

    /* Fill out the encoder appropriately */
    /* get repl_count from provided layout to set decoder repl_count*/
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

static const struct pho_layout_module_ops LAYOUT_RAID1_OPS = {
    .encode = layout_raid1_encode,
    .decode = layout_raid1_decode,
};

/** Layout module registration entry point */
int pho_layout_mod_register(struct layout_module *self)
{
    self->desc = RAID1_MODULE_DESC;
    self->ops = &LAYOUT_RAID1_OPS;

    return 0;
}
