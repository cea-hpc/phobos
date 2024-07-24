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
 * \brief  Phobos Raid1 Layout plugin
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
#ifdef HAVE_XXH128
#include <xxhash.h>
#endif

#include "pho_attrs.h"
#include "pho_cfg.h"
#include "pho_common.h"
#include "pho_dss.h"
#include "pho_dss_wrapper.h"
#include "pho_io.h"
#include "pho_layout.h"
#include "pho_module_loader.h"
#include "pho_srl_common.h"
#include "pho_type_utils.h"
#include "raid1.h"
#include "raid_common.h"

#define PLUGIN_NAME     "raid1"
#define PLUGIN_MAJOR    0
#define PLUGIN_MINOR    2

static struct module_desc RAID1_MODULE_DESC = {
    .mod_name  = PLUGIN_NAME,
    .mod_major = PLUGIN_MAJOR,
    .mod_minor = PLUGIN_MINOR,
};

/**
 * List of configuration parameters for this module
 */
enum pho_cfg_params_raid1 {
    /* Actual parameters */
    PHO_CFG_LYT_RAID1_repl_count,
    PHO_CFG_LYT_RAID1_extent_xxh128,
    PHO_CFG_LYT_RAID1_extent_md5,

    /* Delimiters, update when modifying options */
    PHO_CFG_LYT_RAID1_FIRST = PHO_CFG_LYT_RAID1_repl_count,
    PHO_CFG_LYT_RAID1_LAST  = PHO_CFG_LYT_RAID1_extent_md5,
};

const struct pho_config_item cfg_lyt_raid1[] = {
    [PHO_CFG_LYT_RAID1_repl_count] = {
        .section = "layout_raid1",
        .name    = REPL_COUNT_ATTR_KEY,
        .value   = "2"  /* Total # of copies (default) */
    },
    [PHO_CFG_LYT_RAID1_extent_xxh128] = {
        .section = "layout_raid1",
        .name    = EXTENT_XXH128_ATTR_KEY,
        .value   = DEFAULT_XXH128,
    },
    [PHO_CFG_LYT_RAID1_extent_md5] = {
        .section = "layout_raid1",
        .name    = EXTENT_MD5_ATTR_KEY,
        .value   = DEFAULT_MD5
    },
};

int raid1_repl_count(struct layout_info *layout, unsigned int *repl_count)
{
    const char *string_repl_count = pho_attr_get(&layout->layout_desc.mod_attrs,
                                                 PHO_EA_RAID1_REPL_COUNT_NAME);

    if (string_repl_count == NULL)
        /* Ensure we can read objects from the old schema, which have the
         * replica count under the name 'repl_count' and not 'raid1.repl_count'
         */
        string_repl_count = pho_attr_get(&layout->layout_desc.mod_attrs,
                                         "repl_count");

    if (string_repl_count == NULL)
        LOG_RETURN(-ENOENT, "Unable to get replica count from layout attrs");

    errno = 0;
    *repl_count = strtoul(string_repl_count, NULL, 10);
    if (errno != 0 || *repl_count == 0)
        LOG_RETURN(-EINVAL, "Invalid replica count '%s'", string_repl_count);

    return 0;
}

static int write_all_chunks(struct raid_io_context *io_context,
                            size_t split_size)
{
    struct pho_io_descr *posix = &io_context->posix;
    size_t to_write = split_size;
    struct pho_io_descr *iods;
    size_t buffer_size;
    size_t repl_count;
    char *buffer;
    int rc = 0;

    buffer = io_context->buffers[0].buff;
    buffer_size = io_context->buffers[0].size;
    repl_count = io_context->n_data_extents + io_context->n_parity_extents;
    iods = io_context->iods;

    while (to_write > 0) {
        ssize_t read_size;
        int i;

        read_size = ioa_read(posix->iod_ioa, posix, buffer,
                             to_write > buffer_size ? buffer_size : to_write);
        if (read_size < 0)
            LOG_RETURN(rc,
                       "Error when read buffer in raid1 write, "
                       "%zu remaning bytes",
                       to_write);

        /* TODO manage as async/parallel IO */
        for (i = 0; i < repl_count; ++i) {
            rc = ioa_write(iods[i].iod_ioa, &iods[i], buffer, read_size);
            if (rc)
                LOG_RETURN(rc,
                           "RAID1 write: unable to write %zu bytes in replica "
                           "%d, %zu remaining bytes",
                           read_size, i, to_write);

            /* update written iod size */
            iods[i].iod_size += read_size;
        }

        rc = extent_hash_update(&io_context->hashes[0], buffer, read_size);
        if (rc)
            return rc;

        to_write -= read_size;
    }

    return rc;
}

static int set_layout_specific_md(int layout_index, int replica_count,
                                  struct pho_io_descr *iod)
{
    char str_buffer[16];
    int rc = 0;

    rc = sprintf(str_buffer, "%d", layout_index);
    if (rc < 0)
        LOG_GOTO(attrs_free, rc = -errno,
                 "Unable to construct extent index buffer");

    pho_attr_set(&iod->iod_attrs, PHO_EA_RAID1_EXTENT_INDEX_NAME, str_buffer);

    rc = sprintf(str_buffer, "%d", replica_count);
    if (rc < 0)
        LOG_GOTO(attrs_free, rc = -errno,
                 "Unable to construct replica count buffer");

    pho_attr_set(&iod->iod_attrs, PHO_EA_RAID1_REPL_COUNT_NAME, str_buffer);

    return 0;

attrs_free:
    pho_attrs_free(&iod->iod_attrs);

    return rc;
}

static int raid1_write_split(struct pho_encoder *enc, size_t split_size)
{
    struct raid_io_context *io_context = enc->priv_enc;
    size_t repl_count = io_context->n_data_extents +
        io_context->n_parity_extents;
    struct pho_io_descr *iods;
    int rc = 0;
    int i;

    iods = io_context->iods;

    for (i = 0; i < repl_count; ++i) {
        get_preferred_io_block_size(&enc->io_block_size,
                                    iods[i].iod_ioa,
                                    &iods[i]);
    }

    /* write all extents by chunk of buffer size*/
    rc = write_all_chunks(io_context, split_size);
    if (rc)
        LOG_RETURN(rc, "Unable to write in raid1 encoder write");

    rc = extent_hash_digest(&io_context->hashes[0]);
    if (rc)
        return rc;

    if (rc == 0) {
        for (i = 0; i < repl_count; i++) {
            rc = extent_hash_copy(&io_context->hashes[0],
                                  &io_context->write.extents[i]);
            if (rc)
                return rc;
        }
    }

    for (i = 0; i < repl_count; ++i) {
        struct extent *extent = &io_context->write.extents[i];

        rc = set_layout_specific_md(extent->layout_idx, repl_count, &iods[i]);
        if (rc)
            LOG_RETURN(rc,
                       "Failed to set layout specific attributes on extent "
                       "'%s'", extent[i].uuid);
    }

    return rc;
}

/**
 * Read the data specified by \a extent from \a medium into the output fd of
 * dec->xfer.
 */
static int raid1_read_split(struct pho_encoder *dec)
{
    struct raid_io_context *io_context = dec->priv_enc;
    struct pho_io_descr *iod;
    struct pho_ext_loc loc;

    iod = &io_context->iods[0];
    loc = make_ext_location(dec, 0);

    iod->iod_fd = dec->xfer->xd_fd;
    iod->iod_size = loc.extent->size;
    iod->iod_loc = &loc;

    return ioa_get(iod->iod_ioa, dec->xfer->xd_objid, iod);
}

static int raid1_get_block_size(struct pho_encoder *enc,
                                size_t *block_size)
{
    struct raid_io_context *io_context = enc->priv_enc;
    struct pho_io_descr *iod;

    iod = &io_context->iods[0];

    get_preferred_io_block_size(block_size, iod->iod_ioa, iod);

    return 0;
}

static const struct pho_enc_ops RAID1_ENCODER_OPS = {
    .step       = raid_encoder_step,
    .destroy    = raid_encoder_destroy,
};

static const struct raid_ops RAID1_OPS = {
    .write_split    = raid1_write_split,
    .read_split     = raid1_read_split,
    .get_block_size = raid1_get_block_size,
};

static int raid1_get_repl_count(struct pho_encoder *enc,
                                unsigned int *repl_count)
{
    const char *string_repl_count;
    int rc;

    *repl_count = 0;

    if (pho_attrs_is_empty(&enc->xfer->xd_params.put.lyt_params))
        string_repl_count = PHO_CFG_GET(cfg_lyt_raid1, PHO_CFG_LYT_RAID1,
                                        repl_count);
    else
        string_repl_count = pho_attr_get(&enc->xfer->xd_params.put.lyt_params,
                                         REPL_COUNT_ATTR_KEY);

    if (string_repl_count == NULL)
        LOG_RETURN(-EINVAL, "Unable to get replica count from conf to "
                            "build a raid1 encoder");

    /* set repl_count as char * in layout */
    pho_attr_set(&enc->layout->layout_desc.mod_attrs,
                 PHO_EA_RAID1_REPL_COUNT_NAME, string_repl_count);

    /* set repl_count in encoder */
    rc = raid1_repl_count(enc->layout, repl_count);
    if (rc)
        LOG_RETURN(rc, "Invalid replica count from layout to build raid1 "
                       "encoder");

    /* set write size */
    if (*repl_count <= 0)
        LOG_RETURN(-EINVAL, "Invalid # of replica (%d)", *repl_count);

    return 0;
}

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
    struct raid_io_context *io_context;
    unsigned int repl_count;
    size_t i;
    int rc;

    rc = raid1_get_repl_count(enc, &repl_count);
    if (rc)
        return rc;

    io_context = xcalloc(1, sizeof(*io_context));
    enc->priv_enc = io_context;
    io_context->name = PLUGIN_NAME;
    io_context->n_data_extents = 1;
    io_context->n_parity_extents = repl_count - 1;
    io_context->write.to_write = enc->xfer->xd_params.put.size;
    io_context->nb_hashes = repl_count;
    io_context->hashes = xcalloc(io_context->nb_hashes,
                                 sizeof(*io_context->hashes));

    for (i = 0; i < io_context->nb_hashes; i++) {
        rc = extent_hash_init(&io_context->hashes[i],
                              PHO_CFG_GET_BOOL(cfg_lyt_raid1,
                                               PHO_CFG_LYT_RAID1,
                                               extent_md5,
                                               false),
                              PHO_CFG_GET_BOOL(cfg_lyt_raid1,
                                               PHO_CFG_LYT_RAID1,
                                               extent_xxh128,
                                               false));
        if (rc)
            goto out_hash;
    }

    return raid_encoder_init(enc, &RAID1_MODULE_DESC, &RAID1_ENCODER_OPS,
                             &RAID1_OPS);

out_hash:
    for (i -= 1; i >= 0; i--)
        extent_hash_fini(&io_context->hashes[i]);

    /* The rest will be free'd by layout destroy */
    return rc;
}

/** Implements layout_decode layout module methods. */
static int layout_raid1_decode(struct pho_encoder *dec)
{
    struct raid_io_context *io_context;
    unsigned int repl_count;
    int rc;
    int i;

    ENTRY;

    rc = raid1_repl_count(dec->layout, &repl_count);
    if (rc)
        LOG_RETURN(rc, "Invalid replica count from layout to build raid1 "
                       "decoder");

    if (dec->layout->ext_count % repl_count != 0)
        LOG_RETURN(-EINVAL, "layout extents count (%d) is not a multiple "
                   "of replica count (%u)",
                   dec->layout->ext_count, repl_count);

    io_context = xcalloc(1, sizeof(*io_context));
    dec->priv_enc = io_context;
    io_context->name = PLUGIN_NAME;
    io_context->n_data_extents = 1;
    io_context->n_parity_extents = repl_count - 1;
    rc = raid_decoder_init(dec, &RAID1_MODULE_DESC, &RAID1_ENCODER_OPS,
                           &RAID1_OPS);
    if (rc) {
        dec->priv_enc = NULL;
        free(io_context);
        return rc;
    }

    io_context->read.to_read = 0;
    for (i = 0; i < dec->layout->ext_count / repl_count; i++) {
        struct extent *extent = &dec->layout->extents[i * repl_count];

        io_context->read.to_read += extent->size;
    }

    /* Empty GET does not need any IO */
    if (io_context->read.to_read == 0)
        dec->done = true;

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
     * device of the same type as the object, with their hostname and their
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

static void init_split_access_info(struct split_access_info *split,
                                   unsigned int repl_count)
{
    split->repl_count = repl_count;
    split->nb_hosts = 0;
    split->usable = xcalloc(repl_count, sizeof(*split->usable));
    split->hostnames = xcalloc(repl_count, sizeof(*split->hostnames));
    split->tape_model = xcalloc(repl_count, sizeof(*split->tape_model));
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
        xcalloc(split_count, sizeof(*object_location->split_accesses));

    for (i = 0; i < object_location->split_count; i++)
        init_split_access_info(&object_location->split_accesses[i],
                               object_location->repl_count);

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
    object_location->hosts = xcalloc(object_location->nb_hosts,
                                     sizeof(*object_location->hosts));

    /* Consider the first known host to be focus_host */
    object_location->hosts[0].hostname = xstrdup(focus_host);
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
            host_to_add->hostname = xstrdup(drive_host);
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
         * available).
         */
        if (host_to_update->dev_count == 0)
            continue;

        /* Allocate the correct amount of devices for that host */
        host_to_update->dev_models =
            xmalloc(host_to_update->dev_count *
                    sizeof(*host_to_update->dev_models));

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
        if (devs[i].rsc.model == NULL)
            host_to_update->dev_models[host_to_update->dev_count] = NULL;
        else
            host_to_update->dev_models[host_to_update->dev_count] =
                xstrdup(devs[i].rsc.model);

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
    has_access = xmalloc(object_location->nb_hosts * sizeof(*has_access));

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

    *best_location = NULL;

    /* The very first host known is guaranteed to be focus-host, so if it can
     * read the object and has at least the same number of fitted split as every
     * host, it will be chosen.
     */
    for (i = 0; i < object_location->nb_hosts; i++) {
        struct host_rsc_access_info *candidate = object_location->hosts+i;

        /* If the candidate has any unreachable split, it cannot read the object
         * so we skip it
         */
        if (candidate->nb_unreachable_split > 0)
            continue;

        /* Otherwise, that means that host can read the object, so we pick the
         * one with the most fitted split
         */
        if (*best_location == NULL ||
            (*best_location)->nb_locked_splits < candidate->nb_locked_splits)
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
                         "locate medium at early lock (family %s, name %s, "
                         "library %s) of with extent %d raid1 layout early "
                         "locking leans on other extents", -rc2, strerror(-rc2),
                         rsc_family2str(medium_id->family), medium_id->name,
                         medium_id->library, extent_index);
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
                             "(family %s, name %s, library %s) at locate on "
                             "extent %d raid1 layout, early locking leans on "
                             "other extents", -rc2, strerror(-rc2),
                             rsc_family2str(medium_id->family),
                             medium_id->name, medium_id->library, extent_index);
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
                         "locate split starvation, medium (family '%s', name "
                         "'%s', library '%s') with extent %d raid1 layout",
                         -rc2, strerror(-rc2),
                         rsc_family2str(target_medium.rsc.id.family),
                         target_medium.rsc.id.name,
                         target_medium.rsc.id.library,
                         new_lock_extent_index[new_lock_index]);
            } else {
                pho_warn("Unlock error %d (%s) when dealing with an early lock "
                         "locate split starvation, medium (family '%s', name "
                         "'%s', library '%s') with extent %d raid1 layout",
                         -rc2, strerror(-rc2),
                         rsc_family2str(target_medium.rsc.id.family),
                         target_medium.rsc.id.name,
                         target_medium.rsc.id.library,
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
    rc = raid1_repl_count(layout, &repl_count);
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

    split_known_by_host = xmalloc(object_location.nb_hosts *
                                  sizeof(*split_known_by_host));

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
                         "(family %s, name %s, library %s) of with extent %d "
                         "raid1 layout locate leans on other extents",
                         -rc2, strerror(-rc2),
                         rsc_family2str(medium_id->family), medium_id->name,
                         medium_id->library, i);
                /* If the locate failed, consider the medium unusable for that
                 * locate.
                 */
                usable = false;
            } else {
                enodev = false;

                if (medium_info->rsc.id.family == PHO_RSC_TAPE &&
                    medium_info->rsc.model != NULL)
                    tape_model = xstrdup(medium_info->rsc.model);
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

    /* return -EAGAIN if we did not find any host that can read the object */
    if (best_location == NULL)
        LOG_GOTO(clean, rc = -EAGAIN,
                 "At least 1 split of the raid1 object (oid: \"%s\", "
                 "uuid: \"%s\", version: %d) is not reachable by any host.\n",
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
    *hostname = xstrdup(best_location->hostname);

clean:
    free(split_known_by_host);
    clean_object_location(&object_location);
    return rc;
}

static int layout_raid1_reconstruct(struct layout_info lyt,
                                    struct object_info *obj)
{
    ssize_t extent_sizes = 0;
    ssize_t replica_size = 0;
    struct extent *extents;
    unsigned int repl_cnt;
    const char *buffer;
    int obj_size;
    int ext_cnt;
    int rc;
    int i;

    // Recover repl_count and obj_size
    rc = raid1_repl_count(&lyt, &repl_cnt);
    if (rc)
        LOG_RETURN(rc,
                   "Failed to get replica count for reconstruction of object '%s'",
                   obj->oid);

    ext_cnt = lyt.ext_count;
    extents = lyt.extents;

    buffer = pho_attr_get(&lyt.layout_desc.mod_attrs, PHO_EA_OBJECT_SIZE_NAME);
    if (buffer == NULL)
        LOG_RETURN(-EINVAL,
                   "Failed to get object size for reconstruction of object '%s'",
                   obj->oid);

    obj_size = str2int64(buffer);
    if (obj_size < 0)
        LOG_RETURN(-EINVAL,
                   "Invalid object size for reconstruction of object '%s': '%d'",
                   obj->oid, obj_size);

    for (i = 0; i < ext_cnt; i++) {
        if (replica_size == extents[i].offset)
            replica_size += extents[i].size;

        extent_sizes += extents[i].size;
    }

    if (extent_sizes == repl_cnt * obj_size)
        obj->obj_status = PHO_OBJ_STATUS_COMPLETE;
    else if (replica_size == obj_size)
        obj->obj_status = PHO_OBJ_STATUS_READABLE;
    else
        obj->obj_status = PHO_OBJ_STATUS_INCOMPLETE;

    return 0;
}

static int layout_raid1_get_specific_attrs(struct pho_io_descr *iod,
                                           struct io_adapter_module *ioa,
                                           struct extent *extent,
                                           struct pho_attrs *layout_md)
{
    const char *tmp_extent_index;
    const char *tmp_repl_count;
    struct pho_attrs md;
    int rc;

    md.attr_set = NULL;
    pho_attr_set(&md, PHO_EA_RAID1_EXTENT_INDEX_NAME, NULL);
    pho_attr_set(&md, PHO_EA_RAID1_REPL_COUNT_NAME, NULL);

    iod->iod_attrs = md;
    iod->iod_flags = PHO_IO_MD_ONLY;

    rc = ioa_open(ioa, NULL, iod, false);
    if (rc)
        goto end;

    tmp_repl_count = pho_attr_get(&md, PHO_EA_RAID1_REPL_COUNT_NAME);
    if (tmp_repl_count == NULL)
        LOG_GOTO(end, rc = -EINVAL,
                 "Failed to retrieve replica count of file '%s'",
                 iod->iod_loc->extent->address.buff);

    tmp_extent_index = pho_attr_get(&md, PHO_EA_RAID1_EXTENT_INDEX_NAME);
    if (tmp_extent_index == NULL)
        LOG_GOTO(end, rc = -EINVAL,
                 "Failed to retrieve extent index of file '%s'",
                 iod->iod_loc->extent->address.buff);

    extent->layout_idx = str2int64(tmp_extent_index);
    if (extent->layout_idx < 0)
        LOG_GOTO(end, rc = -EINVAL,
                 "Invalid extent index found on '%s': '%d'",
                 iod->iod_loc->extent->address.buff, extent->layout_idx);

    pho_attr_set(layout_md, PHO_EA_RAID1_REPL_COUNT_NAME, tmp_repl_count);

end:
    pho_attrs_free(&md);

    return rc;
}

static const struct pho_layout_module_ops LAYOUT_RAID1_OPS = {
    .encode = layout_raid1_encode,
    .decode = layout_raid1_decode,
    .locate = layout_raid1_locate,
    .get_specific_attrs = layout_raid1_get_specific_attrs,
    .reconstruct = layout_raid1_reconstruct,
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
