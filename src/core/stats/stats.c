/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2025 CEA/DAM.
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
 * \brief  Management of Phobos metrics
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "pho_stats.h"
#include "pho_common.h"

/** type to handle tags */
struct key_value {
    const char *key;
    const char *value;
};

/** list of tags */
struct tag_list {
    struct key_value *pairs;
    size_t count;
};

struct pho_stat {
    enum pho_stat_type type;
    char *namespace;
    char *name;
    struct tag_list tag_list;

    _Atomic uint_least64_t value;
};

struct pho_stat_iter {
    GList      *current;
    const char *ns_filter;
    const char *name_filter;
    struct tag_list tag_list;
};

/** Protects the list */
pthread_rwlock_t pho_stat_list_lock = PTHREAD_RWLOCK_INITIALIZER;

/** list of struct pho_stat */
GList *pho_stat_list;

/** Parse a tag list string and turn it to a tag_list
 * \param[in]  tags  The string of tags to be parsed.
 * \param[out] tag_list  The list of parsed tags.
 */
static int tokenize_tags(const char *tags, struct tag_list *tag_list)
{
    char *tags_copy;
    char *saveptr; /* To store the state for strtok_r */
    const char *c;
    size_t count;
    size_t index;
    char *token;

    /* Initialize the tag list */
    tag_list->pairs = NULL;
    tag_list->count = 0;

    if (!tags || strlen(tags) == 0)
        return 0; /* return empty list */

    /* Count the number of tags */
    count = 1;
    for (c = tags; *c; c++)
        if (*c == ',')
            count++;

    tag_list->pairs = xcalloc(count, sizeof(struct key_value));
    tags_copy = strdup(tags);
    if (!tags_copy) {
        free(tag_list->pairs);
        tag_list->pairs = NULL;
        return -ENOMEM;
    }

    index = 0;
    for (token = strtok_r(tags_copy, ",", &saveptr);
         token != NULL && index < count;
         token = strtok_r(NULL, ",", &saveptr)) {
        char *equal_sign = strchr(token, '=');

        if (!equal_sign) {
            /* invalid format */
            free(tags_copy);
            free(tag_list->pairs);
            tag_list->pairs = NULL;
            return -EINVAL;
        }

        *equal_sign = '\0'; /* Split the string into key and value */

        tag_list->pairs[index].key = token;
        tag_list->pairs[index].value = equal_sign + 1;
        index++;
    }

    tag_list->count = index;
    return 0;
}

static void free_taglist(struct tag_list *tag_list)
{
    if (!tag_list->pairs)
        return;

    /* 1st key was the first token of 'tags_copy' (see above) */
    free((char *)tag_list->pairs[0].key);
    free(tag_list->pairs);
    tag_list->pairs = NULL;
}

static void pho_stat_register(struct pho_stat *stat)
{
    pthread_rwlock_wrlock(&pho_stat_list_lock);
    pho_stat_list = g_list_append(pho_stat_list, stat);
    pthread_rwlock_unlock(&pho_stat_list_lock);
}

/** Allocate and initialize a new metric */
struct pho_stat *pho_stat_create(enum pho_stat_type type,
                                 const char *namespace,
                                 const char *name,
                                 const char *tags)
{
    struct pho_stat *stat = xmalloc(sizeof(struct pho_stat));

    assert(namespace != NULL && name != NULL);

    if (tokenize_tags(tags, &stat->tag_list) != 0) {
        pho_debug("Invalid format for tags: '%s'", tags);
        return NULL;
    }
    stat->type = type;
    stat->namespace = xstrdup_safe(namespace);
    stat->name = xstrdup_safe(name);

    atomic_init(&stat->value, 0);

    pho_stat_register(stat);

    return stat;
}

/** Increments an integer type metric. */
void pho_stat_incr(struct pho_stat *stat, uint64_t val)
{
    atomic_fetch_add(&stat->value, val);
}

/** Sets the value of an integer type metric. */
void pho_stat_set(struct pho_stat *stat, uint64_t val)
{
    /* can't set a counter */
    assert(stat->type != PHO_STAT_COUNTER);

    atomic_store(&stat->value, val);
}

/**
 *  Get the value from a stat
 */
uint64_t pho_stat_get(struct pho_stat *stat)
{
    return atomic_load(&stat->value);
}

/**
 * Create a stat iterator with the given filters
 */
struct pho_stat_iter *pho_stat_iter_init(const char *namespace,
                                         const char *name,
                                         const char *tag_set)
{
    struct pho_stat_iter *iter = xmalloc(sizeof(struct pho_stat_iter));

    /* Acquire read lock. Will be released when closing the iterator */
    pthread_rwlock_rdlock(&pho_stat_list_lock);
    iter->current = pho_stat_list;
    iter->ns_filter = namespace;
    iter->name_filter = name;

    if (tokenize_tags(tag_set, &iter->tag_list) != 0) {
        pho_debug("Invalid format for tags: '%s'", tag_set);
        pthread_rwlock_unlock(&pho_stat_list_lock);
        return NULL;
    }

    return iter;
}

/** Check if a taglist matches the given filters,
 *  i.e. include the given key/values
 */
static bool taglist_contains(const struct tag_list *tags,
                             const struct tag_list *filters)
{
    assert(tags != NULL && filters != NULL);

    /* no filter, it obvously matches */
    if (filters->count == 0)
        return true;

    for (size_t i = 0; i < filters->count; i++) {
        bool found = false;
        const char *filter_key = filters->pairs[i].key;
        const char *filter_value = filters->pairs[i].value;

        /* Check if the current key-value pair is in the tags */
        for (size_t j = 0; j < tags->count; ++j) {
            if (strcasecmp(filter_key, tags->pairs[j].key) == 0 &&
                strcasecmp(filter_value, tags->pairs[j].value) == 0) {
                found = true;
                break;
            }
        }

        /* the current key-value is not present in the list, not matching */
        if (!found)
            return false;
    }
    /* all key-values were found */
    return true;
}

/** helper to match a name or namespace */
static bool name_matches(const char *name,
                         const char *filter)
{
    /* no filter => match any name */
    if (!filter || strlen(filter) == 0)
        return true;

    return strcasecmp(name, filter) == 0;
}

static bool stat_match(struct pho_stat *stat, const char *ns_filter,
                       const char *name_filter,
                       const struct tag_list *tag_filters)
{
    /* match metric namespace and name */
    if (!name_matches(stat->namespace, ns_filter) ||
        !name_matches(stat->name, name_filter))
        return false;

    /* match metric tags */
    return taglist_contains(&stat->tag_list, tag_filters);
}

/**
 * Get the next metric from the iterator.
 */
struct pho_stat *pho_stat_iter_next(struct pho_stat_iter *iter)
{
    if (!iter)
        return NULL;

    for (; iter->current != NULL; iter->current = iter->current->next) {
        struct pho_stat *current_stat = iter->current->data;

        if (stat_match(current_stat, iter->ns_filter, iter->name_filter,
                       &iter->tag_list)) {
            iter->current = iter->current->next;
            return current_stat;
        }
    }
    return NULL; // No more matching stats
}

/**
 * Close an iterator
 */
void pho_stat_iter_close(struct pho_stat_iter *iter)
{
    if (!iter)
        return;

    pthread_rwlock_unlock(&pho_stat_list_lock);

    free_taglist(&iter->tag_list);
    free(iter);
}
