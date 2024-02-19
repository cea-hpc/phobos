#include <assert.h>
#include <stdbool.h>

#include "pho_cache.h"
#include "pho_common.h"
#include "pho_ref.h"

static struct key_value *value2kv(void *value)
{
    return container_of(value, struct key_value, value);
}

static struct key_value *ref2kv(struct pho_ref *ref)
{
    return (struct key_value *)ref->value;
}

static void *ref2value(struct pho_ref *ref)
{
    if (!ref)
        return NULL;

    return ref2kv(ref)->value;
}

struct key_value *key_value_alloc(void *key, void *data, size_t size)
{
    struct key_value *kv;

    kv = xmalloc(sizeof(*kv) + size);
    kv->key = key;
    if (data)
        memcpy(kv->value, data, size);
    else
        memset(kv->value, 0, size);

    return kv;
}

struct pho_cache *pho_cache_init(const char *name,
                                 struct pho_cache_operations *ops,
                                 void *env)
{
    struct pho_cache *cache;

    cache = xmalloc(sizeof(*cache));
    cache->name = name;
    cache->ops = ops;
    cache->env = env;
    cache->cache = g_hash_table_new(ops->pco_hash, ops->pco_equal);
    cache->old_values = g_hash_table_new(g_direct_hash, g_direct_equal);
    pthread_rwlock_init(&cache->lock, NULL);

    return cache;
}

void pho_cache_destroy(struct pho_cache *cache)
{
    pthread_rwlock_destroy(&cache->lock);
    g_hash_table_destroy(cache->cache);
    g_hash_table_destroy(cache->old_values);
    free(cache);
}

static void pho_cache_rdlock(struct pho_cache *cache)
{
    pthread_rwlock_rdlock(&cache->lock);
}

static void pho_cache_wrlock(struct pho_cache *cache)
{
    pthread_rwlock_wrlock(&cache->lock);
}

static void pho_cache_unlock(struct pho_cache *cache)
{
    pthread_rwlock_unlock(&cache->lock);
}

static void old_cached_ref_remove(struct pho_cache *cache, struct pho_ref *ref)
{
    struct key_value *kv = ref2kv(ref);

    assert(ref->count == 0);
    assert(g_hash_table_remove(cache->old_values, kv->value));
    cache->ops->pco_destroy(kv, cache->env);
    pho_ref_destroy(ref);
}

static void cached_ref_remove(struct pho_cache *cache, struct pho_ref *ref)
{
    struct key_value *kv = ref2kv(ref);

    assert(ref->count == 0);
    assert(g_hash_table_remove(cache->cache, kv->key));
    cache->ops->pco_destroy(kv, cache->env);
    pho_ref_destroy(ref);
}

static struct pho_ref *pho_cache_acquire_nolocked(struct pho_cache *cache,
                                                  const void *key)
{
    struct pho_ref *ref;

    ref = g_hash_table_lookup(cache->cache, key);
    if (ref) {
        pho_ref_acquire(ref);

        return ref;
    }

    return NULL;
}

void *pho_cache_acquire(struct pho_cache *cache, const void *key)
{
    struct key_value *kv;
    struct pho_ref *ref;

    pho_cache_rdlock(cache);
    ref = pho_cache_acquire_nolocked(cache, key);
    if (ref)
        goto unlock;

    pho_cache_unlock(cache);

    pho_cache_wrlock(cache);
    ref = pho_cache_acquire_nolocked(cache, key);
    if (ref)
        goto unlock;

    kv = cache->ops->pco_build(key, cache->env);
    if (!kv)
        goto unlock;

    ref = pho_ref_init(kv);
    pho_ref_acquire(ref);
    g_hash_table_insert(cache->cache, kv->key, ref);

unlock:
    pho_cache_unlock(cache);

    return ref2value(ref);
}

static void pho_cache_insert_old(struct pho_cache *cache,
                                 struct pho_ref *ref)
{
    struct key_value *kv = ref2kv(ref);

    if (ref->count > 0) {
        g_hash_table_insert(cache->old_values, kv->value, ref);
        /* remove the old value from the table since we don't want to keep the
         * old key as it will be freed with the value when completely removed
         * from the cache.
         */
        assert(g_hash_table_remove(cache->cache, kv->key));
    } else {
        cached_ref_remove(cache, ref);
    }
}

static void *pho_cache_insert_nolock(struct pho_cache *cache,
                                     struct key_value *kv)
{
    struct pho_ref *ref;

    ref = g_hash_table_lookup(cache->cache, kv->key);
    if (!ref) {
        ref = pho_ref_init(kv);
        pho_ref_acquire(ref);
        g_hash_table_insert(cache->cache, kv->key, ref);
        return kv->value;
    }

    /* the value was already in the cache, move it to old values */
    pho_cache_insert_old(cache, ref);
    ref = pho_ref_init(kv);
    pho_ref_acquire(ref);
    assert(g_hash_table_insert(cache->cache, kv->key, ref));

    return kv->value;
}

void *pho_cache_insert(struct pho_cache *cache, void *key, void *value)
{
    struct key_value *kv;
    void *res;

    pho_cache_wrlock(cache);
    kv = cache->ops->pco_value2kv(key, value);
    if (!kv)
        GOTO(unlock, res = NULL);

    res = pho_cache_insert_nolock(cache, kv);
unlock:
    pho_cache_unlock(cache);

    return res;
}

void *pho_cache_update(struct pho_cache *cache, void *key)
{
    struct key_value *updated;

    pho_cache_wrlock(cache);
    updated = cache->ops->pco_build(key, cache->env);
    if (!updated) {
        pho_cache_unlock(cache);
        return NULL;
    }

    pho_cache_insert_nolock(cache, updated);
    pho_cache_unlock(cache);

    return updated->value;
}

void pho_cache_release(struct pho_cache *cache, void *value)
{
    struct key_value *kv = value2kv(value);
    struct pho_ref *ref;

    pho_cache_wrlock(cache);
    ref = g_hash_table_lookup(cache->cache, kv->key);
    if (!ref || ref2value(ref) != value) {
        struct pho_ref *old_ref = g_hash_table_lookup(cache->old_values, value);

        assert(old_ref && old_ref->count > 0);

        pho_ref_release(old_ref);
        pho_debug("releasing %p, ref count = %d", kv->value, old_ref->count);
        if (old_ref->count == 0)
            old_cached_ref_remove(cache, old_ref);

        goto unlock;
    }

    pho_ref_release(ref);
    pho_debug("releasing %p, ref count = %d", kv->value, ref->count);
    if (ref->count == 0)
        cached_ref_remove(cache, ref);

unlock:
    pho_cache_unlock(cache);
}

static void display_cache_element(gpointer key, gpointer _ref, gpointer _cache)
{
    struct pho_cache *cache = _cache;
    struct pho_ref *ref = _ref;

    if (cache->ops->pco_display)
        cache->ops->pco_display(key, ref2value(ref), ref->count);
    else
        pho_debug("key: %p, value: %p, rc: %d", key, ref2value(ref),
                  ref->count);
}

/* this table is indexed by the value of the cache, so \p key is the value of
 * the other hash table and value is of type struct key_value.
 */
static void display_old_element(gpointer value, gpointer _ref, gpointer _cache)
{
    struct pho_cache *cache = _cache;
    struct pho_ref *ref = _ref;
    struct key_value *kv = ref2kv(ref);

    if (cache->ops->pco_display)
        cache->ops->pco_display(kv->key, value, ref->count);
    else
        pho_debug("key: %p, value: %p, rc: %d", kv->key, value, ref->count);
}

void pho_cache_dump(struct pho_cache *cache)
{
    if (pho_log_level_get() != PHO_LOG_DEBUG)
        return;

    pho_cache_rdlock(cache);
    g_hash_table_foreach(cache->cache, display_cache_element, cache);
    pho_debug("Old refs:");
    g_hash_table_foreach(cache->old_values, display_old_element, cache);
    pho_cache_unlock(cache);
}
