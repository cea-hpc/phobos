/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 * Copyright 2017 CEA/DAM. All Rights Reserved.
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

struct raid1_ctx {
    int                  itemcnt;         /**< Number of processed items */
    int                  replicas;        /**< Number of replicas */
    int                  retcode;         /**< For passing values to cb */
    size_t               intent_size;     /**< Size of each copy  */
    GHashTable          *intent_copies;   /**< Map <oid:intents>  */
    struct lrs_intent    intents[0];      /**< Reference intents  */
};

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

    ctx = calloc(1, sizeof(*ctx) + copy_count * sizeof(struct lrs_intent));
    if (!ctx)
        return NULL;

    ctx->replicas = copy_count;
    ctx->intent_copies = g_hash_table_new(g_str_hash, g_str_equal);
    return ctx;
}

static void free_extent_copies(void *key, void *val, void *udata)
{
    struct layout_info *layout = val;

    layout->ext_count = 0;
    free(layout->extents);
    layout->extents = NULL;
}

static void raid1_ctx_del(struct layout_composer *comp)
{
    struct raid1_ctx   *ctx = comp->lc_private;

    g_hash_table_foreach(comp->lc_layouts, free_extent_copies, NULL);
    g_hash_table_destroy(ctx->intent_copies);
    free(ctx);
}

static int decode_intent_alloc_cb(const void *key, void *val, void *udata)
{
    struct layout_info      *layout = val;
    struct layout_composer  *comp = udata;
    struct raid1_ctx        *ctx = comp->lc_private;
    struct lrs_intent       *intent = ctx->intents;
    int                      retry = 0;
    int                      rc;

    /**
     * XXX in the absence of a LRS API that allows us to pass multiple options
     * to pick the best one, all we can do here is to iterate and retry if LRS
     * call fails.  Similarly, this module does not allow retries if a failure
     * happens at a later step (not yet).
     */
    do {
        intent->li_location.extent = layout->extents[retry];

        rc = lrs_read_prepare(comp->lc_dss, intent);
        if (rc == 0) {
            g_hash_table_insert(ctx->intent_copies, layout->oid, intent);
            break;
        }
    } while (++retry < ctx->replicas);

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

    comp->lc_private      = ctx;
    comp->lc_private_dtor = raid1_ctx_del;

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

    intents = calloc(ctx->replicas, sizeof(*intents));
    if (!intents)
        return -ENOMEM;

    layout->extents = calloc(ctx->replicas, sizeof(*layout->extents));
    if (!layout->extents)
        GOTO(out_free, rc = -ENOMEM);

    layout->ext_count = ctx->replicas;

    for (i = 0; i < ctx->replicas; i++) {
        intents[i] = ctx->intents[i];
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

    comp->lc_private      = ctx;
    comp->lc_private_dtor = raid1_ctx_del;

    /* Multiple intents of size=sum(slices) each */
    g_hash_table_foreach(comp->lc_layouts, sum_sizes_cb, ctx);

    for (i = 0; i < ctx->replicas; i++) {
        struct lrs_intent   *curr = &ctx->intents[i];

        /* Declare replica size as computed above */
        curr->li_location.extent.size = ctx->intent_size;

        rc = lrs_write_prepare(comp->lc_dss, curr);
        if (rc)
            LOG_GOTO(err_int_free, rc, "Write intent expression #%d failed", i);

        expressed_intents++;
    }

    /* Assign the reserved extents to the registered layouts */
    rc = pho_ht_foreach(comp->lc_layouts, layout_assign_extent_cb, ctx);

err_int_free:
    if (rc) {
        for (i = 0; i < expressed_intents; i++)
            lrs_done(&ctx->intents[i], 0, rc);
    }

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

    for (i = 0; i < ctx->replicas; i++) {
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
                ctx->itemcnt++;
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

    /* In future versions, intent will be an array of ctx->replicas entries,
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
        ctx->itemcnt++;

    return rc;
}

static int raid1_commit_enc(struct layout_module *self,
                            struct layout_composer *comp, int err_code)
{
    struct raid1_ctx    *ctx = comp->lc_private;
    int                  i;
    int                  rc = 0;

    for (i = 0; i < ctx->replicas; i++) {
        int tmp;

        tmp = lrs_done(&ctx->intents[i], ctx->itemcnt, err_code);
        if (tmp && !rc)
            rc = tmp;
    }

    return rc;
}

static void commit_intent_cb(void *key, void *val, void *udata)
{
    struct lrs_intent   *intent = val;
    struct raid1_ctx    *ctx    = udata;
    int                  rc;

    rc = lrs_done(intent, 1, ctx->retcode);
    if (rc && !ctx->retcode)
        ctx->retcode = rc;
}

static int raid1_commit_dec(struct layout_module *self,
                            struct layout_composer *comp, int err_code)
{
    struct raid1_ctx    *ctx = comp->lc_private;

    ctx->retcode = err_code;
    g_hash_table_foreach(ctx->intent_copies, commit_intent_cb, ctx);
    return ctx->retcode;
}


static const struct layout_operations ReplicationOps[] = {
    [LA_ENCODE] = {
        .lmo_compose   = raid1_compose_enc,
        .lmo_io_submit = raid1_encode,
        .lmo_io_commit = raid1_commit_enc,
    },
    [LA_DECODE] = {
        .lmo_compose   = raid1_compose_dec,
        .lmo_io_submit = raid1_decode,
        .lmo_io_commit = raid1_commit_dec,
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
