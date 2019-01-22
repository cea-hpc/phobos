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
 * \brief  Phobos Replication Layout plugin
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <glib.h>
#include "pho_lrs.h"
#include "pho_common.h"
#include "pho_dss.h"
#include "pho_type_utils.h"
#include "pho_cfg.h"
#include "pho_layout.h"
#include "pho_io.h"

#define PLUGIN_NAME     "raid1"
#define PLUGIN_MAJOR    0
#define PLUGIN_MINOR    1

/**
 * List of configuration parameters for this module
 */
enum pho_cfg_params_store {
    /* Actual parameters */
    PHO_CFG_LYT_RAID1_repl_count,

    /* Delimiters, update when modifying options */
    PHO_CFG_LYT_RAID1_FIRST = PHO_CFG_LYT_RAID1_repl_count,
    PHO_CFG_LYT_RAID1_LAST  = PHO_CFG_LYT_RAID1_repl_count,
};

const struct pho_config_item cfg_lyt_raid1[] = {
    [PHO_CFG_LYT_RAID1_repl_count] = {
        .section = "layout_raid1",
        .name    = "repl_count",
        .value   = "2"  /* Total # of copies (default) */
    },
};


/* Structure maintained for each replica, ie. one per media in this module. */
struct replica_state {
    int               items;  /**< Number of items successfuly written on it */
    int               error;  /**< First error encountered or 0 on success   */
    struct lrs_intent intent; /**< LRS intent associated to this replica     */
};

/* Global layout module context, instanciated per transfer (get, put, mput) */
struct raid1_ctx {
    int                      replica_cnt;   /**< Number of replica_cnt */
    size_t                   intent_size;   /**< Size of each copy  */
    GHashTable              *intent_copies; /**< Map <oid:intents>  */
    struct replica_state     replicas[0];   /**< Reference intents  */
};

static void raid1_ctx_del(struct layout_composer *comp);

static void mktag(char *tag, size_t tag_len, int extent_index)
{
    int rc;

    rc = snprintf(tag, tag_len, "r%d", extent_index);
    assert(rc < tag_len);
}

static struct raid1_ctx *raid1_ctx_new(struct layout_module *self,
                                       struct layout_composer *comp)
{
    struct raid1_ctx   *ctx;
    int                 copy_count = PHO_CFG_GET_INT(cfg_lyt_raid1,
                                                     PHO_CFG_LYT_RAID1,
                                                     repl_count, 0);

    if (copy_count <= 0) {
        pho_error(-EINVAL, "Invalid # of replica (%d)", copy_count);
        return NULL;
    }

    if (comp->lc_action == LA_DECODE &&
        g_hash_table_size(comp->lc_layouts) > 1) {
        pho_error(-ENOTSUP, "MGET not supported by this module");
        return NULL;
    }

    ctx = calloc(1, sizeof(*ctx) + copy_count * sizeof(struct replica_state));
    if (!ctx)
        return NULL;

    ctx->replica_cnt = copy_count;
    ctx->intent_copies = g_hash_table_new(g_str_hash, g_str_equal);

    comp->lc_private      = ctx;
    comp->lc_private_dtor = raid1_ctx_del;
    return ctx;
}

static void extent_free_cb(void *key, void *val, void *udata)
{
    struct layout_info *layout = val;

    layout->ext_count = 0;
    free(layout->extents);
    layout->extents = NULL;
}

static void raid1_ctx_del(struct layout_composer *comp)
{
    struct raid1_ctx   *ctx = comp->lc_private;
    int                 itr;
    int                 i;

    if (comp->lc_action == LA_ENCODE)
        itr = ctx->replica_cnt;
    else
        itr = 1;

    for (i = 0; i < itr; i++)
        lrs_resource_release(&ctx->replicas[i].intent);

    g_hash_table_foreach(comp->lc_layouts, extent_free_cb, NULL);
    g_hash_table_destroy(ctx->intent_copies);
    free(ctx);

    comp->lc_private      = NULL;
    comp->lc_private_dtor = NULL;
}

static int decode_intent_alloc_cb(const void *key, void *val, void *udata)
{
    struct layout_info      *layout = val;
    struct layout_composer  *comp = udata;
    struct raid1_ctx        *ctx = comp->lc_private;
    struct lrs_intent       *intent = &ctx->replicas[0].intent;
    int                      retry = 0;
    int                      rc;

    /**
     * XXX in the absence of a LRS API that allows us to pass multiple options
     * to pick the best one, all we can do here is to iterate and retry if LRS
     * call fails.  Similarly, this module does not allow retries if a failure
     * happens at a later step (not yet).
     *
     * The suggested way to GET an object whose replica #0 is on a medium which
     * can be mounted but not read is to lock this medium and retry.
     */
    do {
        intent->li_location.extent = layout->extents[retry];

        rc = lrs_read_prepare(comp->lc_dss, intent);
        if (rc == 0) {
            g_hash_table_insert(ctx->intent_copies, layout->oid, intent);
            break;
        }
    } while (++retry < ctx->replica_cnt);

    return rc;
}

/**
 * Replication layout: data is written in N separate, identical byte streams.
 * We only have to read one. Generate intent list accordingly.
 */
static int raid1_compose_dec(struct layout_module *self,
                             struct layout_composer *comp)
{
    struct raid1_ctx    *ctx;

    ctx = raid1_ctx_new(self, comp);
    if (!ctx)
        return -ENOMEM;

    return pho_ht_foreach(comp->lc_layouts, decode_intent_alloc_cb, comp);
}

static void sum_sizes_cb(void *key, void *val, void *udata)
{
    struct layout_info  *layout = val;
    struct raid1_ctx    *ctx = udata;

    ctx->intent_size += layout->wr_size;
}

static int layout_assign_extent_cb(const void *key, void *val, void *udata)
{
    struct layout_info  *layout = val;
    struct raid1_ctx    *ctx = udata;
    struct lrs_intent   *intents;
    int                  i;
    int                  rc = 0;

    intents = calloc(ctx->replica_cnt, sizeof(*intents));
    if (!intents)
        return -ENOMEM;

    layout->extents = calloc(ctx->replica_cnt, sizeof(*layout->extents));
    if (!layout->extents)
        GOTO(out_free, rc = -ENOMEM);

    layout->ext_count = ctx->replica_cnt;

    for (i = 0; i < ctx->replica_cnt; i++) {
        intents[i] = ctx->replicas[i].intent;
        intents[i].li_location.extent.size = layout->wr_size;
        layout->extents[i] = intents[i].li_location.extent;
    }

    g_hash_table_insert(ctx->intent_copies, layout->oid, intents);
    intents = NULL;

out_free:
    free(intents);
    return rc;
}

static int raid1_compose_enc(struct layout_module *self,
                             struct layout_composer *comp)
{
    struct raid1_ctx    *ctx;
    int                  i;
    int                  expressed_intents = 0;
    int                  rc = 0;

    ctx = raid1_ctx_new(self, comp);
    if (!ctx)
        return -ENOMEM;

    /* Multiple intents of size=sum(slices) each */
    g_hash_table_foreach(comp->lc_layouts, sum_sizes_cb, ctx);

    for (i = 0; i < ctx->replica_cnt; i++) {
        struct lrs_intent   *curr = &ctx->replicas[i].intent;

        /* Declare replica size as computed above */
        curr->li_location.extent.size = ctx->intent_size;

        rc = lrs_write_prepare(comp->lc_dss, curr, &comp->lc_tags);
        if (rc)
            LOG_GOTO(err_int_free, rc, "Write intent expression #%d failed", i);

        expressed_intents++;
    }

    /* Assign the reserved extents to the registered layouts */
    return pho_ht_foreach(comp->lc_layouts, layout_assign_extent_cb, ctx);

err_int_free:
    raid1_ctx_del(comp);
    for (i = 0; i < expressed_intents; i++)
        lrs_resource_release(&ctx->replicas[i].intent);

    return rc;
}

static int raid1_encode(struct layout_module *self,
                        struct layout_composer *comp, const char *objid,
                        struct pho_io_descr *io)
{
    struct raid1_ctx    *ctx    = comp->lc_private;
    struct layout_info  *layout = g_hash_table_lookup(comp->lc_layouts, objid);
    struct lrs_intent   *intent = g_hash_table_lookup(ctx->intent_copies,
                                                      objid);
    int                  i;
    int                  rc = 0;

    for (i = 0; i < ctx->replica_cnt; i++) {
        struct lrs_intent   *curr = &intent[i];
        struct extent       *extent = &curr->li_location.extent;
        struct io_adapter    ioa;
        char                 tag[PHO_LAYOUT_TAG_MAX + 1];

        rc = get_io_adapter(extent->fs_type, &ioa);
        if (rc)
            return rc;

        io->iod_size = extent->size;
        io->iod_off  = 0;
        io->iod_loc  = &curr->li_location;

        /* build extent tag, specific to this layout */
        mktag(tag, sizeof(tag), i);

        if (io->iod_flags & PHO_IO_DELETE) {
            rc = ioa_del(&ioa, objid, tag, io->iod_loc);
        } else {
            rc = ioa_put(&ioa, objid, tag, io, NULL, NULL);
            if (rc == 0)
                ctx->replicas[i].items++;
        }

        layout->extents[i] = *extent;
    }

    return rc;
}

static int raid1_decode(struct layout_module *self,
                        struct layout_composer *comp, const char *objid,
                        struct pho_io_descr *io)
{
    struct raid1_ctx   *ctx    = comp->lc_private;
    struct lrs_intent  *intent = g_hash_table_lookup(ctx->intent_copies, objid);
    struct extent      *extent = &intent->li_location.extent;
    struct io_adapter   ioa;
    char                tag[PHO_LAYOUT_TAG_MAX + 1];
    int                 rc;

    /* In future versions, intent will be an array of ctx->replica_cnt entries,
     * but since we are operating on a single mount here, it is OK to use it
     * directly (intent = &intent[0]).
     */
    rc = get_io_adapter(extent->fs_type, &ioa);
    if (rc)
        return rc;

    /* Complete the IOD with missing information */
    io->iod_size = extent->size;
    io->iod_loc  = &intent->li_location;

    /* build extent tag, specific to this layout */
    mktag(tag, sizeof(tag), 0);

    rc = ioa_get(&ioa, objid, tag, io, NULL, NULL);
    if (rc == 0)
        ctx->replicas[0].items++;

    return rc;
}

static int raid1_commit(struct layout_module *self,
                        struct layout_composer *comp, int err_code)
{
    struct raid1_ctx    *ctx = comp->lc_private;
    int                  i;
    int                  rc = 0;

    if (comp->lc_action == LA_DECODE)
        return 0;

    for (i = 0; i < ctx->replica_cnt; i++) {
        struct replica_state    *repl = &ctx->replicas[i];
        int                      rc2;

        rc2 = lrs_io_complete(&repl->intent, repl->items, repl->error);
        if (rc2 && !rc)
            rc = rc2;
    }

    return rc;
}


static const struct layout_operations ReplicationOps[] = {
    [LA_ENCODE] = {
        .lmo_compose   = raid1_compose_enc,
        .lmo_io_submit = raid1_encode,
        .lmo_io_commit = raid1_commit,
    },
    [LA_DECODE] = {
        .lmo_compose   = raid1_compose_dec,
        .lmo_io_submit = raid1_decode,
        .lmo_io_commit = raid1_commit,
    },
};


int pho_layout_mod_register(struct layout_module *self, enum layout_action act)
{

    self->lm_desc.mod_name  = PLUGIN_NAME;
    self->lm_desc.mod_major = PLUGIN_MAJOR;
    self->lm_desc.mod_minor = PLUGIN_MINOR;

    if (act == LA_ENCODE)
        pho_attr_set(&self->lm_desc.mod_attrs, "repl_count",
                     PHO_CFG_GET(cfg_lyt_raid1, PHO_CFG_LYT_RAID1, repl_count));

    if (act < 0 || act >= ARRAY_SIZE(ReplicationOps))
        return -ENOSYS;

    self->lm_ops = &ReplicationOps[act];
    return 0;
}
