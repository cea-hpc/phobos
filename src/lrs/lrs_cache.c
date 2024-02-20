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
 * \brief LRS Media Cache implementation
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>

#include "lrs_cache.h"
#include "pho_common.h"
#include "pho_dss.h"
#include "pho_type_utils.h"

struct media_cache_env {
    struct dss_handle dss;
} lrs_media_cache_env[PHO_RSC_LAST];

static guint lrs_media_cache_hash(gconstpointer key);
static gboolean lrs_media_cache_equal(gconstpointer lhs, gconstpointer rhs);
static struct key_value *lrs_media_cache_build(const void *key, void *_env);
static struct key_value *lrs_media_cache_value2kv(void *key, void *value);
static void lrs_media_cache_destroy(struct key_value *kv, void *env);
static void lrs_media_cache_display(void *key, void *value, int ref_count);

struct pho_cache_operations lrs_media_cache_ops = {
    .pco_hash     = lrs_media_cache_hash,
    .pco_equal    = lrs_media_cache_equal,
    .pco_build    = lrs_media_cache_build,
    .pco_value2kv = lrs_media_cache_value2kv,
    .pco_destroy  = lrs_media_cache_destroy,
    .pco_display  = lrs_media_cache_display,
};

int lrs_cache_setup(enum rsc_family family)
{
    struct pho_cache *cache;
    int rc;

    /* Note: it is not thread safe to use a dss_handle concurrently. But since
     * the access in the cache is always with the write lock, we could use the
     * dss_handle of another thread to avoid creating a new connection. The
     * issue is that we don't have the guarantee that a given thread will not
     * use its handle while another one uses it through the cache. So we create
     * a new connection here instead.
     */
    rc = dss_init(&lrs_media_cache_env[family].dss);
    if (rc) {
        phobos_context()->lrs_media_cache[family] = NULL;
        return rc;
    }

    cache = pho_cache_init("lrs_media_cache", &lrs_media_cache_ops,
                           &lrs_media_cache_env[family]);
    phobos_context()->lrs_media_cache[family] = cache;
    return 0;
}

void lrs_cache_cleanup(enum rsc_family family)
{
    if (phobos_context()->lrs_media_cache[family]) {
        pho_cache_destroy(phobos_context()->lrs_media_cache[family]);
        dss_fini(&lrs_media_cache_env[family].dss);
    }
}

struct media_info *lrs_medium_acquire(const struct pho_id *id)
{
    pho_debug("cache acquire: %s (%p)", id->name, id);
    return pho_cache_acquire(phobos_context()->lrs_media_cache[id->family], id);
}

void lrs_medium_release(struct media_info *medium)
{
    enum rsc_family family;

    if (!medium)
        return;

    pho_debug("cache release: %s (%p)", medium->rsc.id.name, medium);
    family = medium->rsc.id.family;
    pho_cache_release(phobos_context()->lrs_media_cache[family], medium);
}

struct media_info *lrs_medium_update(struct pho_id *id)
{
    enum rsc_family family = id->family;

    return pho_cache_update(phobos_context()->lrs_media_cache[family], id);
}

struct media_info *lrs_medium_insert(struct media_info *medium)
{
    enum rsc_family family = medium->rsc.id.family;

    return pho_cache_insert(phobos_context()->lrs_media_cache[family],
                            &medium->rsc.id, medium);
}

void lrs_media_cache_dump(enum rsc_family family)
{
    pho_cache_dump(phobos_context()->lrs_media_cache[family]);
}

static guint lrs_media_cache_hash(gconstpointer key)
{
    const struct pho_id *id = key;

    return g_str_hash(id->name);
}

static gboolean lrs_media_cache_equal(gconstpointer _lhs, gconstpointer _rhs)
{
    const struct pho_id *lhs = _lhs;
    const struct pho_id *rhs = _rhs;

    return g_str_equal(lhs->name, rhs->name);
}

static struct key_value *lrs_media_cache_build(const void *key, void *_env)
{
    struct media_cache_env *env = _env;
    const struct pho_id *id = key;
    struct media_info *medium;
    struct dss_filter filter;
    struct key_value *kv;
    int count;
    int rc;

    rc = dss_filter_build(&filter,
                          "{\"$AND\": ["
                              "{\"DSS::MDA::family\": \"%s\"}, "
                              "{\"DSS::MDA::id\": \"%s\"}"
                          "]}",
                          rsc_family2str(id->family),
                          id->name);
    if (rc) {
        errno = -rc;
        return NULL;
    }

    rc = dss_media_get(&env->dss, &filter, &medium, &count);
    dss_filter_free(&filter);
    if (rc) {
        errno = -rc;
        return NULL;
    }
    assert(count <= 1);
    if (count == 0) {
        dss_res_free(medium, count);
        errno = ENXIO;
        return NULL;
    }

    kv = key_value_alloc(NULL, NULL, sizeof(*medium));
    media_info_copy((struct media_info *)kv->value, medium);
    dss_res_free(medium, count);
    kv->key = &((struct media_info *)kv->value)->rsc.id;

    return kv;
}

static struct key_value *lrs_media_cache_value2kv(void *key, void *value)
{
    struct media_info *medium = value;
    struct key_value *kv;

    kv = key_value_alloc(NULL, NULL, sizeof(*medium));
    media_info_copy((struct media_info *)kv->value, medium);

    kv->key = &((struct media_info *)kv->value)->rsc.id;

    return kv;
}

static void lrs_media_cache_destroy(struct key_value *kv, void *env)
{
    (void) env;

    media_info_cleanup((struct media_info *)kv->value);
    free(kv);
}

static void lrs_media_cache_display(void *key, void *value, int ref_count)
{
    struct pho_id *id = key;

    pho_debug("%s: %p (ref count: %d)", id->name, value, ref_count);
}
