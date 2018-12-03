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

#include "pho_type_utils.h"
#include "pho_common.h"
#include <errno.h>
#include <jansson.h>
#include <assert.h>
#include <stdbool.h>


static inline char *strdup_safe(const char *str)
{
    if (str == NULL)
        return NULL;

    return strdup(str);
}

struct dev_info *dev_info_dup(const struct dev_info *dev)
{
    struct dev_info *dev_out;

    dev_out = malloc(sizeof(*dev_out));
    if (!dev_out)
        return NULL;

    dev_out->family = dev->family;
    dev_out->model = strdup_safe(dev->model);
    dev_out->path = strdup_safe(dev->path);
    dev_out->host = strdup_safe(dev->host);
    dev_out->serial = strdup_safe(dev->serial);
    dev_out->adm_status = dev->adm_status;

    return dev_out;
}

void dev_info_free(struct dev_info *dev)
{
    if (!dev)
        return;
    free(dev->model);
    free(dev->path);
    free(dev->host);
    free(dev->serial);
    free(dev);
}

struct media_info *media_info_dup(const struct media_info *media)
{
    struct media_info *media_out;

    media_out = malloc(sizeof(*media_out));
    if (!media_out)
        return NULL;

    memcpy(media_out, media, sizeof(*media_out));
    media_out->model = strdup_safe(media->model);
    tags_dup(&media_out->tags, &media->tags);

    return media_out;
}

void media_info_free(struct media_info *mda)
{
    if (!mda)
        return;
    free(mda->model);
    tags_free(&mda->tags);
    free(mda);
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
