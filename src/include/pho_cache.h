#pragma once

#include <glib.h>
#include <pthread.h>

struct key_value {
    void *key;
    char value[];
};

struct key_value *key_value_alloc(void *key, void *data, size_t size);

struct pho_cache_operations {
    GHashFunc pco_hash;
    GEqualFunc pco_equal;
    struct key_value *(*pco_build)(const void *key, void *env);
    struct key_value *(*pco_value2kv)(void *key, void *value);
    void (*pco_destroy)(struct key_value *kv, void *env);
    void (*pco_display)(void *key, void *value, int ref_count);
};

/**
 * Current value cache (pho_cache::cache):
 * - key:   void *
 * - value: struct pho_ref
 *
 * Old value cache (pho_cache::old_values):
 * - key:   struct key_value::value
 * - value: struct pho_ref
 *
 * The key of pho_cache::old_values is the address of the pointer
 * key_value::value. This is taken from the current value cache's value when a
 * struct pho_ref goes from the current cache to the old value cache.
 *
 * The actual value in the cache associated to a key is of an arbitrary type
 * embedded in a struct key_value. This struct key_value is reference counted
 * and therefore wrapped in a struct pho_ref. This struct pho_ref is then stored
 * in the cache pho_cache::cache.
 *
 * When moving a value from the current cache to the old cache, the key used in
 * the old cache is the address of the value in the current cache. Values are
 * uniquely identified by their address. When inserting or updating a value in
 * the cache, the current value is put into the old value cache. If this value
 * still has references, we need to keep it until all the references are
 * dropped. Which is why the old value cache is necessary. Otherwise, values
 * with no reference are simply dropped.
 */
struct pho_cache {
    /** name of the cache for display purposes */
    const char *name;
    /** Read/write lock to protect concurrent access to the cache. */
    pthread_rwlock_t lock;
    /** Most up to date cached values. */
    GHashTable *cache;
    /** Old values kept until their ref count is 0. */
    GHashTable *old_values;
    /** Arbitrary parameter passed to build and destroy operations. */
    void *env;
    /** Vector of operations to manage keys and values. */
    struct pho_cache_operations *ops;
};

struct pho_cache *pho_cache_init(const char *name,
                                 struct pho_cache_operations *ops,
                                 void *env);

void pho_cache_destroy(struct pho_cache *cache);

void pho_cache_dump(struct pho_cache *cache);

/**
 * Insert a value inside the cache. This function is meant to be called when a
 * value is initialized outside the cache and the user wants to insert it to
 * keep a reference for later use.
 *
 * The whole point of the cache is that initializing the values is expensive, so
 * we should avoid to build already existing ones again.
 *
 * This function is a bit weird and there may not be any good implementation
 * for it. Maybe this issue is that pho_cache_acquire also initializes the
 * values. It may be best to do this outside the cache but then the cache is
 * only responsible for destroying the values...
 */
void *pho_cache_insert(struct pho_cache *cache, void *key, void *value);

void *pho_cache_update(struct pho_cache *cache, void *key);

void *pho_cache_acquire(struct pho_cache *cache, const void *key);

void pho_cache_release(struct pho_cache *cache, void *value);
