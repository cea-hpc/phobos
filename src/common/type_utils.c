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
    if (id1->family != id2->family)
        return false;

    if (strcmp(id1->name, id2->name))
        return false;

    return true;
}

int build_extent_key(const char *uuid, int version, const char *extent_tag,
                     char **key)
{
    int rc;

    rc = asprintf(key, "%d.%s.%s", version, extent_tag, uuid);
    if (rc < 0) {
        *key = NULL;
        return -ENOMEM;
    }

    return 0;
}

int init_pho_lock(struct pho_lock *lock, char *hostname, int owner,
                  struct timeval *timestamp)
{
    lock->hostname = strdup(hostname);
    if (hostname && !lock->hostname)
        return -ENOMEM;

    lock->owner = owner;
    lock->is_external = false;
    lock->timestamp = *timestamp;

    return 0;
}

int pho_lock_cpy(struct pho_lock *lock_dst, const struct pho_lock *lock_src)
{
    lock_dst->hostname = strdup_safe(lock_src->hostname);
    if (lock_src->hostname && !lock_dst->hostname)
        return -ENOMEM;

    lock_dst->owner = lock_src->owner;
    lock_dst->timestamp = lock_src->timestamp;
    lock_dst->is_external = lock_src->is_external;

    return 0;
}

void pho_lock_clean(struct pho_lock *lock)
{
    if (lock == NULL)
        return;

    free(lock->hostname);
    lock->hostname = NULL;
    lock->owner = 0;
    lock->is_external = false;
}

void dev_info_cpy(struct dev_info *dev_dst, const struct dev_info *dev_src)
{
    if (!dev_dst)
        return;

    assert(dev_src);
    dev_dst->rsc.id = dev_src->rsc.id;
    dev_dst->rsc.model = strdup_safe(dev_src->rsc.model);
    dev_dst->rsc.adm_status = dev_src->rsc.adm_status;
    dev_dst->path = strdup_safe(dev_src->path);
    dev_dst->host = strdup_safe(dev_src->host);
    pho_lock_cpy(&dev_dst->lock, &dev_src->lock);
}

struct dev_info *dev_info_dup(const struct dev_info *dev)
{
    struct dev_info *dev_out;

    dev_out = malloc(sizeof(*dev_out));
    if (!dev_out)
        return NULL;

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

struct media_info *media_info_dup(const struct media_info *mda)
{
    struct media_info *media_out;

    media_out = malloc(sizeof(*media_out));
    if (!media_out)
        return NULL;

    memcpy(media_out, mda, sizeof(*media_out));
    media_out->rsc.model = strdup_safe(mda->rsc.model);
    tags_dup(&media_out->tags, &mda->tags);
    pho_lock_cpy(&media_out->lock, &mda->lock);

    return media_out;
}

void media_info_free(struct media_info *mda)
{
    if (!mda)
        return;
    pho_lock_clean(&mda->lock);
    free(mda->rsc.model);
    tags_free(&mda->tags);
    free(mda);
}

struct object_info *object_info_dup(const struct object_info *obj)
{
    struct object_info *obj_out = NULL;

    if (!obj)
        return NULL;

    /* use calloc to set memory to 0 */
    obj_out = calloc(sizeof(*obj_out), 1);
    if (!obj_out)
        return NULL;

    /* dup oid */
    if (obj->oid) {
        obj_out->oid = strdup(obj->oid);
        if (!obj_out->oid)
            goto clean_error;
    }

    /* dup uuid */
    if (obj->uuid) {
        obj_out->uuid = strdup(obj->uuid);
        if (!obj_out->uuid)
            goto clean_error;
    }

    /* version */
    obj_out->version = obj->version;

    /* dup user_md */
    if (obj->user_md) {
        obj_out->user_md = strdup(obj->user_md);
        if (!obj_out->user_md)
            goto clean_error;
    }

    /* timeval deprec_time */
    obj_out->deprec_time = obj->deprec_time;

    /* success */
    return obj_out;

clean_error:
    object_info_free(obj_out);
    return NULL;
}

void object_info_free(struct object_info *obj)
{
    if (!obj)
        return;

    free(obj->oid);
    free(obj->uuid);
    free(obj->user_md);
    free(obj);
}

int tags_dup(struct tags *tags_dst, const struct tags *tags_src)
{
    if (!tags_dst)
        return 0;

    if (!tags_src) {
        *tags_dst = NO_TAGS;
        return 0;
    }

    return tags_init(tags_dst, tags_src->tags, tags_src->n_tags);
}

int tags_init(struct tags *tags, char **tag_values, size_t n_tags)
{
    ssize_t i;

    tags->n_tags = n_tags;
    tags->tags = calloc(n_tags, sizeof(*tags->tags));
    if (!tags->tags)
        return -ENOMEM;

    for (i = 0; i < n_tags; i++) {
        tags->tags[i] = strdup(tag_values[i]);
        if (!tags->tags[i]) {
            tags_free(tags);
            return -ENOMEM;
        }
    }
    return 0;
}

void tags_free(struct tags *tags)
{
    size_t i;

    if (!tags)
        return;

    for (i = 0; i < tags->n_tags; i++)
        free(tags->tags[i]);
    free(tags->tags);

    tags->tags = NULL;
    tags->n_tags = 0;
}

bool tags_eq(const struct tags *tags1, const struct tags *tags2)
{
    size_t i;

    /* Same size? */
    if (tags1->n_tags != tags2->n_tags)
        return false;

    /* Same content? (order matters) */
    for (i = 0; i < tags1->n_tags; i++)
        if (strcmp(tags1->tags[i], tags2->tags[i]))
            return false;

    return true;
}

bool tags_in(const struct tags *haystack, const struct tags *needle)
{
    size_t ndl_i, hay_i;

    /* The needle cannot be larger than the haystack */
    if (needle->n_tags > haystack->n_tags)
        return false;

    /* Naive n^2 set inclusion check */
    for (ndl_i = 0; ndl_i < needle->n_tags; ndl_i++) {
        for (hay_i = 0; hay_i < haystack->n_tags; hay_i++)
            if (!strcmp(needle->tags[ndl_i], haystack->tags[hay_i]))
                break;

        /* Needle tag not found in haystack tags */
        if (hay_i == haystack->n_tags)
            return false;
    }

    return true;
}

int str2tags(const char *tag_str, struct tags *tags)
{
    char *parse_tag_str;
    char *single_tag;
    char *saveptr;

    if (tag_str == NULL || tags == NULL)
        return 0;

    if (strcmp(tag_str, "") == 0)
        return 0;

    /* copy the tags list to tokenize it */
    parse_tag_str = strdup(tag_str);
    if (parse_tag_str == NULL)
        return -errno;

    /* count number of tags in alias */
    single_tag = strtok_r(parse_tag_str, ",", &saveptr);
    size_t n_alias_tags = 0;

    while (single_tag != NULL) {
        n_alias_tags++;
        single_tag = strtok_r(NULL, ",", &saveptr);
    }
    free(parse_tag_str);

    if (n_alias_tags == 0)
        return 0;

    /* allocate space for new tags */
    if (tags->n_tags > 0) {
        tags->tags = realloc(tags->tags,
            (tags->n_tags + n_alias_tags) * sizeof(char *));
        if (tags->tags == NULL)
            return -ENOMEM;
    } else {
        tags->tags = calloc(n_alias_tags, sizeof(char *));
        if (tags->tags == NULL)
            return -ENOMEM;
    }

    /* fill tags */
    parse_tag_str = strdup(tag_str);
    if (parse_tag_str == NULL)
        return -errno;

    size_t i = tags->n_tags;

    for (single_tag = strtok_r(parse_tag_str, ",", &saveptr);
         single_tag != NULL;
         single_tag = strtok_r(NULL, ",", &saveptr), i++) {
        tags->tags[i] = strdup(single_tag);
        if (tags->tags[i] == NULL) {
            free(parse_tag_str);
            return -errno;
        }
        tags->n_tags++;
    }

    free(parse_tag_str);

    return 0;
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

int timeval2str(const struct timeval *tv, char *tv_str)
{
    char buf[PHO_TIMEVAL_MAX_LEN];

    strftime(buf, sizeof(buf), "%Y-%m-%d %T", localtime(&tv->tv_sec));
    snprintf(tv_str, sizeof(buf), "%s.%06ld", buf, tv->tv_usec);

    return 0;
}

void layout_info_free_extents(struct layout_info *layout)
{
    int i;

    for (i = 0; i < layout->ext_count; i++)
        free(layout->extents[i].address.buff);
    layout->ext_count = 0;
    free(layout->extents);
    layout->extents = NULL;
}
