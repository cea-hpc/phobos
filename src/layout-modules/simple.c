/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 * Copyright 2016 CEA/DAM. All Rights Reserved.
 */
/**
 * \brief  Phobos Simple Layout plugin
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

#define PLUGIN_NAME     "simple"
#define PLUGIN_MAJOR    0
#define PLUGIN_MINOR    1


struct simple_ctx {
    struct lrs_intent    sc_main_intent;
    GHashTable          *sc_copy_intents;
    int                  sc_itemcnt;
    int                  sc_retcode;
};

static struct simple_ctx *simple_ctx_new(struct layout_module *self,
                                         struct layout_composer *comp)
{
    struct simple_ctx   *ctx;

    ctx = calloc(1, sizeof(*ctx));
    if (!ctx)
        return NULL;

    ctx->sc_copy_intents = g_hash_table_new_full(g_str_hash, g_str_equal,
                                                 NULL, g_free);
    return ctx;
}

static void simple_ctx_del(struct layout_composer *comp)
{
    struct simple_ctx   *ctx = comp->lc_private;

    g_hash_table_destroy(ctx->sc_copy_intents);
    free(ctx);
}

static void decode_intent_alloc_cb(void *key, void *val, void *udata)
{
    struct layout_info      *layout = val;
    struct layout_composer  *comp = udata;
    struct simple_ctx       *ctx = comp->lc_private;
    struct lrs_intent       *intent;
    int                      rc;

    /* This is simple layout, layout is corrupt if more that 1 extent */
    if (layout->ext_count != 1)
        ctx->sc_retcode = -EINVAL;

    intent = g_malloc0(sizeof(*intent));
    intent->li_location.extent = layout->extents[0];
    g_hash_table_insert(ctx->sc_copy_intents, layout->oid, intent);

    rc = lrs_read_prepare(comp->lc_dss, intent);
    if (rc && !ctx->sc_retcode)
        ctx->sc_retcode = rc;
}

/**
 * Simple layout: data is written in a single, consecutive, byte stream.
 * Therefore generate a single intent list.
 */
static int simple_compose_dec(struct layout_module *self,
                              struct layout_composer *comp)
{
    struct simple_ctx   *ctx;
    int                  rc;

    ctx = simple_ctx_new(self, comp);
    if (!ctx)
        return -ENOMEM;

    comp->lc_private      = ctx;
    comp->lc_private_dtor = simple_ctx_del;

    g_hash_table_foreach(comp->lc_layouts, decode_intent_alloc_cb, comp);
    return ctx->sc_retcode;
}

static void sce_sum_sizes_cb(void *key, void *val, void *udata)
{
    struct layout_info  *layout = val;
    struct simple_ctx   *ctx = udata;

    ctx->sc_main_intent.li_location.extent.size += layout->wr_size;
}

#define MEMDUP(_x)  g_memdup((_x), sizeof(*(_x)))
static void sce_layout_assign_cb(void *key, void *val, void *udata)
{
    struct layout_info  *layout = val;
    struct simple_ctx   *ctx = udata;
    struct lrs_intent   *intent = MEMDUP(&ctx->sc_main_intent);

    g_hash_table_insert(ctx->sc_copy_intents, layout->oid, intent);

    layout->ext_count = 1;
    layout->extents   = &intent->li_location.extent;
    layout->extents->size = layout->wr_size;
}

static int simple_compose_enc(struct layout_module *self,
                              struct layout_composer *comp)
{
    struct simple_ctx   *ctx;
    int                  rc;

    ctx = simple_ctx_new(self, comp);
    if (!ctx)
        return -ENOMEM;

    comp->lc_private      = ctx;
    comp->lc_private_dtor = simple_ctx_del;

    /* Unique intent of size=sum(slices) */
    g_hash_table_foreach(comp->lc_layouts, sce_sum_sizes_cb, ctx);

    rc = lrs_write_prepare(comp->lc_dss, &ctx->sc_main_intent);
    if (rc)
        return rc;

    /* Assign intent layout to the slices' layouts */
    g_hash_table_foreach(comp->lc_layouts, sce_layout_assign_cb, ctx);
    return rc;
}

static int simple_encode(struct layout_module *self,
                         struct layout_composer *comp, const char *objid,
                         struct pho_io_descr *io)
{
    struct layout_info  *layout = g_hash_table_lookup(comp->lc_layouts, objid);
    struct extent       *extent = layout->extents;
    struct simple_ctx   *ctx    = comp->lc_private;
    struct lrs_intent   *intent = g_hash_table_lookup(ctx->sc_copy_intents,
                                                      objid);
    struct io_adapter    ioa;
    int                  rc;

    assert(layout->ext_count == 1);

    rc = get_io_adapter(extent->fs_type, &ioa);
    if (rc)
        return rc;

    io->iod_size = extent->size;
    io->iod_loc  = &intent->li_location;

    if (io->iod_flags & PHO_IO_DELETE) {
        rc = ioa_del(&ioa, objid, NULL, io->iod_loc);
    } else {
        rc = ioa_put(&ioa, objid, NULL, io, NULL, NULL);
        if (rc == 0)
            ctx->sc_itemcnt++;
    }

    return rc;
}

static int simple_decode(struct layout_module *self,
                         struct layout_composer *comp, const char *objid,
                         struct pho_io_descr *io)
{
    struct layout_info  *layout = g_hash_table_lookup(comp->lc_layouts, objid);
    struct simple_ctx   *ctx    = comp->lc_private;
    struct lrs_intent   *intent = g_hash_table_lookup(ctx->sc_copy_intents,
                                                      objid);
    struct extent       *extent = &intent->li_location.extent;
    struct io_adapter    ioa;
    int                  rc;

    rc = get_io_adapter(extent->fs_type, &ioa);
    if (rc)
        return rc;

    /* Complete the IOD with missing information */
    io->iod_size = extent->size;
    io->iod_loc  = &intent->li_location;

    rc = ioa_get(&ioa, objid, NULL, io, NULL, NULL);
    if (rc)
        return rc;

    ctx->sc_itemcnt++;
    return 0;
}

static int simple_commit_enc(struct layout_module *self,
                             struct layout_composer *comp, int err_code)
{
    struct simple_ctx   *ctx = comp->lc_private;
    struct lrs_intent   *intent = &ctx->sc_main_intent;

    return lrs_done(intent, ctx->sc_itemcnt, err_code);
}

static void commit_intent_cb(void *key, void *val, void *udata)
{
    struct lrs_intent   *intent = val;
    struct simple_ctx   *ctx    = udata;
    int                  rc;

    rc = lrs_done(intent, 1, ctx->sc_retcode);
    if (rc && !ctx->sc_retcode)
        ctx->sc_retcode = rc;
}

static int simple_commit_dec(struct layout_module *self,
                             struct layout_composer *comp, int err_code)
{
    struct simple_ctx   *ctx = comp->lc_private;

    ctx->sc_retcode = err_code;
    g_hash_table_foreach(ctx->sc_copy_intents, commit_intent_cb, ctx);
    return ctx->sc_retcode;
}


static const struct layout_operations SimpleOps[] = {
    [LA_ENCODE] = {
        .lmo_compose   = simple_compose_enc,
        .lmo_io_submit = simple_encode,
        .lmo_io_commit = simple_commit_enc,
    },
    [LA_DECODE] = {
        .lmo_compose   = simple_compose_dec,
        .lmo_io_submit = simple_decode,
        .lmo_io_commit = simple_commit_dec,
    },
};


int pho_layout_mod_register(struct layout_module *self, enum layout_action act)
{

    self->lm_desc.mod_name  = PLUGIN_NAME;
    self->lm_desc.mod_major = PLUGIN_MAJOR;
    self->lm_desc.mod_minor = PLUGIN_MINOR;

    if (act < 0 || act >= ARRAY_SIZE(SimpleOps))
        return -ENOSYS;

    self->lm_ops = &SimpleOps[act];
    return 0;
}
