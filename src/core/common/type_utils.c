/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2025 CEA/DAM.
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
 * \brief  Handling of layout and extent structures.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <errno.h>
#include <jansson.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "pho_common.h"
#include "pho_type_utils.h"

bool pho_id_equal(const struct pho_id *id1, const struct pho_id *id2)
{
    if (strcmp(id1->name, id2->name))
        return false;

    if (strcmp(id1->library, id2->library))
        return false;

    if (id1->family != id2->family)
        return false;

    return true;
}

guint g_pho_id_hash(gconstpointer v)
{
    const struct pho_id *pho_id = (const struct pho_id *) v;

    return g_str_hash(pho_id->name) ^ g_str_hash(pho_id->library) ^
           g_int_hash(&pho_id->family);
}

gboolean g_pho_id_equal(gconstpointer p_pho_id_1, gconstpointer p_pho_id_2)
{
    return pho_id_equal(p_pho_id_1, p_pho_id_2);
}

void init_pho_lock(struct pho_lock *lock, char *hostname, int owner,
                   struct timeval *timestamp, bool is_early)
{
    lock->hostname = xstrdup_safe(hostname);
    lock->owner = owner;
    lock->timestamp = *timestamp;
    lock->is_early = is_early;
}

void pho_lock_cpy(struct pho_lock *lock_dst, const struct pho_lock *lock_src)
{
    lock_dst->hostname = xstrdup_safe(lock_src->hostname);
    lock_dst->owner = lock_src->owner;
    lock_dst->timestamp = lock_src->timestamp;
}

void pho_lock_clean(struct pho_lock *lock)
{
    if (lock == NULL)
        return;

    free(lock->hostname);
    lock->hostname = NULL;
    lock->owner = 0;
}

void dev_info_cpy(struct dev_info *dev_dst, const struct dev_info *dev_src)
{
    assert(dev_src);
    pho_id_copy(&dev_dst->rsc.id, &dev_src->rsc.id);
    dev_dst->rsc.model = xstrdup_safe(dev_src->rsc.model);
    dev_dst->rsc.adm_status = dev_src->rsc.adm_status;
    dev_dst->path = xstrdup_safe(dev_src->path);
    dev_dst->host = xstrdup_safe(dev_src->host);
    pho_lock_cpy(&dev_dst->lock, &dev_src->lock);
}

struct dev_info *dev_info_dup(const struct dev_info *dev)
{
    struct dev_info *dev_out;

    dev_out = xmalloc(sizeof(*dev_out));
    dev_info_cpy(dev_out, dev);

    return dev_out;
}

void dev_info_free(struct dev_info *dev, bool free_top_struct)
{
    if (!dev)
        return;
    pho_lock_clean(&dev->lock);
    free(dev->rsc.model);
    free(dev->path);
    free(dev->host);
    if (free_top_struct)
        free(dev);
}

void media_info_copy(struct media_info *dst, const struct media_info *src)
{
    memcpy(dst, src, sizeof(*dst));
    dst->rsc.model = xstrdup_safe(src->rsc.model);
    string_array_dup(&dst->tags, &src->tags);
    pho_lock_cpy(&dst->lock, &src->lock);
    string_array_dup(&dst->groupings, &src->groupings);
}

struct media_info *media_info_dup(const struct media_info *mda)
{
    struct media_info *media_out;

    media_out = xmalloc(sizeof(*media_out));

    memcpy(media_out, mda, sizeof(*media_out));
    media_out->rsc.model = xstrdup_safe(mda->rsc.model);

    string_array_dup(&media_out->tags, &mda->tags);

    pho_lock_cpy(&media_out->lock, &mda->lock);

    return media_out;
}

void media_info_cleanup(struct media_info *medium)
{
    if (!medium)
        return;

    pho_lock_clean(&medium->lock);
    free(medium->rsc.model);
    string_array_free(&medium->tags);
    string_array_free(&medium->groupings);
}

void media_info_free(struct media_info *mda)
{
    if (!mda)
        return;

    pho_lock_clean(&mda->lock);
    free(mda->rsc.model);
    string_array_free(&mda->tags);
    string_array_free(&mda->groupings);
    free(mda);
}

struct object_info *object_info_dup(const struct object_info *obj)
{
    struct object_info *obj_out = NULL;

    /* use xcalloc to set memory to 0 */
    obj_out = xcalloc(sizeof(*obj_out), 1);

    /* dup oid */
    obj_out->oid = xstrdup_safe(obj->oid);

    /* dup uuid */
    obj_out->uuid = xstrdup_safe(obj->uuid);

    /* version */
    obj_out->version = obj->version;

    /* dup user_md */
    obj_out->user_md = xstrdup_safe(obj->user_md);

    /* timeval deprec_time */
    obj_out->deprec_time = obj->deprec_time;

    /* dup grouping */
    obj_out->grouping = xstrdup_safe(obj->grouping);

    /* success */
    return obj_out;
}

void object_info_free(struct object_info *obj)
{
    if (!obj)
        return;

    free(obj->oid);
    free(obj->uuid);
    free(obj->user_md);
    free((void *)obj->grouping);
    free(obj);
}

struct copy_info *copy_info_dup(const struct copy_info *copy)
{
    struct copy_info *copy_out = NULL;

    copy_out = xcalloc(sizeof(*copy_out), 1);

    copy_out->object_uuid = xstrdup_safe(copy->object_uuid);
    copy_out->version = copy->version;
    copy_out->copy_name = xstrdup_safe(copy->copy_name);
    copy_out->copy_status = copy->copy_status;
    copy_out->creation_time = copy->creation_time;

    return copy_out;
}

void copy_info_free(struct copy_info *copy)
{
    if (!copy)
        return;

    free(copy->object_uuid);
    free((char *) copy->copy_name);
    free(copy);
}

void string_array_dup(struct string_array *string_array_dst,
                      const struct string_array *string_array_src)
{
    if (!string_array_dst)
        return;

    if (!string_array_src) {
        *string_array_dst = NO_STRING;
        return;
    }

    string_array_init(string_array_dst, string_array_src->strings,
                      string_array_src->count);
}

void string_array_init(struct string_array *string_array, char **strings,
                       size_t count)
{
    ssize_t i;

    string_array->count = count;
    if (string_array->count == 0) {
        string_array->strings = NULL;
        return;
    }

    string_array->strings = xcalloc(count, sizeof(*string_array->strings));

    for (i = 0; i < count; i++)
        string_array->strings[i] = xstrdup_safe(strings[i]);
}

void string_array_free(struct string_array *string_array)
{
    size_t i;

    if (!string_array)
        return;

    for (i = 0; i < string_array->count; i++)
        free(string_array->strings[i]);
    free(string_array->strings);

    string_array->strings = NULL;
    string_array->count = 0;
}

bool string_array_eq(const struct string_array *string_array1,
                     const struct string_array *string_array2)
{
    size_t i;

    /* Same size? */
    if (string_array1->count != string_array2->count)
        return false;

    /* Same content? (order matters) */
    for (i = 0; i < string_array1->count; i++)
        if (strcmp(string_array1->strings[i], string_array2->strings[i]))
            return false;

    return true;
}

bool string_exists(const struct string_array *string_array, const char *string)
{
    int i;

    for (i = 0; i < string_array->count; i++)
        if (strcmp(string, string_array->strings[i]) == 0)
            return true;

    return false;
}

bool string_array_in(const struct string_array *haystack,
                     const struct string_array *needle)
{
    size_t ndl_i, hay_i;

    /* The needle cannot be larger than the haystack */
    if (needle->count > haystack->count)
        return false;

    /* Naive n^2 set inclusion check */
    for (ndl_i = 0; ndl_i < needle->count; ndl_i++) {
        for (hay_i = 0; hay_i < haystack->count; hay_i++)
            if (!strcmp(needle->strings[ndl_i], haystack->strings[hay_i]))
                break;

        /* Needle string not found in haystack strings */
        if (hay_i == haystack->count)
            return false;
    }

    return true;
}

void string_array_add(struct string_array *string_array, const char *string)
{
    string_array->count += 1;
    string_array->strings = xrealloc(string_array->strings,
                                     sizeof(*string_array->strings) *
                                         string_array->count);
    string_array->strings[string_array->count - 1] = xstrdup_safe(string);
}

void str2string_array(const char *str, struct string_array *string_array)
{
    size_t count = 0;
    char *one_string;
    char *parse_str;
    char *saveptr;
    size_t i;

    if (str == NULL || string_array == NULL)
        return;

    i = string_array->count;

    if (strcmp(str, "") == 0)
        return;

    /* copy the strings list to tokenize it */
    parse_str = xstrdup(str);

    /* count number of strings in profile */
    one_string = strtok_r(parse_str, ",", &saveptr);
    while (one_string != NULL) {
        count++;
        one_string = strtok_r(NULL, ",", &saveptr);
    }
    free(parse_str);

    if (count == 0)
        return;

    /* allocate space for new strings */
    if (string_array->count > 0)
        string_array->strings = xrealloc(string_array->strings,
                                         (string_array->count + count) *
                                             sizeof(char *));
    else
        string_array->strings = xcalloc(count, sizeof(char *));

    /* fill strings */
    parse_str = xstrdup(str);

    for (one_string = strtok_r(parse_str, ",", &saveptr);
         one_string != NULL;
         one_string = strtok_r(NULL, ",", &saveptr), i++) {
        if (string_exists(string_array, one_string))
            continue;

        string_array->strings[i] = xstrdup(one_string);
        string_array->count++;
    }

    free(parse_str);
}

int str2timeval(const char *tv_str, struct timeval *tv)
{
    struct tm tmp_tm = {0};
    char *usec_ptr;

    usec_ptr = strptime(tv_str, "%Y-%m-%d %T", &tmp_tm);
    if (!usec_ptr)
        LOG_RETURN(-EINVAL, "Object timestamp '%s' is not well formatted",
                   tv_str);
    tv->tv_sec = mktime(&tmp_tm);
    tv->tv_usec = 0;
    if (*usec_ptr == '.') {
        tv->tv_usec = atoi(usec_ptr + 1);
        /* in case usec part is less than 6 characters */
        tv->tv_usec *= pow(10, 6 - strlen(usec_ptr + 1));
    }

    return 0;
}

void timeval2str(const struct timeval *tv, char *tv_str)
{
    char buf[PHO_TIMEVAL_MAX_LEN - 7];

    if (tv->tv_sec == 0 && tv->tv_usec == 0) {
        strcpy(tv_str, "0");
        return;
    }

    strftime(buf, sizeof(buf), "%Y-%m-%d %T", localtime(&tv->tv_sec));
    snprintf(tv_str, PHO_TIMEVAL_MAX_LEN, "%s.%06ld", buf, tv->tv_usec);
}

void layout_info_free_extents(struct layout_info *layout)
{
    int i;

    for (i = 0; i < layout->ext_count; i++) {
        free(layout->extents[i].address.buff);
        free(layout->extents[i].uuid);
    }
    layout->ext_count = 0;
    free(layout->extents);
    layout->extents = NULL;
}

int tsqueue_init(struct tsqueue *tsqueue)
{
    int rc;

    rc = pthread_mutex_init(&tsqueue->mutex, NULL);
    if (rc)
        LOG_RETURN(-rc, "Unable to init threadsafe queue mutex");

    tsqueue->queue = g_queue_new();

    return 0;
}

void tsqueue_destroy(struct tsqueue *tsq, GDestroyNotify free_func)
{
    int rc;

    if (free_func == NULL)
        g_queue_free(tsq->queue);
    else
        g_queue_free_full(tsq->queue, free_func);

    rc = pthread_mutex_destroy(&tsq->mutex);
    if (rc)
        pho_error(-rc, "Unable to destroy threadsafe queue mutex");
}

void *tsqueue_pop(struct tsqueue *tsq)
{
    void *data;

    MUTEX_LOCK(&tsq->mutex);
    data = g_queue_pop_tail(tsq->queue);
    MUTEX_UNLOCK(&tsq->mutex);

    return data;
}

void tsqueue_push(struct tsqueue *tsq, void *data)
{
    MUTEX_LOCK(&tsq->mutex);
    g_queue_push_head(tsq->queue, data);
    MUTEX_UNLOCK(&tsq->mutex);
}

unsigned int tsqueue_get_length(struct tsqueue *tsq)
{
    unsigned int length;

    MUTEX_LOCK(&tsq->mutex);
    length = g_queue_get_length(tsq->queue);
    MUTEX_UNLOCK(&tsq->mutex);

    return length;
}

struct pho_id *pho_id_dup(const struct pho_id *src)
{
    struct pho_id *dup;

    dup = xmalloc(sizeof(*dup));
    pho_id_copy(dup, src);
    return dup;
}
